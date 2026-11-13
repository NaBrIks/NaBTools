# Интерфейсы BtEngine, модулей и SIG: предложение

Исторический проект интерфейсов, не описание текущего API. Исходные Doc/ и
Python-генераторы, упоминаемые как источники, доступны в истории Git; после
переноса они удалены. Текущее состояние — [аудит](readme_retired_code_ru.md).

Последний рабочий вариант: [SIG-пакеты, output Action и владение BT](readme_sig_outputs_bt_ownership_ru.md).
Он заменяет ниже обязательную универсальную сериализацию и прикладное
DeviceOwner-владение. Core не требует SIG; BT-подсистема владеет приборами;
protocol module может прямо выдавать CBOR/STRUCT через Action приложения.

**Последующее уточнение:** [бюджет SIG и ответственность приложения](readme_ownership_and_sig_budget_ru.md)
заменяет ниже обязательный SigCatalog в движке и рекомендации о встроенной
политике lifecycle. Полный SIG не является обязательной частью firmware;
приложение решает создание/удаление, engine обеспечивает безопасные механизмы.
Приведённые ниже configure/admit/TTL и registry ownership — ранний вариант,
не текущий согласованный контракт.

Дата: 2026-10-23. Статус: **предложение, без реализации**.
Основание: [принятое направление](adr/ADR_20261023_bt-engine-modules.md).
Новое требование владельца: сохранять полезные сведения Bluetooth SIG,
предусмотреть периодическое обновление; исследовательский модуль работает по именам.
Все C++-фрагменты — эскизы объявлений в `namespace NaBTools`, а не компилируемый
заголовок. Вспомогательные value types показаны не полностью. Сигнатуры пока
не являются утверждённым API. Изменений реализации и upstream-данных нет.

## Повторная оценка

Центральный движок с модулями подходит задаче. Его стоит уточнить так:

| Вопрос | Рекомендация | Причина и цена |
|---|---|---|
| Объём движка | Координирует backend, операции, leases, события и реестр; SIG, codecs и web/CBOR подключаются отдельно | Один арбитр железа без всех протоколов в одном классе |
| Модуль | Единый lifecycle и несколько независимо регистрируемых ролей | TPMS поставляет recognizer/factory, анализатор обходится без фабрики |
| Создание устройств | Конфигурация или admission policy разрешает, registry создаёт через фабрику | Распознавание не управляет скрыто временем жизни |
| Хуки | Короткие view-callbacks движка и собственные mailboxes внешних задач | Нужны явное владение, лимиты и счётчики потерь |
| Результаты команд | Request хранит результат отдельно от потока диагностических событий | Потеря лога не теряет завершение команды |
| SIG | Определения/имена отдельно от проверенных codecs | Нельзя автоматически превратить текстовые условия YAML в надёжный декодер |
| Подключение модулей | Сначала только при остановленном engine | Hot unload требует отдельного решения о живых drivers/descriptors |

Это распространённая комбинация backend adapter, registry, factory/policy,
leases и подписок. Динамические бинарные plugins и универсальный DI framework
первому срезу не нужны. Цена схемы — явные контракты отмены, владения и очередей.

## 1. Движок и backend

```cpp
class BtEngine {
public:
    BtEngine(std::unique_ptr<IBtBackend>, EngineConfig, const SigCatalog&);
    Result<ModuleId> add_module(std::unique_ptr<IBtModule>);
    Result<Request<void>> start();
    Result<Request<void>> stop();
    void poll(WorkBudget);

    Result<ScanLease> request_scan(ScanRequest);
    Result<ConnectionLease> request_connection(ConnectionRequest);
    Result<Request<void>> answer_pairing(PairingReply);
    DeviceCommands& devices();
    EventStream& events();
    EngineSnapshot snapshot() const;
    CapabilitySet capabilities() const;
};

template<class T> class Request {
public:
    RequestId id() const;
    RequestState state() const;
    Result<T> result() const;
    Status cancel();
};

class IBackendIngress {
public:
    virtual ~IBackendIngress() = default;
    virtual IngressResult try_push(const BackendEventView&) noexcept = 0;
};

class IBtBackend {
public:
    virtual ~IBtBackend() = default;
    virtual CapabilitySet capabilities() const = 0;
    virtual Status attach(IBackendIngress&) = 0;
    virtual Status submit(const BackendCommand&) = 0;
    virtual void detach() noexcept = 0;
};
```

Result сообщает immediate ошибки: stopped, queue_full, unsupported, conflict,
invalid_argument, resource_limit. Request означает **принято в обработку**, не
завершено. Result возвращает not_ready до terminal success/failure/cancelled;
timeouts явные. Pending table ограничена. Освобождение ticket не отменяет
команду; cancel явный. Terminal-запись хранится до освобождения последнего ticket.
Результат Request не зависит от потерь в диагностических подписках.

Команды/start/stop и DeviceCommands доступны через thread-safe фасад.
Poll, состояния, callbacks модулей и драйверов исполняются последовательно
в одном контексте; рекурсивный/конкурентный poll запрещён. Snapshot возвращает
согласованные собственные данные, а не mutable ссылку. Команды из hook
откладываются до границы текущей рассылки.

Движок сам не создаёт задачу. Необязательный EspIdfEngineRunner владеет
FreeRTOS task, вызывающей poll; альтернативно poll вызывает задача приложения.
Два режима одновременно запрещены. WorkBudget ограничивает количество/время
работы за проход, не является обещанием hard realtime.

Stop запрещает новые операции, отменяет pending, закрывает ресурсы и обеспечивает
backend callback barrier до успешного завершения Request. После этого новых
пользовательских callbacks нет. Приложение сохраняет engine/backend/каталог
живыми до завершения stop. Поздние tickets/leases возвращают expired через
безопасный control block, не разыменовывают уничтоженный engine.

Первый backend — ESP-IDF/Bluedroid; NimBLE/replay/другие контроллеры возможны
позже. Capabilities отражают chip + stack + sdkconfig, включая LE/Classic/GATT,
PHY, extended advertising и реальные лимиты. Unsupported не превращается
в фиктивный успешный вызов.

SDK callback отдаёт краткоживущий BackendEventView в ingress; ingress копирует
необходимые байты/метаданные в ограниченную очередь без ожидания. Ни rendering,
ни распознавание, ни socket I/O здесь не выполняются. Advertising может теряться
со счётчиком. Для control/completions нужен резерв, согласованный с pending limit;
его отказ приводит к явному fault, а не к бесконечному pending. Detach вызывается
после shutdown; backend обязан обеспечить отсутствие callbacks после barrier.
BackendEvent и публичный BtEvent — разные контракты; SDK types не выходят в API.

ScanLease выражает спрос: passive/active, PHY, interval/window, filters, budget.
Engine согласует одну аппаратную процедуру и сообщает effective parameters;
несовместимые требования дают conflict. ConnectionLease позволяет совместно
использовать один сеанс идентифицированного peer нескольким профилям.
GATT-операции сериализуются; освобождение одного lease не разрывает остальных.
RAII release отмечается в resource table и не должен теряться при полной общей
очереди. Принудительная отмена всех demands происходит при remove/stop.

## 2. Модули и их роли

```cpp
class IBtModule {
public:
    virtual ~IBtModule() = default;
    virtual ModuleInfo info() const = 0; // key, display_name, version
    virtual Status configure(ModuleRegistrar&) = 0;
    virtual Status on_start(ModuleContext&) = 0;
    virtual void on_stop(ModuleContext&) noexcept = 0;
};

class ModuleRegistrar {
public:
    Status recognizer(IRecognizer&);
    Status driver_factory(IDeviceDriverFactory&);
    Status payload(const PayloadDescriptor&);
    Status observer(EventFilter, IEventObserver&);
    Status definitions(const DefinitionProvider&);
};

class ModuleContext {
public:
    EngineCommands& commands();
    EventPublisher& publish();
    const SigCatalog& catalog() const;
    RegistryView registry() const;
};
```

Engine владеет unique_ptr модуля. Зарегистрированные роли/metadata принадлежат
модулю и живут дольше регистрации. Configure транзакционный: дубликат symbolic key
или ошибка откатывает attach целиком. Роли включаются после общей конфигурации.
Конструкторы не запускают железо, singleton, task или глобальные подписки.
On_start вызывается после готовности backend, долгие операции оформляются Requests;
on_stop освобождает demand, а engine завершает отмену. Динамическое удаление модулей
при работающих drivers в первый срез не входит.

Модули не получают backend или mutable map устройств. ModuleContext даёт
ограниченные команды, публикацию и readonly registry. Один модуль может иметь
несколько ролей; research-модуль не обязан содержать recognizer или фабрику.

## 3. Распознавание и создание экземпляров

```cpp
class IRecognizer {
public:
    virtual ~IRecognizer() = default;
    virtual RecognizerInfo info() const = 0;
    virtual void probe(const ObservationView&, CandidateSink&) const = 0;
};
class IAdmissionPolicy {
public:
    virtual ~IAdmissionPolicy() = default;
    virtual AdmissionDecision decide(const CandidateView&,
                                     const RegistryView&) const = 0;
};
class IDeviceDriverFactory {
public:
    virtual ~IDeviceDriverFactory() = default;
    virtual DriverInfo info() const = 0;
    virtual Result<std::unique_ptr<IDeviceDriver>> create(
        const DeviceInit&, DeviceContext&) = 0;
};
class IDeviceDriver {
public:
    virtual ~IDeviceDriver() = default;
    virtual Status start(DeviceContext&) = 0;
    virtual void on_event(const DeviceEventView&, DeviceContext&) = 0;
    virtual void stop(DeviceContext&) noexcept = 0;
};
class DeviceCommands {
public:
    Result<Request<DeviceHandle>> configure(KnownDeviceSpec);
    Result<Request<DeviceHandle>> admit(CandidateId, AdmissionSpec);
    Result<Request<void>> remove(DeviceId);
};
```

Поток: observation → все подходящие recognizers → кандидаты/доказательства →
identity resolution → admission policy → registry → factory → driver.
Recognizer не создаёт экземпляр и не открывает соединение. CandidateSink
ограничивает количество/размер кандидатов. Результат содержит type/driver key,
причины совпадения, возможную identity и уровень доказанности; неоднозначность
не скрывается порядком регистрации. Confidence не выдаётся за вероятность.

AdmissionDecision: observe_only, bind_existing, create_temporary,
require_selection, reject. Выбор драйвера и разрешение активных операций явные.
Известная конфигурация может сразу создавать driver без обнаружения типа.

| Объект | Идентичность/владелец | Срок жизни |
|---|---|---|
| Observation | backend + адрес с типом + SID/параметры; observation table | Короткая bounded запись, не постоянная identity прибора |
| Device | DeviceId и прикладной symbolic key; DeviceRegistry | До remove, либо конца временного admission |
| Session | SessionId и generation; engine | До disconnect, handles привязаны к generation |

KnownDeviceSpec содержит device_key, driver_key, IdentitySelector и типизированные
настройки driver с validator модуля. Настройки не обязаны сначала сериализоваться
в CBOR. Приложение загружает их и вызывает configure. Registry создаёт Device и
driver в waiting_for_observation до первого пакета. Первый пакет обновляет тот же
Device; TPMS advertising не требует соединения. Disconnect/stale не удаляет
настроенный прибор. Временные записи имеют TTL и не удаляются, пока их удерживает
явный demand. Прикладные колёса/сезонные наборы хранятся за пределами registry.

IdentitySelector: адрес с типом, протокольный sensor ID или подтверждённая identity
стека/bonding. Имя и service UUID не идентифицируют прибор. Похожие пакеты/RPA
не сливаются автоматически. DeviceHandle проверяет generation и возвращает expired
после remove. Late notification прежнего Session не попадает в новый.

Один Device может иметь несколько facets: HRM, Battery, Device Information.
Уникальность driver — `(DeviceId, DriverKey, role)`, не отдельный физический Device
на каждый сервис. При reconnect remote handles/кэш заново проверяются; совпадение
номеров handles не доказывает неизменность GATT database.

## 4. Хуки, данные и очередь

```cpp
class IEventObserver {
public:
    virtual ~IEventObserver() = default;
    virtual void on_event(const BtEventView&) = 0;
};
class EventStream {
public:
    Result<Subscription> observe(EventFilter, IEventObserver&);
    Result<EventMailbox> mailbox(EventFilter, QueuePolicy);
};
class EventMailbox {
public:
    Result<EventSnapshot> try_take();
    MailboxStats stats() const;
    void close();
};
struct EventHeader {
    EventKey type; // symbolic name + schema version
    ModuleKey source;
    std::optional<DeviceId> device;
    std::optional<SessionId> session;
    Sequence sequence;
    MonotonicTime observed_at;
};
```

Группы: observations, recognition, device_state, connection_state,
operation_result, measurements, diagnostics. Filter выбирает группу/имя,
устройство, уровень. Для состояния не нужно парсить строку лога.

Observe исполняется в engine context, не SDK callback. View живёт до возврата;
observer не блокирует и не сохраняет указатели. RAII Subscription помечает
подписку неактивной. Из другого task перед уничтожением borrowed observer нужен
barrier завершения текущего callback: одного destructor токена для этого мало.
Из callback отписка не блокирует; дальнейшая доставка этому observer прекращается.

Mailbox имеет собственную bounded очередь и owned payload. LVGL/веб/логгер
не конкурируют за одну очередь. Копирование учитывает глубокое владение,
не memcpy структуры со span. Drop_oldest/drop_newest/coalesce и счётчики потерь
явные; coalesce подходит latest state, не всем measurements. После потери state
потребитель может запросить snapshot. Результат Request хранится отдельно.

Для extensible payload предлагается PayloadDescriptor: symbolic schema/version,
проверка C++-типа без RTTI, обход именованных полей, bounded clone/destroy.
`BtEventView::as<T>()` проверяет descriptor. Core имеет свои типизированные
события, TPMS/HRM — собственные модели. Clone никогда не сохраняет borrowed views.
Это маленькая инфраструктура событий, а не универсальная VM/язык разбора протоколов.

CborAdapter кодирует данные по descriptor/visitor и отдаёт owned bytes в очередь
приложения; TextRenderer формирует подробный текст; WebAdapter читает mailbox
в контексте сервера. Driver декодирует один раз для всех получателей.
CBOR внутри core остаётся обсуждаемым решением, не уже принятой обязанностью.

## 5. Исследование по именам и SIG API

```cpp
class SigCatalog {
public:
    CatalogRevision revision() const;
    CatalogCoverage coverage() const;
    Result<DefinitionRef> resolve(DefinitionKind, std::string_view key) const;
    DefinitionRange search(const DefinitionQuery&) const;
    NameInfo describe(const BluetoothCode&) const;
};
class DefinitionRef {
public:
    std::string_view key() const;
    std::string_view name() const;
    DescriptionView description() const;
    SourceRef source() const;
    FieldRange fields() const;
};
class ValueCodec {
public:
    virtual ~ValueCodec() = default;
    virtual DecodeStatus inspect(DefinitionRef, ByteView,
                                 NamedFieldVisitor&) const = 0;
};
class ResearchModule : public IBtModule {
public:
    Result<DefinitionRef> definition(DefinitionKind, std::string_view name) const;
    Result<Request<GattSnapshot>> discover(DeviceId, DiscoveryOptions);
    Result<CharacteristicSelection> select(const GattSnapshot&,
        std::string_view service_name, std::string_view characteristic_name) const;
    Result<Request<ValueSnapshot>> read(const CharacteristicRef&);
    Result<GattNotifyLease> subscribe(const CharacteristicRef&);
};
```

ResearchModule — пользовательский фасад. GATT-операции выполняет engine, не
второй стек анализатора. Definition — описание типа, RemoteService/CharacteristicRef
— конкретный объект сеанса с handles/generation. Select возвращает набор совпадений:
один UUID может встретиться несколько раз, read требует однозначного экземпляра.

Правила именования:

1. Основной машинный ключ — официальный identifier YAML, например
   `org.bluetooth.service.heart_rate` и
   `org.bluetooth.characteristic.heart_rate_measurement`.
2. Удобные scoped aliases `heart_rate`, `heart_rate_measurement` и точные
   display names `Heart Rate`, `Heart Rate Measurement` разрешаются через каталог.
   Неоднозначность возвращает варианты; alias не зависит от перевода.
3. TomTom, SDK errors/events, состояния движка и недостающие битовые имена
   добавляются namespaced DefinitionProvider. Не всё это содержится в SIG.
4. NameInfo различает known, unknown_code, catalog_missing, partial_definition.
   Unknown сохраняет полный код и явную именованную пометку; имя не выдумывается.
5. Research требует имена для всех поддержанных сущностей: сервисов,
   характеристик, descriptors, AD, компаний, appearance, flags, enum values,
   единиц, ошибок, состояний и модулей. Профиль без имён не выдаётся за full research;
   coverage проверяется/показывается явно. Числа/UUID/handles доступны для сверки.
6. Официальные английские имена сохраняются. Переводы — отдельное дополнение.

Пример пользовательского API без магических UUID:

```cpp
auto info = catalog.resolve(DefinitionKind::characteristic,
    "org.bluetooth.characteristic.heart_rate_measurement");
auto choices = research.select(gatt_snapshot, "Heart Rate", "Heart Rate Measurement");
// Проверить результат и выбрать один экземпляр с нужным handle из choices.
```

Пример желаемого вывода, не показание реального прибора:

```text
Service: Heart Rate [0x180D]
  Characteristic: Heart Rate Measurement [0x2A37]
  Properties: Notify
  Heart rate: 72 beats/minute
  Sensor contact: detected
  RR interval: 0.833 seconds
```

Codec сообщает decoded/partial/unsupported/malformed, raw offsets, единицы,
unknown sentinels и reserved bits. Проверенный HRM codec не означает поддержку
всех характеристик. Крупные descriptions immutable и разделяются объектами;
они не копируются в каждый пакет. View и owned Snapshot используют один разбор
и visitor именованных полей. Постоянное дерево каждого advertising не требуется.
Bytes comparison, semantic comparison и diff — отдельные явные операции.

## 6. Что использовать из bluetooth-SIG

Проверенная локальная coverage:

| Материал | Польза |
|---|---|
| assigned_numbers — 121 файл | Компании, AD, UUID, appearance, Class of Device, formats, PSM, SDP, mesh и параметры профилей; есть вложенные таблицы |
| gss — 261 файл | В этом snapshot все файлы — характеристики: identifier, name, descriptions, структура/типы, flags, условия, значения |
| dp — 185 файлов | Properties, groups, ссылки на characteristic и смысл величины; не автоматически список сервисов прибора |
| uuids/descriptors.yaml | Имена и UUID дескрипторов; отдельных descriptor YAML в локальном gss не обнаружено |

GenAll.py сейчас обрабатывает только 14 выбранных плоских таблиц, не GSS/DP,
теряет часть identifier/descriptions и не поддерживает все вложенные структуры.
Следующий генератор должен сохранять source/commit, identifier, name,
description, связи и coverage. Библиотека определений полезна до появления codecs.

Условные поля GSS часто описаны текстом: например, HRM выбирает 8/16-bit поле
по биту Flags, затем optional Energy Expended и массив RR intervals. Для
температуры есть signedness/scale/unknown sentinel в description. Это материал
для проверки codec и нормализации overlay, не готовые машинные выражения.
Ссылки autoref нельзя молча выбросить вместе со смыслом условий.

## 7. Синхронизация SIG

Раньше TPMS-проект подключал submodule
`https://bitbucket.org/bluetooth-SIG/public.git` на main и обновлял
`git submodule update --remote`. В NaBTools сейчас snapshot без Git metadata;
сам он не синхронизируется. Его commit:
`4f955efbe9144b01c02e2ee2044f43d510cc8da0` от 2025-02-28.
Read-only `git ls-remote` 2026-10-23 вернул другой HEAD/main:
`e7f76d7c5e2edced4872a352ff4a300610ac8212`. Новое дерево не скачивалось;
изменения данных/схем по одному ref не установлены.

Рекомендация: вернуть pinned upstream submodule **в новом проекте**, а
производные каталоги/таблицы хранить здесь как reviewable source artifacts.
Это знакомая модель с фиксируемым commit; цена — submodule init при получении
источников. Альтернатива: snapshot + собственный updater/cache/lock manifest;
меньше Git-сложности пользователю, но больше своего tooling.

Обычная сборка работает с зафиксированными derivatives без сети. Не двигать
source отдельно от генерации. Предлагаемый workflow, ещё не реализованный:

1. Check сравнивает ref с зафиксированной ревизией, не меняя working tree.
2. Prepare(commit) получает upstream в staging, проверяет YAML и генерирует
   детерминированные artifacts вне рабочих файлов.
3. Отчёт показывает новые/переименованные/удалённые определения, изменения форматов,
   неоднозначные aliases, coverage, flash size и неподдержанные конструкции.
4. Проверки: UUID↔identifier↔unit/property links, unknown codes/bounds, aliases,
   эталонные codecs. Совпадение source hash не доказывает правильность декодера.
5. Принятие переносит pinned source, manifest и derivatives согласованно;
   прерванное обновление восстанавливается из staging и не выдаёт смесь версий
   за согласованный каталог. Старые aliases/definitions не удаляются молча.

CatalogRevision: source URL/commit, generator version, schema version,
profile/coverage, checksum результата. Capture сохраняет эту ревизию.
Рекомендуем check раз в неделю и перед выпуском; update после проверки отчёта.
Планировщик и автоматическое обновление в этой задаче не создавались.
Исходные notices сохраняются, условия SIG не заменяются лицензией собственного кода.

## 8. Следующий срез

1. Безопасный parser/lookup, owned/view и tests malformed/unknown.
2. Именованный каталог определений и SIG servicing; затем проверенные codecs.
3. Engine + один backend + passive scan lease + analyzer + TomTom recognizer/factory.
4. Requests/mailboxes/диагностика, CBOR-адаптер, затем GATT explorer/HRM.

Критерии первого engine-среза: один scan для двух модулей, анализатор видит и
распознанные и unknown пакеты, два заранее настроенных TPMS Device получают
раздельные обновления, медленный логгер не блокирует BT, ошибки и поля имеют имена.
Эти проверки ещё не выполнялись. Hot unload, аудио и настройки автопарка в первый
срез не входят. Точные EngineConfig limits, первый codec, scan conflict rules,
финальный CBOR schema и долгосрочная совместимость BTController ещё обсуждаются.

## Источники

- [SIG Assigned Numbers](https://www.bluetooth.com/specifications/assigned-numbers/): официальный YAML upstream.
- [GATT Specification Supplement](https://www.bluetooth.com/specifications/gss/): определения характеристик и YAML.
- [Device Properties](https://www.bluetooth.com/specifications/specs/device-properties/): свойства и YAML.
- [ESP-IDF GAP API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-reference/bluetooth/esp_gap_ble.html): тяжёлую работу из SDK callback следует переносить.
- Локальные Doc/ReadMe.md, Python/GenAll.py, YAML snapshot и .gitmodules TPMS-проекта: факты о прежнем обновлении и текущих данных.

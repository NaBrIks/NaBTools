# Стандартный пульсометр: первый переносимый модуль

Реализованы парсер, распознаватель, BT-owned экземпляр, прямые выходы STRUCT /
CBOR и переносимый клиент профиля через общий IGattClient. После явной команды
Connect драйвер автоматически обнаруживает Heart Rate Service и подписывается
на измерения. Проверки активного драйвера ниже используют native fake transport;
аппаратная приёмка конкретного backend/C6 фиксируется отдельно. Реклама Garmin
сама по себе не подтверждает получение измерений пульса.

## Граница протокола

Основание распознавания — UUID `0x180D` в полных/неполных списках UUID16
рекламы. Имя Garmin и company ID сами по себе недостаточны. Это заявление
сервиса в рекламе, не проверка подлинности или обнаруженного дерева GATT.
UUID128 и другие формы рекламы пока не распознаются этим модулем.

`HeartRateMeasurement::parse(Bytes)` разбирает **значение характеристики
`0x2A37`**, не рекламу. BPM: UINT8 / UINT16; контакт: support + optional detected;
энергия: optional UINT16 kJ; RR: последовательность UINT16 в исходных единицах
1/1024 секунды, от старого к новому. Все многобайтные поля little endian.
RR flag требует хотя бы одно целое значение; пустой/нечётный RR отвергается.
Усечение после целого RR неотличимо от самостоятельного более короткого пакета.

RFU flags и необъявленный хвост сохраняются, `standard_layout=false`.
Это разбор известной части текущего формата, не обещание поддержки будущей
ревизии. Неизвестный хвост не становится измерением. При неподдерживаемом
контакте detected возвращается пустым, а не ложным утверждением о коже.

Первичный источник: [Bluetooth SIG Heart Rate Service 1.0, раздел 3.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/HRS_v1.0/out/en/index-en.html).
RR units сверены также с локальным
`bluetooth-SIG/gss/org.bluetooth.characteristic.heart_rate_measurement.yaml`.
Энергия названа `energy_kj` по нормативному HRS §3.1.1.3; одинокая запись
`joule` в GSS YAML не используется как множитель 1 J. Полной сертификации HRS
или Heart Rate Profile этим кодом не заявлено.

## Использование и владение

`HeartRateModule` (`nabtools.bluetooth.hrm`) подключается как обычный IModule.
Recognition не создаёт экземпляры; `engine.attach` делает это явно.
`add_known` позволяет приложению передать заранее настроенный адрес.
Экземпляр `HeartRateDevice` хранится в BtEngine, приложение владеет конфигурацией
и контекстами Actions. Никакой привязки к ESP-IDF или Garmin у модуля нет.

Для внешнего адаптера / replay сохранён защищённый borrowed вход:

```cpp
device->on_measurement(engine, id, characteristic_value, received_time_ms);
```

Такой адаптер обязан подтвердить сеанс и соответствие discovered handles сервису
`0x180D` / характеристике `0x2A37`. После позднего SDK callback сначала получает
текущий экземпляр по DeviceId/generation, не сохраняет сырой указатель прибора.
SDK callback сам не вызывает модуль: передаёт принадлежащие очереди данные
в сериализованный контекст движка. После возврата borrowed payload недействителен.
Обычный GATT путь доставляет owned GattEvent через `IDevice::on_gatt` и вызывает
внутренний parser в уже защищённом dispatch, без рекурсивного входа.

Вход использует `BtEngine::dispatch` с защитой срока жизни до конца callback.
Рекурсивный dispatch возвращает busy. Удаление из Action сразу делает DeviceId
недоступным и отменяет следующий выход этого измерения; деструктор выполняется
после возврата защищённого вызова. Адресная ротация/identity resolution пока
не реализованы. Максимальный вход ограничен 512 байтами политикой модуля;
это не результат согласования ATT MTU.

Объявление STRUCT даёт `HeartRateAction` с borrowed `HeartRateMeasurement`.
`NABTOOLS_HRM_STRUCT` и `NABTOOLS_HRM_CBOR` независимы. CBOR формируется напрямую,
без общего DOM; typed view не требуется как транспортная структура. Приложение
может подключить один или оба Actions. Для отложенной обработки копирует
нужные bytes или разобранные значения. Модуль не сохраняет последний payload.

Каждый валидный вход характеристики увеличивает sequence и выдаёт сообщение,
даже если все байты равны предыдущим: два одинаковых BPM — два измерения.
Реклама только обновляет присутствие / счётчик packets и не создаёт измерений.
`measurements()` считает принятые измерения; `invalid_measurements()` — неверные
или превышающие лимит входы. delivered/dropped считают результаты Actions.
Время MessageInfo — время приёма адаптером, не метка времени измерения датчика.

## Прямой CBOR

Schema в MessageInfo: `nabtools.bluetooth.hrm.measurement.v1`.
Payload: `type="measurement"`, `raw`, `flags`, `reserved_flags`,
`standard_layout`, `heart_rate_bpm`, `contact_supported`, `contact_detected`
(bool/null), `energy_kj` (number/null), `rr_1_1024_s` (array), `unknown_tail`.
Сырые поля — byte strings. MessageInfo передаёт DeviceId, receive time и sequence;
общий исследовательский журнал уже умеет копировать такие сообщения.

Состояния передаются отдельной schema `nabtools.bluetooth.hrm.state.v1`: `type="state"`,
`event`, `state`, `status`, `error`, `request_id`, `session_slot`,
`session_generation`, `measurement_handle`, `control_handle`. Sequence состояний
независим от sequence измерений. В STRUCT доступен отдельный `HeartRateStateAction`;
в CBOR-only обязательной структуры состояния нет. Ошибка не становится измерением.

`NABTOOLS_DEVICE_DESCRIPTIONS` включает динамические описания connect/disconnect,
subscribe/reset energy и state/measurement events. Доступность зависит от
`IGattClient::stats().ready`, сеанса и реально обнаруженных handles. Connect
не требует пересоздания прибора, если backend зарегистрировался позже.
Без описаний прикладной код вызывает driver invoke через защищённый dispatch;
общая UI-команда требует включённого каталога. Аргументы команд пока пустые:
пустые bytes/format либо пустая CBOR map (`application/cbor`, также `cbor`).

После Connect: connected → discover `0x180D` → выбрать `0x2A37` с Notify
(иначе Indicate) и его CCCD `0x2902` → subscribe → subscribed → notifications.
Измерения принимаются только по выбранному value handle, текущему SessionId
и DeviceId. Старые callbacks и чужие handles игнорируются. GATT handles
не сохраняются между сеансами. Частичный discovery без CCCD даёт ошибку состояния.
При ошибке подписки можно повторить Subscribe; при неполном discovery —
Disconnect/Connect. Автоматического reconnect нет.

`energy.reset` доступен только после обнаружения writable `0x2A39`; он отправляет
`0x01` с ответом. Body Sensor Location / Battery пока не читаются.

## Проверка и интеграция

`tests/profiles/heart_rate/hrm.cpp` — синтетические входы по спецификации, не записи Garmin.
Проверяются границы, все 256 сочетаний flags, порядок/единицы RR, неизвестные
поля, независимые outputs, одинаковые реальные входы, отсутствие измерений
из рекламы, старые поколения и удаление из callback. Бинарный выход теста
можно отдельно декодировать независимым CBOR codec.

При разработке прошли 3/3 отдельных native режима MSVC 19.44 (STRUCT, CBOR,
BOTH) и независимое декодирование обоих бинарных выходов через cborg. Проверен
также вход 512 байт / 255 RR, ограничение размера и рекурсивный ingress (busy).
Промежуточные проекты и результаты находятся в ignored `local/hrm-agent/`.
Для активного драйвера отдельно прошли 3/3 режима в `local/hrm-agent-active/`:
поздняя готовность backend, цепочка connect/discover/subscribe, Notify/Indicate,
повтор подписки после ошибки, точный energy reset, missing CCCD/control point,
backend error, неправильные сеансы/handles, некорректное уведомление и удаление
из GATT callback с отменой транспорта. Проверяются MIME `application/cbor`
и раздельные state/measurement сообщения. Эти входы синтетические.

В общую сборку подключён `src/profiles/heart_rate/hrm.cpp`; добавлены defaults
`NABTOOLS_HRM_STRUCT` / `NABTOOLS_HRM_CBOR` и IDF Kconfig counterparts
`CONFIG_NABTOOLS_HRM_STRUCT` / `CONFIG_NABTOOLS_HRM_CBOR`. Native tests собираются в трёх
режимах STRUCT / CBOR / BOTH; CBOR-only/combined линкуют `src/core/cbor.cpp`.
В research application установлен `HeartRateModule` с Action общего журнала;
специальных полей пульсометра в веб-просмотрщик не добавлено. Исходники/тесты
размещены по [одобренной иерархии](readme_layout_ru.md), прежний `nabtools/hrm.hpp`
сохранён перенаправлением. Общая native сборка после GATT прошла 17/17, web 8/8.
Реальный пульс Garmin подтверждён отдельно: [GATT и приёмка C6](readme_gatt_ru.md).

Native тест проверяет переносимый профиль и lifecycle с fake IGattClient.
Положительный native результат сам по себе не доказывает приём пульса C6.

## Аппаратная проверка

Стандартный HRS профиль проверен с реальным BLE-пульсометром на ESP32-C6:
connect → discovery → CCCD subscribe → notification → прямой CBOR Action.
Измерения появляются в общем viewer. Непереданные contact/energy/RR остаются
отсутствующими. Реальная приёмка energy reset, security и нескольких одновременных
пульсометров этим не доказана. Методика транспорта: [GATT](readme_gatt_ru.md).

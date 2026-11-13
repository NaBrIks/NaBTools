# Каталог Bluetooth SIG

`bluetooth-SIG` — подмодуль официального репозитория. Git фиксирует его ревизию.
В обычном checkout достаточно профиля OFF (по умолчанию). В этом режиме исходники
и таблицы SIG не участвуют в сборке.

Для профилей COMMON, EXTENDED и FULL:

```sh
git submodule update --init --checkout -- bluetooth-SIG
python -m pip install -r tools/requirements.txt
python tools/generate_sig.py
python tools/generate_sig.py --check
```

Генератор проверяет SHA и чистоту подмодуля и создаёт один локальный include.
Препроцессор выбирает нужный раздел. В Git остаётся только ссылка на подмодуль,
код генератора и ручные списки отбора. Для проверки native-матрицы используйте
`-DNABTOOLS_TEST_SIG_PROFILES=ON`. Для ESP-IDF выберите профиль `NABTOOLS_SIG_*`
в Kconfig. CMake сообщает об отсутствующей таблице при включённом каталоге.

Для Git-компонента в ESP-IDF можно сгенерировать таблицы вне
`managed_components`: передайте `--output <каталог>/sig.generated.inc`
генератору, затем `-DNABTOOLS_SIG_DATA_DIR=<каталог>` CMake. Генерацию выполняйте
из обычного checkout NaBTools с инициализированным SIG. Некоторые версии
ESP-IDF Component Manager рекурсивно обновляют подмодули при загрузке компонента.
Для SIG установлено `update = none`, поэтому обычное рекурсивное обновление его
пропускает. Явная команда с `--checkout` выше включает загрузку.

## Снимок от 23.10.2026

Источник — официальный [YAML repository Bluetooth SIG](https://bitbucket.org/bluetooth-SIG/public/src/main/),
на который ссылается [страница Assigned Numbers](https://www.bluetooth.com/specifications/assigned-numbers/).
Прежний commit: `4f955efbe9144b01c02e2ee2044f43d510cc8da0`.
Новый: `e7f76d7c5e2edced4872a352ff4a300610ac8212`, 22.10.2026 15:20:34 UTC.
Импортированы 608 файлов, 2 260 422 байта, включая notices и материалы GSS/DP.
В прошивку попадают только выбранные сгенерированные таблицы имён.

| Таблица | Было | Стало | Добавлено | Удалено | Переименовано |
|---|---:|---:|---:|---:|---:|
| Компании | 3754 | 4057 | 552 | 249 | 24 |
| Типы AD | 50 | 50 | 0 | 0 | 1* |
| Сервисы | 71 | 76 | 5 | 0 | 0 |
| Характеристики | 471 | 512 | 41 | 0 | 1 |

*В SIG AD 0x10 имеет два контекстных имени: Device ID и Security Manager TK Value.
Прежний runtime генератор молча оставлял последнее; теперь сохраняет оба
через `/`. Это исправление представления, не переименование в upstream.
Прежний дублирующий генератор и его API удалены 29.10.2026.

Удалённые upstream записи не склеены обратно в актуальный каталог. Полный
список diff и контрольные суммы доступны в истории до перехода на подмодуль.
В текущем дереве версию данных определяет SHA подмодуля.

Владелец подтвердил включение Huami `0x0157` в COMMON. Это производитель
носимой электроники группы Zepp Health: Amazfit/Zepp; см.
[профиль](https://www.zepp.com/company-profile) и
[историю переименования](https://www.zepp.com/press-release/huami-corporation-announces-name-change-to-zepp-health).
Имя SIG сохранено официальное, модель по одному ID не определяется.
COMMON теперь 16 компаний; EXTENDED 84; FULL 4057. Это ручная подборка.
Отсутствующему ID нельзя придумывать производителя. «Нет в COMMON» означает
отсутствие в выбранном профиле, а не обязательно отсутствие во всём SIG.

## Покрытие таблиц, 29.10.2026

Единый список входов — `tools/sig_tables.py`. Поддержаны компании, типы AD,
service/characteristic UUID, дескрипторы, декларации, единицы, service class,
протоколы, member/SDO UUID, browse group, mesh profile, object type и appearance.
Все категории прежних таблиц перенесены; appearance добавлен отдельно.

COMMON использует ручные списки из `tools/sig_profiles.json`; для member/SDO/mesh
он пустой. В EXTENDED/FULL остальные категории полные в пределах снимка:
24 дескриптора, 4 декларации, 127 единиц, 76 service class, 24 протокола,
724 member, 17 SDO, 1 browse group, 7 mesh profile, 6 object type, 342 appearance.
Компании по-прежнему различаются: 84 в EXTENDED, 4057 в FULL.
Имена доступны через `sig::lookup(Table, code)` и типизированные функции.
`sig::uuid` ищет только точные Bluetooth-base UUID; vendor UUID не угадывается
по последним байтам. Неизвестный код возвращает null, исходное число сохраняется.
Названия Classic/SDP UUID не означают наличие Classic/SDP транспорта.

## Следующее обновление

Получите нужный commit в подмодуле, переключите его на этот SHA и добавьте
`bluetooth-SIG` в индекс. Запустите генератор и `--check`, затем проверьте native
и firmware-профили. При обычной компиляции генератор и сетевой fetch не запускаются.

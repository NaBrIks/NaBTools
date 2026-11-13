# Структура исходников

Структура одобрена владельцем и применена 23.10.2026. Группируем по протоколу
и ответственности; публичные заголовки и реализации повторяют одну иерархию.

```text
include/NaBTools/                      src/
  core/                             core/
    action.hpp                        cbor.cpp
    cbor.hpp
    config.hpp
    status.hpp
    device_id.hpp
    uuid.hpp
    units.hpp
  bt/                               bt/
    engine.hpp                        engine.cpp
    interaction.hpp
    gatt/client.hpp                   gatt/client.cpp
    gap/advertisement.hpp             gap/advertisement.cpp
    gap/ad_view.hpp
    gap/classification.hpp            gap/classification.cpp
  profiles/                         profiles/
    heart_rate/hrm.hpp                heart_rate/hrm.cpp
  devices/                          devices/
    samsung/smarttag/smarttag.hpp      samsung/smarttag/smarttag.cpp
                                      samsung/smarttag/gatt.cpp
    samsung/smarttag/owner.hpp         samsung/smarttag/owner.cpp
                                      samsung/smarttag/owner_gatt.cpp
    tomtom/tpms/tpms.hpp               tomtom/tpms/tpms.cpp
  research/research.hpp              research/research.cpp
  catalogs/sig/sig.hpp               catalogs/sig/sig.cpp
                                    catalogs/sig/sig.generated.inc
backends/espidf/                   # аппаратный SDK, owned callback queue
integrations/research/             # WS/CBOR-команды, поток и прикладной журнал
integrations/espidf/samsung/        # mbedTLS/RNG адаптер для SmartTag
examples/research/                 # все собственные исходники основной демки
  main/                           # композиция прошивки, HTTP/WS endpoint
  display/research_lcd7/           # необязательный экран Waveshare S3 / LVGL
  web/                            # HTML/CSS/JS и значки
  tools/                          # pack_research.py, preview_research.mjs
  tests/web/                      # DOM и WS/CBOR проверки демки
  tests/lcd7/                     # headless LVGL отрисовка и очередь команд
tests/
  bt/
  profiles/heart_rate/
  devices/samsung/smarttag/
  devices/tomtom/tpms/
  integrations/research/
  research/
```

GATT client находится в bt/gatt, адаптер — в backends/espidf/gatt_client.*.
Security и новые профили получат каталоги при появлении кода; пустые заготовки
не создавались. Общие DeviceId и UUID находятся в core. CBOR-кодек остаётся необязательным для типизированных потребителей.

Внутри модуля рядом живут разбор пакета, распознавание, экземпляр, команды,
описания и выходы. Маленькому модулю достаточно одной пары .hpp/.cpp.
Когда он вырастет, разделяем на packet/recognizer/device/description/output_cbor
в том же каталоге; не создаём глобальную россыпь всех recognizers отдельно
от всех drivers. Стандартный Heart Rate находится в profiles, а возможное
Garmin-расширение будет отдельным devices/garmin модулем.

В include/NaBTools/*.hpp есть короткие пути-перенаправления.
Новый код использует полный путь модуля. Старое расположение backend header
integrations/espidf/NaBTools_espidf.hpp также перенаправляет на backends/espidf.
В новых папках нет дублирующих реализаций: CMake собирает только новые пути.
SIG generator обновлён и проверяет новый путь производного файла.

## Одна реализация

29.10.2026 полезные обёртки и категории SIG перенесены в эти папки, а плоские
`src/NaBTools*`, дублирующие таблицы, прежние генераторы и Doc/ удалены.
См. [состав переноса и аудит](readme_retired_code_ru.md).
Полный upstream `bluetooth-SIG/` сохранён с provenance/notices как вход генератора.
Архивные ADR и манифест извлечения описывают прошлое, а не текущее дерево.

Документы остаются точками входа в docs; по мере роста модульных руководств
их можно сгруппировать отдельно, сохраняя ссылки. Это не причина создавать
папку или отдельный файл для каждой фирмы в каталоге SIG.

## Проверки после переноса

Общая native сборка включает 20 тестов на Windows, в том числе заимствованные
AD/UUID, метаданные радио, четыре SIG-профиля, GATT и режимы модулей.
Веб-проверки запускаются через `npm test` из корня проекта; исходники —
`examples/research/tests/web/`.
Сохраняются прежние имена бинарных результатов в local/native-build,
чтобы средства проверки данных не зависели от размещения исходников.
Отдельно проверяются ESP-IDF C6/S3 research и S3 smoke текущего API; сборка S3
не означает прошивку этой платы.

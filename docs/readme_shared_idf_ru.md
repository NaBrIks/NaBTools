# Рекомендуемая общая ESP-IDF 5.5.6

Это предусмотренная базовая конфигурация для воспроизведения, **не обязательное
ограничение** на расположение SDK или на другие явно выбранные версии.
Полная [структура каталогов, версии и перенос на другое рабочее место](readme_development_environment_ru.md).

По умолчанию используется `~/.platformio`, в Windows — `%USERPROFILE%\.platformio`.
SDK базового профиля: `<pio-packages>/framework-espidf-v5.5.6`.
`symlink://` ссылается на выбранный packages без копирования SDK в проект.
`PLATFORMIO_CORE_DIR` и `PLATFORMIO_PACKAGES_DIR` позволяют выбрать другие пути;
скрипты с `-CoreDir` принимают явное расположение Core.

Текущие профили закрепляют Espressif32 8.0.0, ESP-IDF 5.5.6 и GCC
14.2.0+20260121 для повторяемости. Другие сочетания задаются отдельным явным
профилем/конфигом и проверяются отдельно. SDK и её PlatformIO-метаданные следует
подготовить заранее для выбранного профиля. Исторические отчёты с IDF 5.4.0
описывают прежние проверки и не переопределяют текущую конфигурацию.

Из корня репозитория:

```powershell
./tools/build_smoke.ps1
pio run -d examples/research -e c6
pio run -d examples/research -e s3
```

Smoke проверяет библиотечное подключение на ESP32-S3; research собирает
современный runtime, Bluetooth backend и соседний компонент NaWServer.
При переходе между C6 и S3 Component Manager обновляет целевую платформу
в `dependencies.lock`; SDK в обоих случаях остаётся 5.5.6.

Интеграция использует типизированный `NaWServer::HttpMethod`.
В research доступ к нативному HTTPD-запросу оформлен через явный ESP-IDF
адаптер `NaWServer::espIdfRequest`; синхронная отправка снимка остаётся прежней.
Файл backend называется `espidf_backend.cpp`, чтобы PlatformIO не сводил его
и одноимённый SSE backend NaWServer к одному объекту `backend.cpp.o`.

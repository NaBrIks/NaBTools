# Исследовательская Bluetooth-демка NaBTools

Это основной пример, который принимает BLE-наблюдения и показывает их через
NaWServer и WebSocket/CBOR. Собственные исходники демки собраны здесь:

| Каталог | Содержимое |
|---|---|
| `main/` | Композиция прошивки, HTTP assets и WS endpoint |
| `web/` | HTML, CSS, JavaScript, значки и их лицензии |
| `tools/pack_research.py` | Упаковка вебки в generated C++ header |
| `tools/preview_research.mjs` | Локальный preview с синтетическими данными |
| `tests/web/` | DOM, WS/CBOR и совместимость с native fixtures |
| `display/research_lcd7/` | Необязательный LVGL экран для Waveshare S3 800×480 |
| `tests/lcd7/` | Проверка и отрисовка экранного UI настоящим LVGL на компьютере |
| `platformio.ini`, `CMakeLists.txt`, `prepare.py` | Сборка примера |

Демка использует библиотечные `include/NaBTools`, `src`, `backends/espidf`.
Переиспользуемые WS/CBOR команды, поток, снимки и журнал находятся в
`integrations/research` в корне библиотеки. Это зависимости демки, а не ещё
одна вебка; они не копируются сюда вторым экземпляром.

Из корня NaBTools:

```powershell
pio run -d examples/research -e c6
pio run -d examples/research -e s3_lcd7
npm test
npm run preview
```

Preview доступен на `http://127.0.0.1:5279/` и показывает **синтетические данные**.
Для тестов нужны fixtures от native CMake/CTest сборки в `local/native-build`.
Упаковка вебки автоматически запускается перед сборкой PlatformIO; вручную:

```powershell
python examples/research/tools/pack_research.py
```

Wi-Fi задаётся в ignored `main/wifi_secret.hpp`. Generated assets, локальные
ключи, прошивки и журналы не включаются в Git. Изменения вебки попадут на MCU
после сборки и явной прошивки; файловый перенос сам по себе плату не обновляет.

Экран S3: [компоновка, границы и сборка](display/research_lcd7/README.md).
На ESP-IDF 5.5.6 NaWServer HTTPD WebSocket требует включённого
`CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT`; это учтено в defaults примера.

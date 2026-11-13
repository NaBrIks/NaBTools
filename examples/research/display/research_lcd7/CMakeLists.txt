# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
if(NOT IDF_TARGET STREQUAL "esp32s3")
  message(FATAL_ERROR "The research LCD7 port requires ESP32-S3 RGB/PSRAM hardware")
endif()
idf_component_register(SRCS board.cpp display_ui.cpp cbor_text.cpp
    INCLUDE_DIRS . ../../../../integrations/research
    PRIV_REQUIRES NaBTools lvgl__lvgl espressif__cbor esp_lcd driver esp_timer
        espressif__esp_lcd_touch espressif__esp_lcd_touch_gt911)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_definitions(${COMPONENT_LIB} PRIVATE NABTOOLS_LCD7_TOUCH=1)

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
set(research_display_requirements)
if(NABTOOLS_LCD7)
  list(APPEND research_display_requirements research_lcd7)
endif()
idf_component_register(SRCS main.cpp live_transport.cpp ../../../integrations/research/command.cpp
    INCLUDE_DIRS . ../../../integrations/research
    PRIV_REQUIRES NaBTools NaWServer nvs_flash esp_wifi esp_event esp_netif esp_timer mbedtls ${research_display_requirements})
target_compile_definitions(${COMPONENT_LIB} PRIVATE NABTOOLS_LCD7=$<BOOL:${NABTOOLS_LCD7}>)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)

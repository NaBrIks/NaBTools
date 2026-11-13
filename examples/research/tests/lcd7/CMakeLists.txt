# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
cmake_minimum_required(VERSION 3.20)
project(NaBToolsResearchScreen LANGUAGES C CXX)
enable_testing()
get_filename_component(NABTOOLS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(LCD_SOURCE "${NABTOOLS_ROOT}/examples/research/display/research_lcd7")
set(LVGL_SOURCE_DIR "${NABTOOLS_ROOT}/examples/research/managed_components/lvgl__lvgl" CACHE PATH "LVGL 9.6 source")
set(TINYCBOR_SOURCE_DIR "${NABTOOLS_ROOT}/examples/research/managed_components/espressif__cbor/tinycbor" CACHE PATH "TinyCBOR source")
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_USE_STDLIB_MALLOC=1 LV_FONT_MONTSERRAT_14=1)
add_subdirectory("${LVGL_SOURCE_DIR}" lvgl)
add_executable(research_screen screen.cpp "${LCD_SOURCE}/display_ui.cpp" "${LCD_SOURCE}/cbor_text.cpp"
    "${NABTOOLS_ROOT}/src/bt/engine.cpp" "${NABTOOLS_ROOT}/src/bt/gap/advertisement.cpp"
    "${NABTOOLS_ROOT}/src/bt/gap/classification.cpp"
    "${NABTOOLS_ROOT}/src/bt/gatt/client.cpp" "${NABTOOLS_ROOT}/src/core/cbor.cpp"
    "${NABTOOLS_ROOT}/src/research/research.cpp" "${NABTOOLS_ROOT}/src/catalogs/sig/sig.cpp"
    "${TINYCBOR_SOURCE_DIR}/src/cborparser.c" "${TINYCBOR_SOURCE_DIR}/src/cborparser_float.c")
target_include_directories(research_screen PRIVATE "${LCD_SOURCE}" "${NABTOOLS_ROOT}/include"
    "${NABTOOLS_ROOT}/integrations/research" "${TINYCBOR_SOURCE_DIR}/src")
target_compile_features(research_screen PRIVATE cxx_std_20)
target_link_libraries(research_screen PRIVATE lvgl)
if(MSVC)
  target_compile_options(research_screen PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME research_screen COMMAND research_screen "${CMAKE_CURRENT_BINARY_DIR}")

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../.." "${CMAKE_CURRENT_LIST_DIR}/../../../NaWServer")
option(NABTOOLS_LCD7 "Build the Waveshare S3 800x480 research screen" OFF)
if(NABTOOLS_LCD7)
  list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/display/research_lcd7")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nabtools_research)

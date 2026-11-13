// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "esp_err.h"
#include <cstdint>
esp_err_t initialize_display();
// Read only from the same LVGL task, after synchronous refresh.
uint32_t display_frame_count();
uint16_t display_pixel(int32_t x, int32_t y);
bool display_touch_available();
uint32_t display_touch_errors();

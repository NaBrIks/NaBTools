// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "esp_gap_ble_api.h"
constexpr int ESP_BLUEDROID_STATUS_UNINITIALIZED = 0, ESP_BLUEDROID_STATUS_INITIALIZED = 1,
    ESP_BLUEDROID_STATUS_ENABLED = 2;
inline int esp_bluedroid_get_status()
{
	return ESP_BLUEDROID_STATUS_UNINITIALIZED;
}

inline int esp_bluedroid_init()
{
	return ESP_OK;
}

inline int esp_bluedroid_enable()
{
	return ESP_OK;
}

inline int esp_bluedroid_disable()
{
	return ESP_OK;
}

inline int esp_bluedroid_deinit()
{
	return ESP_OK;
}

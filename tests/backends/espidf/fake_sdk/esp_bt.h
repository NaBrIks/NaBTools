// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "esp_gap_ble_api.h"
constexpr int ESP_BT_CONTROLLER_STATUS_IDLE = 0, ESP_BT_CONTROLLER_STATUS_INITED = 1,
    ESP_BT_CONTROLLER_STATUS_ENABLED = 2, ESP_BT_MODE_BLE = 1;
struct esp_bt_controller_config_t {};
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() esp_bt_controller_config_t{}
inline int esp_bt_controller_get_status()
{
	return ESP_BT_CONTROLLER_STATUS_IDLE;
}

inline int esp_bt_controller_init(esp_bt_controller_config_t *)
{
	return ESP_OK;
}

inline int esp_bt_controller_enable(int)
{
	return ESP_OK;
}

inline int esp_bt_controller_disable()
{
	return ESP_OK;
}

inline int esp_bt_controller_deinit()
{
	return ESP_OK;
}

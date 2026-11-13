// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include <cstdint>
constexpr int ESP_OK = 0, ESP_ERR_NO_MEM = 0x101, ESP_ERR_INVALID_STATE = 0x103, ESP_BT_STATUS_SUCCESS = 0;
constexpr int BLE_ADDR_TYPE_PUBLIC = 0, BLE_SCAN_FILTER_ALLOW_ALL = 0, BLE_SCAN_DUPLICATE_DISABLE = 0;
constexpr int ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK = 1, BLE_SCAN_TYPE_PASSIVE = 0, BLE_SCAN_TYPE_ACTIVE = 1;
constexpr int ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY = 16, ESP_BLE_GAP_EXT_ADV_DATA_COMPLETE = 0;
enum esp_gap_ble_cb_event_t {ESP_GAP_BLE_EXT_ADV_REPORT_EVT, ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT,
	                         ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT, ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT};
struct esp_ble_gap_ext_adv_report_t {uint8_t addr[6]{}, addr_type{}, rssi{}, sid{}, event_type{}, data_status{},
	                                     adv_data_len{},
	                                     primary_phy{}, secondly_phy{}, tx_power{}, dir_addr_type{}, dir_addr[6]{},
	                                     adv_data[251]{}; uint16_t per_adv_interval{};};
struct esp_gap_ble_cb_param_t {
	struct {esp_ble_gap_ext_adv_report_t params;} ext_adv_report;
	struct {int status{};} set_ext_scan_params, ext_scan_start, ext_scan_stop;
};
using esp_ble_gap_cb_param_t = esp_gap_ble_cb_param_t;
struct esp_ble_ext_scan_params_t {
	int own_addr_type{}, filter_policy{}, scan_duplicate{}, cfg_mask{};
	struct {int scan_type{}, scan_interval{}, scan_window{};} uncoded_cfg, coded_cfg;
};
using esp_gap_ble_cb_t = void (*)(esp_gap_ble_cb_event_t, esp_gap_ble_cb_param_t *);
int esp_ble_gap_register_callback(esp_gap_ble_cb_t);
int esp_ble_gap_set_ext_scan_params(const esp_ble_ext_scan_params_t *);
int esp_ble_gap_start_ext_scan(uint32_t, uint16_t);
int esp_ble_gap_stop_ext_scan();

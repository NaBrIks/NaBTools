// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools_espidf.hpp"
#if NABTOOLS_GATT_CLIENT
#include "gatt_client.hpp"
#endif
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstring>

namespace NaBTools::runtime
{
	EspIdfBackend::EspIdfBackend(EspIdfScanTiming timing)
	  : scan_timing_(timing)
	{
	}

	IGattClient * EspIdfBackend::gatt()
	{
#if NABTOOLS_GATT_CLIENT
		return gatt_.get();
#else
		return nullptr;
#endif
	}

	EspIdfBackend *EspIdfBackend::active_{};
	portMUX_TYPE EspIdfBackend::callback_lock_ = portMUX_INITIALIZER_UNLOCKED;
	bool EspIdfBackend::check(int status)
	{
		if(status == ESP_OK) return true;

		error_ = status; state_ = ScanState::fault; return false;
	}

	EspIdfBackend::~EspIdfBackend()
	{
#if NABTOOLS_GATT_CLIENT
		gatt_.reset();
#endif
		// Serialize pointer invalidation with the entire bounded callback body.
		portENTER_CRITICAL(&callback_lock_);
		if(active_ == this) active_ = nullptr;

		portEXIT_CRITICAL(&callback_lock_);
		if(owned_host_)
		{
			if(esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) esp_bluedroid_disable();

			if(esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) esp_bluedroid_deinit();
		}

		if(owned_controller_)
		{
			if(esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();

			if(esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) esp_bt_controller_deinit();
		}
	}

	Status EspIdfBackend::start()
	{
		if(state_ != ScanState::idle) return Status::busy;

		if(!scan_timing_.valid()) return Status::invalid;

		portENTER_CRITICAL(&callback_lock_);
		const bool busy = active_ && active_ != this;
		if(!busy) active_ = this;

		portEXIT_CRITICAL(&callback_lock_);
		if(busy) return Status::busy;

		// Refuse to adopt a stack initialized by another subsystem.
		if(esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE ||
		    esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED)
		{
			error_ = ESP_ERR_INVALID_STATE; state_ = ScanState::fault; return Status::busy;
		}

		queue_ = xQueueCreateStatic(queue_capacity, sizeof(Advertisement), queue_bytes_, &queue_control_);
		if(!queue_){check(ESP_ERR_NO_MEM); return Status::backend_error;}

		// The ESP32-C6 SDK macro omits two CCA fields. C++ zero-initializes them.
		// https://github.com/espressif/esp-idf/blob/v6.1/components/bt/include/esp32c6/include/esp_bt.h
#if defined(CONFIG_IDF_TARGET_ESP32C6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
		esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if defined(CONFIG_IDF_TARGET_ESP32C6)
#pragma GCC diagnostic pop
#endif
		if(!check(esp_bt_controller_init(&cfg))) return Status::backend_error;

		owned_controller_ = true;
		if(!check(esp_bt_controller_enable(ESP_BT_MODE_BLE))) return Status::backend_error;

		if(!check(esp_bluedroid_init())) return Status::backend_error;

		owned_host_ = true;
		if(!check(esp_bluedroid_enable()) || !check(esp_ble_gap_register_callback(gap))) return Status::backend_error;

#if NABTOOLS_GATT_CLIENT
		gatt_ = std::make_unique<EspIdfGattClient>();
		if(gatt_->start() != Status::ok) return Status::backend_error;

#endif
		return configure_scan();
	}

	Status EspIdfBackend::configure_scan()
	{
		// Keep the parameter storage alive through asynchronous completion. Configure
		// only after STOP_COMPLETE, then wait for SET_EXT_SCAN_PARAMS_COMPLETE before
		// resuming the application's scan intent. Active scanning is not a connection.
		// https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/bluetooth/esp_gap_ble.html#_CPPv431esp_ble_gap_set_ext_scan_paramsPK25esp_ble_ext_scan_params_t
		parameters_ = {}; parameters_.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
		parameters_.filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
		parameters_.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
		parameters_.cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK;
		parameters_.uncoded_cfg.scan_type = desired_mode_ ==
		    ScanMode::active ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE;
		parameters_.uncoded_cfg.scan_interval = scan_timing_.interval;
		parameters_.uncoded_cfg.scan_window = scan_timing_.window;
		configuring_timing_ = scan_timing_;
		configuring_mode_ = desired_mode_; parameters_ready_ = false;
		state_ = ScanState::starting;
		return check(esp_ble_gap_set_ext_scan_params(&parameters_)) ? Status::ok : Status::backend_error;
	}

	void EspIdfBackend::report(const esp_ble_gap_ext_adv_report_t &r)
	{
		++reports_;
		if(r.adv_data_len > Advertisement::capacity){++dropped_; return;}

		Advertisement out; std::copy_n(r.addr, 6, out.address.bytes.begin()); out.address.type = r.addr_type;
		out.time_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000); out.rssi = r.rssi; out.sid = r.sid;
		out.legacy = (r.event_type & ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY) != 0;
		out.complete = r.data_status == ESP_BLE_GAP_EXT_ADV_DATA_COMPLETE; out.size = r.adv_data_len;
		// Copy controller facts along with payload bytes before leaving GAP callback.
		// https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/bluetooth/esp_gap_ble.html#_CPPv428esp_ble_gap_ext_adv_report_t
		RadioInfo radio; radio.event_properties = r.event_type;
		radio.primary_phy = r.primary_phy;
#if CONFIG_BT_BLE_FEAT_PAWR_EN
		radio.secondary_phy = r.secondary_phy;
#else
		radio.secondary_phy = r.secondly_phy;
#endif
		radio.tx_power = static_cast<int8_t>(r.tx_power); radio.periodic_interval = r.per_adv_interval;
		radio.directed_address.type = r.dir_addr_type; std::copy_n(r.dir_addr, 6, radio.directed_address.bytes.begin());
		radio.data_status = r.data_status; out.radio = radio;
		std::copy_n(r.adv_data, out.size, out.data.begin());
		if(xQueueSend(queue_, &out, 0) != pdTRUE) ++dropped_;
	}

	void EspIdfBackend::gap(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
	{
		portENTER_CRITICAL(&callback_lock_);
		auto *self = active_;
		if(self && p)
		{
			switch(event)
			{
			case ESP_GAP_BLE_EXT_ADV_REPORT_EVT: self->report(p->ext_adv_report.params); break;
			case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT:
				if(p->set_ext_scan_params.status == ESP_BT_STATUS_SUCCESS)
				{
					self->configured_timing_ = self->configuring_timing_.load();
					self->configured_mode_ = self->configuring_mode_.load(); self->parameters_ready_ = true;
					self->state_ = ScanState::stopped;
				}
				else
				{
					self->error_ = p->set_ext_scan_params.status; self->state_ = ScanState::fault;
				}

				break;
			case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:
				if(p->ext_scan_start.status == ESP_BT_STATUS_SUCCESS) self->state_ = ScanState::scanning;
				else
				{
					self->error_ = p->ext_scan_start.status; self->state_ = ScanState::fault;
				}

				break;
			case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:
				if(p->ext_scan_stop.status == ESP_BT_STATUS_SUCCESS) self->state_ = ScanState::stopped;
				else
				{
					self->error_ = p->ext_scan_stop.status; self->state_ = ScanState::fault;
				}

				break;
			default: break;
			}
		}

		portEXIT_CRITICAL(&callback_lock_);
	}

	void EspIdfBackend::pump()
	{
#if NABTOOLS_GATT_CLIENT
		// Wait for a successful GAP EXT_SCAN_STOP_COMPLETE before initiating OPEN;
		// a successful stop API return only means that the request was accepted.
		if(gatt_)
			gatt_->pump(state_.load() == ScanState::stopped && parameters_ready_ &&
			    configured_mode_.load() == desired_mode_ && configured_timing_.load() == scan_timing_);

#endif
		if(!parameters_ready_) return;

		const auto actual = state_.load();
		if(configured_mode_.load() != desired_mode_ || configured_timing_.load() != scan_timing_)
		{
			if(actual == ScanState::scanning){state_ = ScanState::stopping; check(esp_ble_gap_stop_ext_scan());}
			else if(actual == ScanState::stopped)
			{
#if NABTOOLS_GATT_CLIENT
				if(gatt_ && gatt_->opening()) return;

#endif
				configure_scan();
			}

			return;
		}

		bool effective = desired_;
#if NABTOOLS_GATT_CLIENT
		// Espressif recommends stop scan -> connect -> resume scan. Its BLE 5.0
		// client example uses the same sequence:
		// https://github.com/espressif/esp-idf/issues/4562
		// https://github.com/espressif/esp-idf/blob/v5.4/examples/bluetooth/bluedroid/ble_50/ble50_security_client/main/ble50_sec_gattc_demo.c
		// On our C6/IDF 5.4, OPEN failed with generic GATT status 0x85 during scan
		// and succeeded after scan stopped. This does not identify the underlying
		// HCI failure. Bluetooth permits optional state combinations; do not turn
		// this backend policy into a profile-specific or universal Bluetooth rule:
		// https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/low-energy-controller/link-layer-specification.html
		// Preserve desired_: scan resumes after initiation only if requested by
		// the application, and stays available during established GATT sessions.
		if(gatt_ && gatt_->opening()) effective = false;

#endif
		if(effective && actual == ScanState::stopped)
		{
			state_ = ScanState::starting; check(esp_ble_gap_start_ext_scan(0, 0));
		}
		else if(!effective && actual == ScanState::scanning)
		{
			state_ = ScanState::stopping; check(esp_ble_gap_stop_ext_scan());
		}
	}

	Status EspIdfBackend::scan(bool enabled)
	{
		if(state_ == ScanState::fault || state_ == ScanState::idle)
			return Status::invalid;

		desired_ = enabled; return Status::ok;
	}

	Status EspIdfBackend::set_scan_mode(ScanMode mode)
	{
		if((mode != ScanMode::passive && mode != ScanMode::active) || state_ == ScanState::fault)
			return Status::invalid;

		desired_mode_ = mode; return Status::ok;
	}

	Status EspIdfBackend::set_scan_timing(EspIdfScanTiming timing)
	{
		if(!timing.valid() || state_ == ScanState::fault) return Status::invalid;

		scan_timing_ = timing; return Status::ok;
	}

	bool EspIdfBackend::take(Advertisement &out)
	{
		return queue_ && xQueueReceive(queue_, &out, 0) == pdTRUE;
	}

	BackendSnapshot EspIdfBackend::snapshot()const
	{
		return {state_.load(), error_.load(), reports_.load(), dropped_.load(),
		        configured_mode_.load(), desired_mode_,
		        !parameters_ready_ || configured_mode_.load() != desired_mode_ ||
		        configured_timing_.load() != scan_timing_, true};
	}

}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
// Compile the real backend against a minimal event-controlled SDK test double.
// No COM port, radio, GATT, network or SDK task is used by this test.
#include "NaBTools_espidf.hpp"
#include <stdexcept>
#include <iostream>
using namespace NaBTools::runtime;
esp_gap_ble_cb_t callback{}; unsigned configurations{}, starts{}, stops{}; int configure_result{};
const esp_ble_ext_scan_params_t *pending{};
int esp_ble_gap_register_callback(esp_gap_ble_cb_t c)
{
	callback = c; return 0;
}

int esp_ble_gap_set_ext_scan_params(const esp_ble_ext_scan_params_t *p)
{
	pending = p; ++configurations; return configure_result;
}

int esp_ble_gap_start_ext_scan(uint32_t, uint16_t)
{
	++starts; return 0;
}

int esp_ble_gap_stop_ext_scan()
{
	++stops; return 0;
}

void check(bool v, const char *text)
{
	if(!v)
		throw std::runtime_error(text);
}

void complete(esp_gap_ble_cb_event_t event, int status = 0)
{
	esp_gap_ble_cb_param_t p;
	p.set_ext_scan_params.status = status; p.ext_scan_start.status = status; p.ext_scan_stop.status = status;
	callback(event, &p);
}

int main()
{
	try{
		{EspIdfBackend backend; check(backend.start() == Status::ok, "start accepted");
		 check(configurations == 1 && pending->uncoded_cfg.scan_type == BLE_SCAN_TYPE_PASSIVE,
			 "passive default persistent parameters");
		 backend.pump(); check(starts == 0, "wait for parameter completion");
		 complete(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT);
		 backend.pump(); backend.pump(); check(starts == 1, "one asynchronous start");
		 complete(ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT);
		 check(backend.set_scan_mode(ScanMode::active) == Status::ok, "active requested"); backend.pump();
		 backend.pump();
		 check(stops == 1 && configurations == 1 && backend.snapshot().scan_mode == ScanMode::passive,
			 "stop acknowledgement required");
		 complete(ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT); backend.pump();
		 check(configurations == 2 && pending->uncoded_cfg.scan_type == BLE_SCAN_TYPE_ACTIVE,
			 "configure only after stopped");
		 check(backend.snapshot().scan_mode_pending && backend.snapshot().scan_mode == ScanMode::passive,
			 "accepted is not applied");
		 backend.scan(false); complete(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT); backend.pump();
		 check(starts == 1 && backend.snapshot().scan_mode == ScanMode::active && !backend.snapshot().scan_mode_pending,
			 "paused intent preserved");
		 backend.set_scan_mode(ScanMode::passive); backend.pump();
		 complete(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT); backend.pump();
		 check(starts == 1, "changing mode while paused does not start scan");
		 backend.scan(true); backend.pump(); complete(ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT);
		 backend.set_scan_mode(ScanMode::active); backend.pump(); complete(ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT);
		 configure_result = 0x105; backend.pump();
		 check(backend.snapshot().state == ScanState::fault && backend.snapshot().error == 0x105,
			 "SDK failure surfaced");}
		configure_result = 0; {EspIdfBackend configured;
			                   check(configured.set_scan_mode(ScanMode::active) == Status::ok &&
			                       configured.start() == Status::ok, "mode can be selected before start");
			                   check(pending->uncoded_cfg.scan_type == BLE_SCAN_TYPE_ACTIVE,
			                       "initial active selection");
			                   check(configured.set_scan_mode(static_cast<ScanMode>(9)) == Status::invalid,
			                       "invalid mode rejected");}
		const auto before_invalid = configurations;
		{EspIdfBackend invalid({3, 3});
		 check(invalid.start() == Status::invalid && configurations == before_invalid,
			 "invalid constructor timing rejected before SDK access");}
		{EspIdfBackend backend({0x100, 0x80});
		 check(backend.scan_timing() == EspIdfScanTiming{0, 0}, "unconfigured is not reported applied");
		 check(backend.start() == Status::ok && pending->uncoded_cfg.scan_interval == 0x100 &&
			 pending->uncoded_cfg.scan_window == 0x80, "constructor timing reaches SDK");
		 check(backend.scan_timing_pending(), "initial timing pending");
		 complete(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT);
		 check(backend.scan_timing() == EspIdfScanTiming{0x100, 0x80} && !backend.scan_timing_pending(),
			 "callback commits timing");
		 backend.pump(); complete(ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT);
		 const auto configured_before = configurations, started_before = starts, stopped_before = stops;
		 check(backend.set_scan_timing({0x200, 0x100}) == Status::ok, "timing request accepted");
		 check(backend.scan_timing() == EspIdfScanTiming{0x100,
			                                             0x80} && backend.scan_timing_pending() &&
			 backend.snapshot().scan_mode_pending, "actual timing unchanged before completion");
		 backend.pump(); backend.pump();
		 check(stops == stopped_before + 1 && configurations == configured_before, "timing waits for stop callback");
		 check(backend.set_scan_timing({0x300, 0x180}) == Status::ok, "latest timing wins before configuration");
		 complete(ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT); backend.pump();
		 check(pending->uncoded_cfg.scan_interval == 0x300 && pending->uncoded_cfg.scan_window == 0x180 &&
			 configurations == configured_before + 1, "latest timing configured once");
		 backend.pump(); check(starts == started_before, "no restart before parameter callback");
		 complete(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT); backend.pump();
		 complete(ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT);
		 check(backend.scan_timing() == EspIdfScanTiming{0x300, 0x180} && starts == started_before + 1,
			 "scan resumes with confirmed timing");
		 backend.scan(false); backend.pump(); complete(ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT);
		 check(backend.set_scan_timing({4, 4}) == Status::ok, "minimum continuous timing accepted"); backend.pump();
		 check(backend.set_scan_mode(ScanMode::active) == Status::ok && backend.set_scan_timing({0x4000,
			                                                                                     0x4000}) == Status::ok,
			 "mode and timing can change while configuration is pending");
		 complete(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT); backend.pump();
		 check(pending->uncoded_cfg.scan_type == BLE_SCAN_TYPE_ACTIVE && pending->uncoded_cfg.scan_interval == 0x4000,
			 "pending changes coalesce into next configuration");
		 complete(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT); backend.pump();
		 check(starts == started_before + 1 && backend.scan_timing() == EspIdfScanTiming{0x4000, 0x4000},
			 "pause preserved after timing changes");
		 for(const auto bad:{EspIdfScanTiming{0, 0}, EspIdfScanTiming{4, 3}, EspIdfScanTiming{8, 9},
			                 EspIdfScanTiming{0x4001, 4}})
			 check(backend.set_scan_timing(bad) == Status::invalid, "invalid range rejected");
		 check(backend.requested_scan_timing() == EspIdfScanTiming{0x4000, 0x4000},
			 "invalid requests do not change desired timing");
		 backend.set_scan_timing({8, 4}); backend.pump(); complete(ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT, 0x106);
		 check(backend.snapshot().state == ScanState::fault && backend.scan_timing() == EspIdfScanTiming{0x4000,
			                                                                                             0x4000},
			 "failed callback does not publish requested timing");}
		std::cout << "PASS real ESP-IDF backend asynchronous scan mode and timing\n"; return 0;
	}catch(const std::exception &e){std::cerr << e.what() << '\n'; return 1;}
}

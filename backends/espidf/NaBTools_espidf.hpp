// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/engine.hpp"
#include "NaBTools/core/config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_gap_ble_api.h"
#include <type_traits>
static_assert(std::is_trivially_copyable_v<NaBTools::runtime::Advertisement>,
    "FreeRTOS queue copies packet storage by value");
#include <atomic>

namespace NaBTools::runtime
{
	class EspIdfGattClient;
// GAP scan timing units are 0.625 ms; equal window/interval requests
// continuous scanning. RF coexistence can still preempt the scanner.
	struct EspIdfScanTiming {
		uint16_t interval{0x80}, window{0x30};
		constexpr bool valid() const
		{
			return interval >= 4 && interval <= 0x4000 && window >= 4 && window <= interval;
		}

		bool operator==(const EspIdfScanTiming &) const = default;
	};
// One owner of GAP scanning, passive by default. Active scan requests extra
// advertising data; it does not connect or pair. GATT remains an optional client.
	class EspIdfBackend final : public IBackend
	{
#if NABTOOLS_GATT_CLIENT
		std::unique_ptr<EspIdfGattClient> gatt_;
#endif
		static constexpr size_t queue_capacity = 32;
		StaticQueue_t queue_control_{};
		alignas(4) uint8_t queue_bytes_[queue_capacity * sizeof(Advertisement)]{};
		QueueHandle_t queue_{};
		std::atomic<ScanState> state_{ScanState::idle};
		std::atomic<int> error_{0};
		std::atomic<uint32_t> reports_{0}, dropped_{0};
		std::atomic<bool> parameters_ready_{false};
		std::atomic<ScanMode> configured_mode_{ScanMode::passive}, configuring_mode_{ScanMode::passive};
		ScanMode desired_mode_{ScanMode::passive};
		esp_ble_ext_scan_params_t parameters_{};
		EspIdfScanTiming scan_timing_;
		std::atomic<EspIdfScanTiming> configured_timing_{EspIdfScanTiming{0, 0}}, configuring_timing_{};
		bool desired_{true}, owned_controller_{}, owned_host_{};
		static EspIdfBackend *active_;
		static portMUX_TYPE callback_lock_;
		static void gap(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t *);
		void report(const esp_ble_gap_ext_adv_report_t &);
		bool check(int);
		Status configure_scan();
	public:
		explicit EspIdfBackend(EspIdfScanTiming timing = {});
		~EspIdfBackend()override;
		Status start()override;
		void pump()override;
		Status scan(bool enabled)override;
		Status set_scan_mode(ScanMode)override;
		Status set_scan_timing(EspIdfScanTiming);
		// Applied timing is {0,0} until the first successful parameter completion.
		EspIdfScanTiming scan_timing() const
		{
			return configured_timing_.load();
		}

		EspIdfScanTiming requested_scan_timing() const
		{
			return scan_timing_;
		}

		bool scan_timing_pending() const
		{
			return !parameters_ready_ || configured_timing_.load() != scan_timing_;
		}

		bool take(Advertisement &out)override;
		BackendSnapshot snapshot()const override;
		IGattClient * gatt()override;
	};
}

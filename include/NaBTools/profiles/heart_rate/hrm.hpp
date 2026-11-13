// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/core/config.hpp"
#include "NaBTools/core/action.hpp"
#include "NaBTools/bt/gatt/client.hpp"

namespace NaBTools::runtime
{
// Borrowed characteristic 0x2A37 value, never an advertising payload.
	class HeartRateMeasurement
	{
		Bytes bytes_;
		size_t rr_offset_{};
		HeartRateMeasurement(Bytes bytes, size_t rr_offset)
		  : bytes_(bytes), rr_offset_(rr_offset)
		{
		}

	public:
		static std::optional<HeartRateMeasurement> parse(Bytes);
		Bytes raw()const
		{
			return bytes_;
		}

		uint8_t flags()const
		{
			return bytes_[0];
		}

		uint8_t reserved_flags()const
		{
			return flags() & 0xe0;
		}

		bool wide_bpm()const
		{
			return flags() & 1;
		}

		uint16_t bpm()const;
		bool contact_supported()const
		{
			return flags() & 4;
		}

		std::optional<bool> contact_detected()const
		{
			return contact_supported() ? std::optional<bool>(bool(flags() & 2)) : std::nullopt;
		}

		std::optional<uint16_t> energy_kj()const;
		bool rr_present()const
		{
			return flags() & 0x10;
		}

		size_t rr_count()const
		{
			return rr_present() ? (bytes_.size() - rr_offset_) / 2 : 0;
		}

		// Original units: 1/1024 second. No rounding to milliseconds.
		std::optional<uint16_t> rr_1_1024_s(size_t index)const;
		Bytes unknown_tail()const
		{
			return rr_present() ? Bytes{} : bytes_.subspan(rr_offset_);
		}

		bool standard_layout()const
		{
			return !reserved_flags() && unknown_tail().empty();
		}

	};
	class HeartRateRecognizer
	{
	public:
		static bool matches(AdvertisementView);
		static std::optional<Detection> recognize(AdvertisementView);
	};
	enum class HeartRateState {unavailable, disconnected, connecting, connected, discovering, subscribing, subscribed,
		                       disconnecting};
	const char *name(HeartRateState);
#if NABTOOLS_HRM_STRUCT
	struct HeartRateAction {void *context{};
		                    Delivery (*invoke)(void *, const MessageInfo &, const HeartRateMeasurement &)
							{
							};};
	struct HeartRateStateInfo {HeartRateState state; const char *event; Status status; int error; uint64_t request_id;
		                       GattSessionId session;};
	struct HeartRateStateAction {void *context{};
		                         Delivery (*invoke)(void *, const MessageInfo &, const HeartRateStateInfo &)
								 {
								 };};
#endif
	struct HeartRateActions {
#if NABTOOLS_HRM_STRUCT
		HeartRateAction measurement;
		HeartRateStateAction state;
#endif
#if NABTOOLS_HRM_CBOR
		CborAction cbor;
#endif
	};
	class HeartRateDevice final : public IDevice
	{
		Address address_;
		HeartRateActions actions_;
		uint64_t last_seen_{}, measurements_{};
		uint32_t packets_{}, invalid_{}, delivered_{}, dropped_{};
		IGattClient *gatt_{};
		DeviceId owner_;
		GattSessionId session_;
		HeartRateState state_{HeartRateState::unavailable};
		uint64_t state_events_{}, flow_request_{}, subscribe_request_{};
		uint16_t service_handle_{}, service_end_{}, measurement_handle_{}, measurement_properties_{}, cccd_handle_{},
		    energy_handle_{};
		mutable std::array<OperationInfo, 4> commands_{};
		mutable std::array<OperationInfo, 2> events_{};
		Status process_measurement(BtEngine &, DeviceId, Bytes, uint64_t time_ms);
		void emit_state(BtEngine &, DeviceId, const char *event, Status, int error, uint64_t request, uint64_t time_ms);
		void clear_handles();
		Status subscribe(uint64_t request);
	public:
		// Local bound for future notification adapters; no negotiated ATT_MTU claim.
		static constexpr size_t max_measurement_size = 512;
		HeartRateDevice(Address address, HeartRateActions actions = {})
		  : address_(address), actions_(actions)
		{
		}

		DeviceInfo info()const override;
		InteractionInfo interaction()const override;
		void on_attached(BtEngine &, DeviceId)override;
		void on_gatt(BtEngine &, DeviceId, const GattEvent &)override;
		Status invoke(BtEngine &, DeviceId, const DeviceCommand &)override;
		void on_advertisement(BtEngine &, DeviceId, AdvertisementView)override;
		// Engine context only; enters protected engine dispatch, borrowed until return.
		// The future GATT adapter must
		// validate connection + 0x180D/0x2A37 handles before calling this method.
		// Every valid notification is delivered, including identical values.
		Status on_measurement(BtEngine &, DeviceId, Bytes, uint64_t time_ms);
		uint64_t measurements()const
		{
			return measurements_;
		}

		uint32_t invalid_measurements()const
		{
			return invalid_;
		}

		HeartRateState state()const
		{
			return state_;
		}

		GattSessionId session()const
		{
			return session_;
		}

	};
	class HeartRateModule final : public IModule
	{
		HeartRateActions actions_;
	public:
		explicit HeartRateModule(HeartRateActions actions = {})
		  : actions_(actions)
		{
		}

		const char * key()const override
		{
			return "nabtools.bluetooth.hrm";
		}

		void observe(BtEngine &, AdvertisementView, AdStatus)override
		{
		}                                                        // Explicit admission.

		std::optional<Detection> recognize(AdvertisementView v)const override
		{
			return HeartRateRecognizer::recognize(v);
		}

		std::unique_ptr<IDevice> create(AdvertisementView)const override;
		DeviceId add_known(BtEngine &, Address)const;
	};
}

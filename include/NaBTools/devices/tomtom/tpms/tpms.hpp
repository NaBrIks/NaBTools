// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/core/config.hpp"
#include "NaBTools/bt/engine.hpp"
#include "NaBTools/core/action.hpp"
#include "NaBTools/core/units.hpp"

namespace NaBTools::runtime
{
#if NABTOOLS_TPMS_STRUCT
	struct TpmsReading {
		uint8_t sensor_number{}, battery_percentage{}, alarm{};
		uint32_t sensor_address{}, pressure_pa{};
		int32_t temperature_centi_c{};
		Pressure pressure()const
		{
			return {double(pressure_pa)};
		}

		Temperature temperature()const
		{
			return Temperature::from_centi_c(temperature_centi_c);
		}

	};
	struct TpmsAction {void *context{}; Delivery (*invoke)(void *, const MessageInfo &, const TpmsReading &)
					   {
					   };};
#endif
	struct TpmsActions {
#if NABTOOLS_TPMS_STRUCT
		TpmsAction reading;
#endif
#if NABTOOLS_TPMS_CBOR
		CborAction cbor;
#endif
	};
// A non-owning, validated view of TomTom's manufacturer payload.
	class TomTomPacket
	{
		Bytes bytes_;
		explicit TomTomPacket(Bytes bytes)
		  : bytes_(bytes)
		{
		}

	public:
		static std::optional<TomTomPacket> parse(AdvertisementView);
		uint8_t sensor_number()const
		{
			return bytes_[2] - 0x80;
		}

		uint32_t sensor_address()const;
		uint32_t pressure_pa()const;
		int32_t temperature_centi_c()const;
		uint8_t battery()const
		{
			return bytes_[16];
		}

		uint8_t alarm()const
		{
			return bytes_[17];
		}

		Bytes raw()const
		{
			return bytes_;
		}

	};
	class TomTomDevice final : public IDevice
	{
		Address address_;
		TpmsActions actions_;
		std::array<uint8_t, 18> latest_{};
		uint64_t last_seen_{};
		uint32_t packets_{}, delivered_{}, dropped_{};
		void account(Delivery);
	public:
		TomTomDevice(Address address, TpmsActions actions)
		  : address_(address), actions_(actions)
		{
		}

		DeviceInfo info()const override;
		void on_advertisement(BtEngine &, DeviceId, AdvertisementView)override;
		Bytes latest_raw()const
		{
			return packets_ ? Bytes(latest_) : Bytes{};
		}

	};
// Auto-discovery is an explicit MODULE option, never a core lifetime policy.
	class TomTomModule final : public IModule
	{
		bool discover_;
		TpmsActions actions_;
		uint32_t candidates_{}, rejected_{};
	public:
		TomTomModule(bool discover, TpmsActions actions)
		  : discover_(discover), actions_(actions)
		{
		}

		const char * key()const override
		{
			return "nabtools.tomtom";
		}

		std::optional<Detection> recognize(AdvertisementView)const override;
		std::unique_ptr<IDevice> create(AdvertisementView)const override;
		DeviceId add_known(BtEngine &, Address)const;
		void observe(BtEngine &, AdvertisementView, AdStatus)override;
		uint32_t candidates()const
		{
			return candidates_;
		}

		uint32_t rejected()const
		{
			return rejected_;
		}

	};
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/devices/tomtom/tpms/tpms.hpp"
#if NABTOOLS_TPMS_CBOR
#include "NaBTools/core/cbor.hpp"
#endif
#include <algorithm>
#include <bit>

namespace NaBTools::runtime
{
	namespace
	{uint32_t le32(Bytes b, size_t o)
	 {
		 return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) | (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
	 }

	}
	std::optional<TomTomPacket> TomTomPacket::parse(AdvertisementView v)
	{
		if(validate(v) != AdStatus::complete) return std::nullopt;

		AdCursor cursor(v.payload);
		while(auto f = cursor.next())
			if(f->type == 0xff && f->value.size() == 18)
			{
				auto r = f->value;
				if(r[0] == 0 && r[1] == 1 && r[2] >= 0x80 && r[3] == 0xea && r[4] == 0xca)
					return TomTomPacket(r);
			}

		return std::nullopt;
	}

	uint32_t TomTomPacket::sensor_address()const
	{
		return uint32_t(bytes_[5]) | (uint32_t(bytes_[6]) << 8) | (uint32_t(bytes_[7]) << 16);
	}

	uint32_t TomTomPacket::pressure_pa()const
	{
		return le32(bytes_, 8);
	}

	int32_t TomTomPacket::temperature_centi_c()const
	{
		return std::bit_cast<int32_t>(le32(bytes_, 12));
	}

	DeviceInfo TomTomDevice::info()const
	{
		return {{}, address_, "TomTom TPMS", last_seen_, packets_, delivered_, dropped_};
	}

	void TomTomDevice::account(Delivery d)
	{
		if(d == Delivery::accepted)
			++delivered_;
		else
			++dropped_;
	}

	void TomTomDevice::on_advertisement(BtEngine &engine, DeviceId id, AdvertisementView view)
	{
		if(!(view.address == address_)) return;

		const auto packet = TomTomPacket::parse(view); if(!packet) return;

		std::copy(packet->raw().begin(), packet->raw().end(), latest_.begin());
		last_seen_ = view.time_ms; ++packets_;
		const MessageInfo meta{id, view.time_ms, packets_, "nabtools.tpms.v1"};
#if NABTOOLS_TPMS_STRUCT
		if(actions_.reading.invoke)
		{
			const TpmsReading reading{packet->sensor_number(), packet->battery(), packet->alarm(),
				                      packet->sensor_address(), packet->pressure_pa(), packet->temperature_centi_c()};
			account(actions_.reading.invoke(actions_.reading.context, meta, reading));
		}

#endif
#if NABTOOLS_TPMS_CBOR
		if(!engine.device(id)) return; // a typed Action may have requested removal

		if(actions_.cbor.invoke)
		{
			std::array<uint8_t, 320> buffer{}; CborWriter w(buffer);
			w.map(11);
			w.text("schema"); w.text(meta.schema);
			w.text("device_address"); w.text(view.address.text());
			w.text("address_type"); w.number(view.address.type);
			w.text("observed_ms"); w.number(view.time_ms);
			w.text("sequence"); w.number(meta.sequence);
			w.text("sensor_number"); w.number(packet->sensor_number());
			w.text("sensor_address"); w.number(packet->sensor_address());
			w.text("pressure_pa"); w.number(packet->pressure_pa());
			w.text("temperature_centi_c"); w.integer(packet->temperature_centi_c());
			w.text("battery_percentage"); w.number(packet->battery());
			w.text("alarm_flag"); w.number(packet->alarm());
			account(w.good() ? actions_.cbor.invoke(actions_.cbor.context, meta, w.result()) : Delivery::error);
		}

#endif
	}

	DeviceId TomTomModule::add_known(BtEngine &e, Address address)const
	{
		if(auto existing = e.find(address, "TomTom TPMS"); existing.valid()) return existing;

		return e.add_device(std::make_unique<TomTomDevice>(address, actions_));
	}

	std::optional<Detection> TomTomModule::recognize(AdvertisementView view)const
	{
		if(!TomTomPacket::parse(view)) return {};

		return Detection{"TomTom TPMS", "manufacturer 0x0100 + TomTom packet signature", true,
		                 DeviceKind::tire_pressure_sensor};
	}

	std::unique_ptr<IDevice> TomTomModule::create(AdvertisementView view)const
	{
		if(!recognize(view)) return {};

		return std::make_unique<TomTomDevice>(view.address, actions_);
	}

	void TomTomModule::observe(BtEngine &e, AdvertisementView view, AdStatus validity)
	{
		if(validity != AdStatus::complete || !TomTomPacket::parse(view)) return;

		++candidates_;
		if(discover_ && !add_known(e, view.address).valid()) ++rejected_;
	}

}

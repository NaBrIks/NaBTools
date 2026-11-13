// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/devices/samsung/smarttag/smarttag.hpp"
#if NABTOOLS_SMARTTAG_CBOR
#include "NaBTools/core/cbor.hpp"
#endif
#include <algorithm>

namespace NaBTools::runtime
{
	std::optional<SmartTagPacket> SmartTagPacket::parse(AdvertisementView view)
	{
		if(validate(view) != AdStatus::complete) return {};

		AdCursor cursor(view.payload);
		while(auto f = cursor.next())
			if(f->type == 0x16 && f->value.size() >= 3 && f->value[0] == 0x5a && f->value[1] == 0xfd)
				return SmartTagPacket(f->value.subspan(2));

		return {};
	}

	uint32_t SmartTagPacket::aging_counter()const
	{
		return decoded() ? uint32_t(bytes_[1]) | (uint32_t(bytes_[2]) << 8) | (uint32_t(bytes_[3]) << 16) : 0;
	}

	const char * smarttag_state_name(uint8_t state)
	{
		switch(state)
		{
		case 1: return "premature_lost"; case 2: return "lost"; case 3: return "overmature_lost";
		case 4: return "paired_one"; case 5: return "connected_one"; case 6: return "connected_two";
		default: return "unknown";
		}
	}

	std::optional<Detection> SmartTagRecognizer::recognize(AdvertisementView v)
	{
		auto packet = SmartTagPacket::parse(v); if(!packet) return {};

		return Detection{"Samsung SmartTag",
		                 packet->decoded() ? "FD5A service data, version 1, 20 bytes" :
		                 "FD5A service data, unknown format", packet->decoded(), DeviceKind::tag};
	}

	DeviceInfo SmartTagDevice::info()const
	{
		return {{}, address_, "Samsung SmartTag", last_seen_, packets_, delivered_, dropped_};
	}

	void SmartTagDevice::on_advertisement(BtEngine &engine, DeviceId id, AdvertisementView view)
	{
		if(!(view.address == address_)) return;

		auto packet = SmartTagPacket::parse(view); if(!packet) return;

		const auto raw = packet->raw(); const bool first = packets_ == 0;
		std::array<uint8_t, Advertisement::capacity> changed{}; size_t count = 0;
		if(!first)
			for(size_t i = 0; i < std::max(latest_size_, raw.size()); ++i)
				if(i >= latest_size_ || i >= raw.size() || latest_[i] != raw[i])
					changed[count++] = static_cast<uint8_t>(i);

		last_seen_ = view.time_ms; ++packets_;
		std::copy(raw.begin(), raw.end(), latest_.begin()); latest_size_ = raw.size();
		if(!first && !count) return; // RSSI changes and identical adverts are not events

		++changes_; const MessageInfo meta{id, view.time_ms, changes_, "nabtools.samsung.smarttag.advertisement.v1"};
		auto account = [this](Delivery d){
				if(d == Delivery::accepted)
					++delivered_;
				else
					++dropped_;
			};
#if NABTOOLS_SMARTTAG_STRUCT
		if(actions_.reading.invoke)
			account(actions_.reading.invoke(actions_.reading.context, meta,
			    SmartTagReading{*packet, Bytes(changed.data(), count), first}));

#endif
#if NABTOOLS_SMARTTAG_CBOR
		if(!engine.device(id)) return;

		if(actions_.cbor.invoke)
		{
			std::array<uint8_t, 768> buffer; CborWriter w(buffer);
			w.map(packet->decoded() ? 16 : 6);
			w.text("type"); w.text("advertisement"); w.text("change"); w.text(first ? "first" : "payload_changed");
			w.text("decoded"); w.boolean(packet->decoded()); w.text("version"); w.number(packet->version());
			w.text("raw"); w.bytes(raw); w.text("changed_offsets"); w.bytes(Bytes(changed.data(), count));
			if(packet->decoded())
			{
				w.text("advertisement_type"); w.number(packet->advertisement_type());
				w.text("tag_state"); w.number(packet->state());
				w.text("aging_counter"); w.number(packet->aging_counter()); w.text("privacy_id");
				w.bytes(packet->privacy_id());
				w.text("region_id"); w.number(packet->region()); w.text("e2e_enabled"); w.boolean(packet->encrypted());
				w.text("uwb_present"); w.boolean(packet->uwb()); w.text("battery_level");
				w.number(packet->battery_level());
				w.text("reserved"); w.bytes(packet->reserved()); w.text("signature"); w.bytes(packet->signature());
			}

			account(w.good() ? actions_.cbor.invoke(actions_.cbor.context, meta, w.result()) : Delivery::error);
		}

#endif
	}

	std::unique_ptr<IDevice> SmartTagModule::create(AdvertisementView v)const
	{
		if(!recognize(v)) return {};

		return std::make_unique<SmartTagDevice>(v.address, actions_, owner_);
	}

	DeviceId SmartTagModule::add_known(BtEngine &e, Address address)const
	{
		if(auto existing = e.find(address, "Samsung SmartTag"); existing.valid()) return existing;

		return e.add_device(std::make_unique<SmartTagDevice>(address, actions_, owner_));
	}

}

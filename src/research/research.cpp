// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/research/research.hpp"
#include "NaBTools/catalogs/sig/sig.hpp"
#include <algorithm>

namespace NaBTools::runtime
{
	namespace
	{
		void copy_name(Bytes bytes, std::span<char> out)
		{
			size_t i = 0, written = 0;
			while(i < bytes.size() && written + 1 < out.size())
			{
				const uint8_t lead = bytes[i];
				size_t n = lead < 128 ? 1 : (lead >= 0xc2 && lead <= 0xdf) ? 2 : (lead >= 0xe0 &&
				    lead <= 0xef) ? 3 : (lead >= 0xf0 && lead <= 0xf4) ? 4 : 0;
				bool valid = n && n <= bytes.size() - i;
				uint32_t code = n ? lead & (n == 1 ? 0x7f : n == 2 ? 0x1f : n == 3 ? 0xf : 7) : 0;
				for(size_t j = 1; valid && j < n; ++j)
				{
					valid = (bytes[i + j] & 0xc0) == 0x80; code = (code << 6) | (bytes[i + j] & 63);
				}
				if(valid &&
				    ((n == 2 && code < 128) || (n == 3 && code < 2048) || (n == 4 && code < 65536) || code > 0x10ffff ||
				    (code >= 0xd800 && code <= 0xdfff))) valid = false;

				if(!valid || (n == 1 && (lead < 32 || lead == 127))){out[written++] = '?'; ++i; continue;}

				if(n > out.size() - written - 1) break;

				for(size_t j = 0; j < n; ++j)
					out[written++] = static_cast<char>(bytes[i++]);
			}
			out[written] = 0;
		}

	}
	AdvertisementClassification ObservationRow::classification()const
	{
		AdvertisementClassification out;
		// A scan response supplements the same address/type/SID, not another peer.
		// Age each half independently so old scan responses do not live forever.
		if(packet.time_ms >= advertising_time && packet.time_ms - advertising_time <= 30000)
			out.merge(advertising_knowledge);

		if(packet.time_ms >= response_time && packet.time_ms - response_time <= 30000) out.merge(response_knowledge);

		return out;
	}

	void ResearchModule::observe(BtEngine &engine, AdvertisementView v, AdStatus status)
	{
		auto owned = Advertisement::copy(v); if(!owned) return;

		ObservationRow *row = nullptr;
		for(size_t i = 0; i < count_; ++i)
			if(rows_[i].packet.address == v.address && rows_[i].packet.sid == v.sid){row = &rows_[i]; break;}

		if(!row)
		{
			if(count_ < capacity) row = &rows_[count_++];
			else
			{
				row = &*std::min_element(rows_.begin(), rows_.end(), [](auto &a, auto &b){
					return a.packet.time_ms < b.packet.time_ms;
				}); ++evicted_;
			}

			*row = {};
		}

		row->packet = *owned; row->status = status; row->manufacturer = -1; ++row->packets;
		if(status != AdStatus::complete){row->advertising_knowledge = {}; row->response_knowledge = {}; return;}

		auto knowledge = classify_advertisement(v);
		for(const auto &match:engine.recognize(v))
		{
			knowledge.offer({match.detection.kind, KnowledgeSource::handler,
			                 match.detection.decoded ? KnowledgeStrength::decoded : KnowledgeStrength::hint, 0});
			if(match.detection.kind == DeviceKind::tire_pressure_sensor && match.detection.decoded)
				knowledge.add({AdvertisedCapability::tire_pressure, KnowledgeSource::handler, 0});
		}
		if(v.radio && v.radio->scan_response()){row->response_knowledge = knowledge; row->response_time = v.time_ms;}
		else {row->advertising_knowledge = knowledge; row->advertising_time = v.time_ms;}

		auto local = field(v.payload, 9); if(!local) local = field(v.payload, 8);

		if(local){row->local_name.fill(0); copy_name(*local, row->local_name);}

		// Only manufacturer data observed in this packet; name can persist across
		// scan-response/advertising pairs, but its source is not fabricated.
		row->manufacturer = -1;
		if(auto m = field(v.payload, 0xff); m && m->size() >= 2) row->manufacturer = (*m)[0] | (uint16_t((*m)[1]) << 8);
	}

	bool ResearchModule::write_summary(const BtEngine &engine, CborWriter &w, const char *source, uint64_t now)const
	{
		w.map(16); write_summary_fields(engine, w, source, now); return w.good();
	}

	void ResearchModule::write_summary_fields(const BtEngine &engine, CborWriter &w, const char *source,
	    uint64_t now)const
	{
		const auto counters = engine.counters(); const auto backend = engine.backend();
		w.text("schema"); w.text("nabtools.research.v1");
		w.text("source"); w.text(source);
		w.text("gatt"); const auto gatt = engine.gatt() ? engine.gatt()->stats() : GattStats{};
		w.map(3); w.text("ready"); w.boolean(gatt.ready); w.text("connections"); w.number(gatt.connections);
		w.text("dropped"); w.number(gatt.dropped);
		w.text("now_ms"); w.number(now);
		w.text("scan"); w.text(name(backend.state));
		w.text("scan_mode"); w.text(name(backend.scan_mode));
		w.text("requested_scan_mode"); w.text(name(backend.requested_scan_mode));
		w.text("scan_mode_pending"); w.boolean(backend.scan_mode_pending);
		w.text("scan_mode_supported"); w.boolean(backend.scan_mode_supported);
		w.text("backend_error"); w.integer(backend.error);
		w.text("observed"); w.number(counters.observed);
		w.text("malformed"); w.number(counters.malformed);
		w.text("fragments"); w.number(counters.fragments);
		w.text("dropped"); w.number(backend.dropped);
		w.text("sig"); w.text(sig::profile());
		w.text("evicted"); w.number(evicted_);
	}

	void ResearchModule::write_device(const BtEngine &engine, CborWriter &w, const DeviceInfo &d)
	{
		w.map(11);
		w.text("slot"); w.number(d.id.slot); w.text("generation"); w.number(d.id.generation);
		w.text("address"); w.text(d.address.text()); w.text("protocol"); w.text(d.protocol);
		w.text("address_type"); w.number(d.address.type);
		w.text("packets"); w.number(d.packets); w.text("last_seen_ms"); w.number(d.last_seen_ms);
		w.text("delivered"); w.number(d.delivered); w.text("dropped"); w.number(d.dropped);
		const auto interaction = engine.interaction(d.id);
		const auto operations = [&](const char *key, std::span<const OperationInfo> values){
				w.text(key); w.array(values.size()); for(const auto &op:values)
				{
					w.map(4);
					w.text("key"); w.text(op.key); w.text("label"); w.text(op.label);
					w.text("status"); w.text(name(op.status)); w.text("reason"); w.text(op.reason ? op.reason : "");
				}
			};
		operations("commands", interaction.commands); operations("events", interaction.events);
	}

	void ResearchModule::write_observation(const BtEngine &engine, CborWriter &w, const ObservationRow &row)
	{
		w.map(16);
		w.text("address"); w.text(row.packet.address.text());
		w.text("address_type"); w.number(row.packet.address.type);
		w.text("address_type_name"); w.text(address_type_name(row.packet.address.type));
		w.text("sid"); w.number(row.packet.sid);
		w.text("name"); w.text(row.local_name.data());
		w.text("rssi"); w.integer(row.packet.rssi);
		w.text("packets"); w.number(row.packets);
		w.text("last_seen_ms"); w.number(row.packet.time_ms);
		w.text("status"); w.text(name(row.status));
		const auto knowledge = row.classification(); w.text("classification"); w.map(8);
		w.text("type"); w.text(name(knowledge.type.kind));
		w.text("strength"); w.text(name(knowledge.type.strength));
		w.text("source"); w.text(name(knowledge.type.source));
		w.text("value"); w.number(knowledge.type.value);
		w.text("conflict"); w.boolean(knowledge.conflict);
		w.text("truncated"); w.boolean(knowledge.truncated);
		const auto claims = [&](const char *key, bool wanted){
				size_t count = 0;
				for(size_t i = 0; i < knowledge.count; ++i)
					if((knowledge.capabilities[i].source == KnowledgeSource::solicitation) == wanted) ++count;

				w.text(key); w.array(count);
				for(size_t i = 0; i < knowledge.count; ++i)
				{
					const auto &c = knowledge.capabilities[i]; if((c.source == KnowledgeSource::solicitation) != wanted)
						continue;

					w.map(3); w.text("key"); w.text(name(c.capability)); w.text("source"); w.text(name(c.source));
					w.text("value"); w.number(c.value);
				}
			};
		claims("capabilities", false); claims("requested", true);
		w.text("radio");
		if(!row.packet.radio)w.null(); else
		{
			const auto &r = *row.packet.radio; w.map(12);
			w.text("event_properties"); w.number(r.event_properties);
			const auto phy = [&](const char *key, uint8_t value){
					w.text(key); w.map(2); w.text("code"); w.number(value); w.text("name"); w.text(phy_name(value));
				};
			phy("primary_phy", r.primary_phy); phy("secondary_phy", r.secondary_phy);
			w.text("tx_power"); w.integer(r.tx_power);
			w.text("tx_power_dbm"); if(auto value = r.tx_power_dbm())w.integer(*value); else w.null();

			w.text("periodic_interval"); w.number(r.periodic_interval);
			w.text("periodic_interval_us"); if(r.periodic_interval)w.number(uint32_t(r.periodic_interval) * 1250); else
				w.null();

			w.text("direct_address"); w.map(4); w.text("address"); w.text(r.directed_address.text());
			w.text("type"); w.number(r.directed_address.type); w.text("type_name");
			w.text(address_type_name(r.directed_address.type));
			w.text("directed"); w.boolean(r.directed());
			w.text("data_status"); w.map(2); w.text("code"); w.number(r.data_status); w.text("name");
			w.text(data_status_name(r.data_status));
			w.text("connectable"); w.boolean(r.connectable()); w.text("scannable"); w.boolean(r.scannable());
			w.text("scan_response"); w.boolean(r.scan_response());
		}

		w.text("manufacturer_id"); if(row.manufacturer < 0)w.null(); else w.number(row.manufacturer);

		w.text("manufacturer");
		const char *label = row.manufacturer < 0 ? nullptr : sig::company(static_cast<uint16_t>(row.manufacturer));
		if(label)w.text(label); else w.null();

		w.text("raw"); w.bytes(row.packet.view().payload);
		w.text("handlers"); const auto matches = engine.recognize(row.packet.view()); w.array(matches.size());
		for(const auto &match:matches)
		{
			w.map(4);
			w.text("key"); w.text(match.key); w.text("label"); w.text(match.detection.label);
			w.text("evidence"); w.text(match.detection.evidence); w.text("decoded"); w.boolean(match.detection.decoded);
		}
		w.text("ad");
		size_t fields = 0; if(row.status == AdStatus::complete)
		{
			AdCursor c(row.packet.view().payload); while(c.next())
				++fields;
		}

		w.array(fields);
		if(fields)
		{
			AdCursor c(row.packet.view().payload); while(auto f = c.next())
			{
				w.map(3); w.text("type"); w.number(f->type); w.text("name"); auto n = sig::ad_type(f->type); if(n)
					w.text(n);
				else w.null();

				w.text("data"); w.bytes(f->value);
			}
		}
	}

	bool ResearchModule::write_snapshot(const BtEngine &engine, CborWriter &w, const char *source, uint64_t now)const
	{
		w.map(18); write_summary_fields(engine, w, source, now);
		w.text("devices"); const auto devices = engine.devices(); w.array(devices.size());
		for(const auto &d:devices)
			write_device(engine, w, d);
		w.text("observations"); w.array(count_);
		for(const auto &row:rows())
			write_observation(engine, w, row);
		return w.good();
	}

	const ObservationRow * ResearchModule::find(Address address, uint8_t sid)const
	{
		for(const auto &row:rows())
			if(row.packet.address == address && row.packet.sid == sid) return &row;

		return nullptr;
	}

}

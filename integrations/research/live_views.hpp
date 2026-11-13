// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "stream.hpp"
#include "NaBTools/research/research.hpp"

namespace NaBTools::runtime::research
{
// Coalesce advertising/UI state, never protocol messages. Check one row of
// each bounded collection per tick; a scanner burst cannot monopolize BT work.
	class LiveViews
	{
		size_t cursor_{}; uint64_t next_row_{}, next_meta_{};
		struct Seen {Address address; uint8_t sid{}; uint32_t packets{}; uint64_t time{}; bool valid{};};
		std::array<Seen, ResearchModule::capacity> seen_{};
		std::array<uint64_t, BtEngine::max_devices> device_hash_{};
		std::array<DeviceId, BtEngine::max_devices> device_ids_{};
		std::array<uint8_t, EventStream::frame_capacity> bytes_{};
	public:
		static void write_runtime(CborWriter &w, const BtEngine &engine, const ResearchModule &view, uint64_t now,
		    uint64_t message_dropped, uint64_t stream_dropped)
		{
			const auto devices = engine.devices();
			w.map(5); w.text("runtime"); view.write_summary(engine, w, "espidf.bluedroid", now);
			w.text("device_ids"); w.array(devices.size()); for(const auto &d:devices)
			{
				w.array(2); w.number(d.id.slot); w.number(d.id.generation);
			}
			w.text("observation_ids"); w.array(view.rows().size()); for(const auto &row:view.rows())
			{
				w.array(3); w.text(row.packet.address.text()); w.number(row.packet.address.type);
				w.number(row.packet.sid);
			}
			w.text("message_dropped"); w.number(message_dropped); w.text("stream_dropped"); w.number(stream_dropped);
		}

		void update(EventStream &stream, const BtEngine &engine, const ResearchModule &view, uint64_t now,
		    uint64_t message_dropped, uint64_t stream_dropped, bool force = false)
		{
			const auto devices = engine.devices();
			const auto index = cursor_ % ResearchModule::capacity;
			const bool due = now >= next_row_;
			const bool observation_identity = due && index < view.rows().size() &&
			    (!seen_[index].valid || seen_[index].address != view.rows()[index].packet.address ||
			    seen_[index].sid != view.rows()[index].packet.sid);
			const bool device_identity = due && index < devices.size() && device_ids_[index] != devices[index].id;
			// Publish authoritative membership BEFORE a new identity. At capacity,
			// the receiver must remove the evicted row before inserting its replacement.
			// A heartbeat also detects loss of a tail event without new measurements.
			if(force || observation_identity || device_identity || now >= next_meta_)
			{
				next_meta_ = now + 1000;
				stream.emit("runtime", [&](CborWriter &w){
					write_runtime(w, engine, view, now, message_dropped, stream_dropped);
				});
			}

			if(due)
			{
				next_row_ = now + 20; ++cursor_;
				if(index < view.rows().size())
				{
					const auto &row = view.rows()[index]; auto &old = seen_[index];
					if(!old.valid || old.address != row.packet.address || old.sid != row.packet.sid ||
					    old.packets != row.packets || old.time != row.packet.time_ms)
					{
						stream.emit("observation", [&](CborWriter &w){
							ResearchModule::write_observation(engine, w, row);
						});
						old = {row.packet.address, row.packet.sid, row.packets, row.packet.time_ms, true};
					}
				}

				if(index < devices.size())
				{
					const auto &d = devices[index]; CborWriter w(bytes_); ResearchModule::write_device(engine, w, d);
					device_ids_[index] = d.id;
					uint64_t hash = 14695981039346656037ULL;
					for(auto b:w.result())
					{
						hash ^= b; hash *= 1099511628211ULL;
					}
					if(!w.good() || hash != device_hash_[index])
					{
						stream.emit("device", [&](CborWriter &out){
							ResearchModule::write_device(engine, out, d);
						}); device_hash_[index] = hash;
					}
				}
				else {device_hash_[index] = 0; device_ids_[index] = {};}
			}
		}

	};
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/research/research.hpp"
#include "NaBTools/catalogs/sig/sig.hpp"
#include "NaBTools/devices/tomtom/tpms/tpms.hpp"
#include "live_views.hpp"
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace NaBTools::runtime;
void check(bool v, const char *m)
{
	if(!v)
		throw std::runtime_error(m);
}

class Backend : public IBackend
{
public:
	std::deque<Advertisement> data;
	Status start()override
	{
		return Status::ok;
	}

	void pump()override
	{
	}

	Status scan(bool)override
	{
		return Status::ok;
	}

	bool take(Advertisement &v)override
	{
		if(data.empty())
			return false;

		v = data.front(); data.pop_front(); return true;
	}

	BackendSnapshot snapshot()const override
	{
		return {ScanState::scanning, 0, 99, 2};
	}

};
Advertisement make(uint8_t id, uint16_t company, const char *label, int8_t rssi = -80)
{
	Advertisement p; p.address.bytes = {1, 2, 3, 4, 5, id}; p.rssi = rssi; p.time_ms = 1000 + id;
	p.data[0] = 3; p.data[1] = 0xff; p.data[2] = uint8_t(company); p.data[3] = uint8_t(company >> 8);
	if(id == 1 || id >= 6)
	{
		RadioInfo r; r.event_properties = 0x13; r.primary_phy = 1; r.secondary_phy = 2; r.tx_power = -12;
		r.periodic_interval = 80; p.radio = r;
	}

	const size_t n = std::min(size_t(30), std::strlen(label)); p.data[4] = uint8_t(n + 1); p.data[5] = 9;
	std::copy_n(label, n, p.data.begin() + 6); p.size = uint16_t(6 + n); return p;
}

int main(int argc, char **argv)
{
	try{
#if NABTOOLS_SIG_PROFILE
		check(std::string(sig::company(0x75)).find("Samsung") != std::string::npos, "Samsung common");
		check(std::string(sig::company(0x87)).find("Garmin") != std::string::npos, "Garmin common");
		check(std::string(sig::company(0x157)).find("Huami") != std::string::npos, "Huami common");
		check(std::string(sig::characteristic(0x2a37)) == "Heart Rate Measurement", "HRM named");
		check(sig::company(0xffff) == nullptr && sig::ad_type(0xfe) == nullptr, "unknown lookup bounded");
		check(std::string(sig::descriptor(0x2902)) == "Client Characteristic Configuration", "descriptor named");
		check(std::string(sig::declaration(0x2800)) == "Primary Service", "declaration named");
		check(std::string(sig::unit(0x2701)).find("metre") != std::string::npos, "unit named");
		check(sig::service_class(0x1101) && sig::protocol(3) && sig::browse_group(0x1002) && sig::object_type(0x2aca) &&
		    sig::appearance(0x340), "additional catalogs");
		check(sig::uuid(Uuid::from16(0x2902)) == sig::descriptor(0x2902), "UUID catalog dispatch");
		auto vendor = Uuid::from16(0x2902); vendor.bytes[0] ^= 1; check(!sig::uuid(vendor), "vendor UUID not guessed");
#if NABTOOLS_SIG_PROFILE >= 2
		check(sig::member(0xfeff) && sig::sdo(0xfffe) && sig::mesh_profile(0x1600), "all transferred categories");
#else
		check(!sig::member(0xfeff) && !sig::sdo(0xfffe) && !sig::mesh_profile(0x1600),
		    "unselected COMMON categories absent");
#endif
#else
		check(!sig::company(0x75) && !sig::service(0x180d), "SIG completely disabled");
		for(unsigned t = 0; t <= unsigned(sig::Table::appearance); ++t)
			check(!sig::lookup(static_cast<sig::Table>(t), 0x2902), "all SIG tables disabled");
#endif
		auto b = std::make_unique<Backend>(); auto *backend = b.get(); BtEngine engine(std::move(b));
		auto m = std::make_unique<ResearchModule>(); auto *research = m.get(); engine.add_module(std::move(m));
		engine.start();
		backend->data.push_back(make(1, 0x75, "Samsung nearby candidate", -39));
		backend->data.push_back(make(2, 0x87, "Garmin HRM candidate", -70));
		backend->data.push_back(make(3, 0xfffe, "Long unknown device 1234567890", -101));
		auto malformed = make(4, 0x75, "bad"); malformed.data[0] = 255; backend->data.push_back(malformed);
		auto fragment = make(5, 0x87, ""); fragment.legacy = false; backend->data.push_back(fragment);
		backend->data.push_back(make(6, 0xfffe, "SmartRemote"));
		auto watch = make(7, 0xfffe, "Sports watch");
		for(uint8_t b:{3, 0x19, 0xc0, 0, 5, 3, 0x0d, 0x18, 0x0f, 0x18})
			watch.data[watch.size++] = b;
		backend->data.push_back(watch);
		auto keyboard = make(8, 0xfffe, "Keyboard with a long name");
		for(uint8_t b:{3, 0x19, 0xc1, 3, 3, 3, 0x12, 0x18})
			keyboard.data[keyboard.size++] = b;
		backend->data.push_back(keyboard);
		engine.poll(); check(research->rows().size() == 8, "all observations retained");
		check(research->rows()[5].classification().type.kind == DeviceKind::remote_control, "name-only remote fixture");
		check(research->rows()[6].classification().type.kind == DeviceKind::watch,
		    "appearance overrides HRM type inference");
		// Scan response metadata supplements advertising but expires independently.
		auto response = make(6, 0xfffe, "SmartRemote"); response.radio->event_properties |= 8;
		for(uint8_t b:{3, 3, 0x12, 0x18})
			response.data[response.size++] = b;
		response.time_ms = 1010; backend->data.push_back(response); engine.poll();
		check(research->rows()[5].classification().count == 1, "scan response adds capabilities");
		check(research->rows()[3].manufacturer == -1, "malformed not decoded");
		check(research->rows()[4].status == AdStatus::fragment, "fragment labeled");
		// Serialize a genuinely generated fixture for independent CBOR and browser QA.
		std::array<uint8_t, 32768> buffer{}; CborWriter writer(buffer);
		writer.map(2); writer.text("runtime");
		check(research->write_snapshot(engine, writer, "native fixture", 1500), "snapshot fits");
		writer.text("tpms"); writer.array(0);
		check(writer.good(), "wrapper complete");
		if(argc > 1)
		{
			std::ofstream out(argv[1], std::ios::binary); auto bytes = writer.result();
			out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		}

		auto later = make(6, 0xfffe, "SmartRemote"); later.time_ms = 32000; backend->data.push_back(later);
		engine.poll();
		check(research->rows()[5].classification().count == 0, "old scan response facts expire");
		// Malformed fuzz corpus; no SDK, no packet buffer borrowed past callback.
		uint32_t seed = 0x42;
		for(size_t n = 0; n < 5000; ++n)
		{
			Advertisement p; p.size = uint16_t(n % 256);
			for(size_t i = 0; i < p.size; ++i)
			{
				seed = 1664525 * seed + 1013904223; p.data[i] = uint8_t(seed >> 24);
			}
			(void)validate(p.view()); (void)TomTomPacket::parse(p.view()); (void)classify_advertisement(p.view());
		}
		for(unsigned i = 6; i < 70; ++i)
		{
			auto full = make(uint8_t(i), 0x75, "bounded");
			const uint8_t fields[]{3, 0x19, 0xc0, 0, 5, 3, 0x0d, 0x18, 0x0f, 0x18};
			for(auto byte:fields)
				full.data[full.size++] = byte;
			backend->data.push_back(full);
		}
		engine.poll(100);
		check(research->rows().size() == ResearchModule::capacity && research->evicted() == 37,
		    "bounded table eviction");
		// Exercise the actual producer at full capacity, including a replacement.
		std::vector<std::vector<uint8_t> > frames;
		research::EventStream stream; stream.begin(42, &frames, [](void *c, Bytes bytes){
			static_cast<decltype(frames) *>(c)->emplace_back(bytes.begin(), bytes.end()); return true;
		});
		research::LiveViews live;
		for(unsigned i = 0; i < 32; ++i)
			live.update(stream, engine, *research, 2000 + i * 20, 0, 0);
		frames.clear(); CborWriter reset(buffer); reset.map(5); reset.text("v"); reset.number(1); reset.text("boot_id");
		reset.number(42);
		reset.text("seq"); reset.number(stream.sequence()); reset.text("reset"); reset.boolean(true);
		reset.text("changes"); reset.array(33);
		reset.array(2); reset.text("runtime"); research::LiveViews::write_runtime(reset, engine, *research, 2700, 0, 0);
		for(const auto &row:research->rows())
		{
			reset.array(2); reset.text("observation"); ResearchModule::write_observation(engine, reset, row);
		}
		check(reset.good(), "full reset fixture fits");
		frames.emplace_back(reset.result().begin(), reset.result().end());
		backend->data.push_back(make(70, 0x75, "replacement")); engine.poll();
		for(unsigned i = 0; i < 32; ++i)
			live.update(stream, engine, *research, 2800 + i * 20, 0, 0);
		std::vector<uint8_t> encoded(131072); CborWriter updates(encoded); updates.array(frames.size());
		for(const auto &frame:frames)
			updates.bytes(frame);
		check(updates.good(), "bounded membership fixture fits");
		if(argc > 1)
		{
			std::ofstream out(std::string(argv[1]) + ".updates", std::ios::binary); auto bytes = updates.result();
			out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		}

		std::cout << "PASS SIG=" << sig::profile() << "; parser fuzz 5000 inputs\n"; return 0;
	}catch(const std::exception &e){std::cerr << e.what() << '\n'; return 1;}
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/devices/samsung/smarttag/smarttag.hpp"
#if NABTOOLS_SMARTTAG_CBOR
#include "journal.hpp"
#endif
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace NaBTools::runtime;
void check(bool value, const char *text)
{
	if(!value)
		throw std::runtime_error(text);
}

struct Backend : IBackend {
	std::deque<Advertisement> q;
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

	bool take(Advertisement &p)override
	{
		if(q.empty())
			return false;

		p = q.front(); q.pop_front(); return true;
	}

	BackendSnapshot snapshot()const override
	{
		return {};
	}

};
Advertisement packet()
{
	Advertisement p; p.address.bytes = {0x62, 0x63, 0x9c, 0xf8, 0x3e, 0xc7}; p.address.type = 1; p.time_ms = 1000;
	constexpr uint8_t bytes[]{0x17, 0x16, 0x5a, 0xfd, 0x15, 0x1f, 0x60, 0x03, 0x7d, 0x82, 0xd7, 0xe6, 0xc3, 0x81, 0x8f,
		                      0xd5, 0xb2, 0, 0, 0, 0x25, 0x9f, 0, 0x15};
	std::copy(std::begin(bytes), std::end(bytes), p.data.begin()); p.size = sizeof(bytes); return p;
}

struct Capture {unsigned typed{}, cbor{}; std::vector<uint8_t> bytes; bool remove{}; BtEngine *engine{};};
int main(int argc, char **argv)
{
	try{
		auto p = packet(); const auto parsed = SmartTagPacket::parse(p.view());
		check(parsed && parsed->decoded(), "real FD5A format");
		check(parsed->version() == 1 && parsed->state() == 5 && parsed->battery_level() == 2 && parsed->region() == 11,
		    "packed fields");
		check(parsed->aging_counter() == 0x03601f && parsed->privacy_id().size() == 8 &&
		    parsed->signature().size() == 4, "field offsets");
		for(size_t n = 0; n < p.size; ++n)
		{
			auto v = p.view(); v.payload = v.payload.first(n); check(!SmartTagPacket::parse(v), "truncation rejected");
		}
		auto fake = p; fake.data[1] = 0xff;
		check(!SmartTagRecognizer::matches(fake.view()), "Samsung manufacturer alone not a tag");
		auto fragmented = p; fragmented.complete = false;
		check(!SmartTagRecognizer::matches(fragmented.view()), "fragment not a tag");
		auto backend = std::make_unique<Backend>(); auto *b = backend.get(); BtEngine e(std::move(backend)); Capture c;
		c.engine = &e;
		SmartTagActions actions;
#if NABTOOLS_SMARTTAG_STRUCT
		actions.reading = {&c, [](void *ctx, const MessageInfo &info, const SmartTagReading &reading){
							   auto &s = *static_cast<Capture *>(ctx); ++s.typed;
							   check(reading.packet.decoded(), "typed view"); if(s.remove)
								   s.engine->remove_device(info.device);

							   return Delivery::accepted;
						   }};
#endif
#if NABTOOLS_SMARTTAG_CBOR
		actions.cbor = {&c, [](void *ctx, const MessageInfo &, Bytes bytes){
							auto &s = *static_cast<Capture *>(ctx); ++s.cbor;
							s.bytes.assign(bytes.begin(), bytes.end()); return Delivery::accepted;
						}};
#endif
		e.add_module(std::make_unique<SmartTagModule>(actions)); e.start(); b->q.push_back(p); e.poll();
		check(e.devices().empty(), "recognition never auto-creates a device");
		check(e.recognize(p.view()).size() == 1, "scanner recognition");
		DeviceId id; check(e.attach("nabtools.samsung.smarttag", p.view(), id) == Status::ok, "explicit add");
		DeviceId same;
		check(e.attach("nabtools.samsung.smarttag", p.view(), same) == Status::ok && same == id, "idempotent add");
		b->q.push_back(p); b->q.push_back(p); e.poll(); check(e.devices()[0].packets == 2, "instance counts repeats");
#if NABTOOLS_SMARTTAG_CBOR
		check(c.cbor == 1, "identical adverts do not flood outputs");
		if(argc > 1)
		{
			std::ofstream out(argv[1], std::ios::binary);
			out.write(reinterpret_cast<const char *>(c.bytes.data()), c.bytes.size());
		}

		research::MessageJournal journal; const MessageInfo meta{id, 100, 1, "test.v1"};
		std::array<uint8_t, 2> payload{0x18, 0x42};
		check(journal.push(meta, payload) == Delivery::accepted, "journal accepts"); payload[1] = 0;
		std::array<uint8_t, 8192> output; CborWriter copy(output); journal.write(copy);
		if(argc > 1)
		{
			std::ofstream out(std::string(argv[1]) + ".journal", std::ios::binary); auto bytes = copy.result();
			out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		}

		for(unsigned i = 0; i < 20; ++i)
			check(journal.push(meta, payload) == Delivery::accepted, "bounded journal wrap");
		CborWriter writer(output); journal.write(writer); check(writer.good(), "journal serialized");
		if(argc > 1)
		{
			std::ofstream out(std::string(argv[1]) + ".wrapped", std::ios::binary); auto bytes = writer.result();
			out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		}

		std::array<uint8_t, 769> oversized{};
		check(journal.push(meta, oversized) == Delivery::dropped && journal.dropped() == 1, "oversized output bounded");
#endif
		p.data[16] ^= 1; b->q.push_back(p); e.poll();
#if NABTOOLS_SMARTTAG_CBOR
		check(c.cbor == 2, "payload delta emitted");
#endif
#if NABTOOLS_SMARTTAG_STRUCT
		check(c.typed == 2, "typed output uses same processed observation");
		c.remove = true; p.data[16] ^= 1; b->q.push_back(p); e.poll(); check(!e.device(id), "remove during action");
#if NABTOOLS_SMARTTAG_CBOR
		check(c.cbor == 2, "removal cancels later outputs of the same observation");
#endif
#else
		e.remove_device(id);
#endif
		check(e.remove_device(id) == Status::expired, "expired handle");
		check(e.attach("nabtools.samsung.smarttag", p.view(),
		    same) == Status::ok && same.slot == id.slot && same.generation != id.generation, "re-add generation");
		auto unknown = p; unknown.data[4] = 0x25;
		check(SmartTagPacket::parse(unknown.view()) && !SmartTagPacket::parse(unknown.view())->decoded(),
		    "unknown version preserved without false decode");
		std::cout << "PASS SmartTag parser, recognition, explicit instance and output lifecycle\n"; return 0;
	}catch(const std::exception &e){std::cerr << e.what() << '\n'; return 1;}
}

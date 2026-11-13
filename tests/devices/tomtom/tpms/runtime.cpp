// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/devices/tomtom/tpms/tpms.hpp"
#if NABTOOLS_TPMS_CBOR
#include "NaBTools/core/cbor.hpp"
#endif
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <limits>
#include <bit>
using namespace NaBTools::runtime;
int checks{};
void check(bool value, const char *message)
{
	++checks; if(!value)
		throw std::runtime_error(message);
}

class FakeBackend : public IBackend
{
public:
	std::deque<Advertisement> queue;
	BackendSnapshot status;
	unsigned starts{};
	Status start()override
	{
		++starts; status.state = ScanState::scanning; return Status::ok;
	}

	void pump()override
	{
	}

	Status scan(bool v)override
	{
		status.state = v ? ScanState::scanning : ScanState::stopped; return Status::ok;
	}

	bool take(Advertisement &out)override
	{
		if(queue.empty())
			return false;

		out = queue.front(); queue.pop_front(); return true;
	}

	BackendSnapshot snapshot()const override
	{
		return status;
	}

};
Advertisement packet(uint8_t address = 1, int32_t temp = -1234)
{
	Advertisement p; p.address.bytes = {0x80, 0xea, 0xca, 0x10, 0x8a, address}; p.time_ms = 123456; p.rssi = -65;
	uint8_t data[]{2, 1, 5, 19, 0xff, 0, 1, 0x80, 0xea, 0xca, 0x10, 0x8a, 0x78, 0, 0, 0, 0, 0, 0, 0, 0, 91, 1};
	p.size = sizeof(data); std::copy(std::begin(data), std::end(data), p.data.begin());
	uint32_t pressure = 250000, temperature = std::bit_cast<uint32_t>(temp);
	for(unsigned i = 0; i < 4; ++i)
	{
		p.data[13 + i] = uint8_t(pressure >> (8 * i)); p.data[17 + i] = uint8_t(temperature >> (8 * i));
	}
	return p;
}

struct Capture {
	unsigned typed{}, cbor{};
	std::vector<uint8_t> bytes;
	bool remove_self{}; BtEngine *engine{}; bool alive_in_callback{}; DeviceId last_id;
#if NABTOOLS_TPMS_STRUCT
	static Delivery reading(void *ctx, const MessageInfo &meta, const TpmsReading &value)
	{
		auto &c = *static_cast<Capture *>(ctx); ++c.typed; c.last_id = meta.device;
		check(value.pressure_pa == 250000, "pressure units");
		check(value.temperature_centi_c == -1234, "signed negative temperature");
		check(value.battery_percentage == 91 && value.alarm == 1, "battery/alarm");
		if(c.remove_self)
		{
			check(c.engine->remove_device(meta.device) == Status::ok, "self removal accepted");
			c.alive_in_callback = true;
		}

		return Delivery::accepted;
	}

#endif
#if NABTOOLS_TPMS_CBOR
	static Delivery encoded(void *ctx, const MessageInfo &meta, Bytes bytes)
	{
		auto &c = *static_cast<Capture *>(ctx); ++c.cbor; c.last_id = meta.device;
		c.bytes.assign(bytes.begin(), bytes.end()); return Delivery::accepted;
	}

#endif
	TpmsActions actions()
	{
		TpmsActions a;
#if NABTOOLS_TPMS_STRUCT
		a.reading = {this, reading};
#endif
#if NABTOOLS_TPMS_CBOR
		a.cbor = {this, encoded};
#endif
		return a;
	}

};
class Observer : public IModule
{
public:
	unsigned complete{}, malformed{}, fragments{};
	const char * key()const override
	{
		return "test.observer";
	}

	void observe(BtEngine &, AdvertisementView, AdStatus s)override
	{
		if(s == AdStatus::complete)++complete; else if(s == AdStatus::malformed) ++malformed; else ++fragments;
	}

};
int main(int argc, char **argv)
{
	try{
		auto p = packet(); auto view = p.view();
		check(view.address.text().size() == 17, "full MAC without truncation");
		auto copied = Advertisement::copy(view); p.data[0] = 255;
		check(copied && copied->data[0] == 2, "owned copy independent from source");
		check(validate(p.view()) == AdStatus::malformed, "truncated AD rejected");
		p = packet();
		auto decoded = TomTomPacket::parse(p.view());
		check(decoded && decoded->temperature_centi_c() == -1234, "packet view signed temperature");
		// Every truncation is safe. Only complete manufacturer frames may decode.
		for(size_t n = 0; n < p.size; ++n)
		{
			auto v = p.view(); v.payload = v.payload.first(n);
			check(!TomTomPacket::parse(v), "truncated protocol rejected");
		}
		std::array<uint8_t, 7> repeated{2, 0xff, 0x00, 3, 0xff, 0x00, 0x01};
		check(field(repeated, 0xff, 0)->size() == 1, "first repeated AD retained");
		check(field(repeated, 0xff, 1)->size() == 2, "second repeated AD retained");
		check(!field(repeated, 0xff, 2), "missing repeated AD");
		p.legacy = false; check(!TomTomPacket::parse(p.view()), "extended fragment not decoded");
		p = packet(); p.complete = false;
		check(validate(p.view()) == AdStatus::fragment, "incomplete legacy not decoded");
		auto fake = std::make_unique<FakeBackend>(); auto *backend = fake.get(); BtEngine engine(std::move(fake));
		Capture capture; capture.engine = &engine;
		auto observed = std::make_unique<Observer>(); auto *obs = observed.get();
		engine.add_module(std::move(observed));
		auto module = std::make_unique<TomTomModule>(false, capture.actions()); auto *tpms = module.get();
		engine.add_module(std::move(module));
		p = packet(); const auto first = tpms->add_known(engine, p.address);
		check(first.valid() && engine.devices().size() == 1, "known device before first sighting");
		check(tpms->add_known(engine, p.address) == first, "known device creation idempotent");
		check(engine.start() == Status::ok && backend->starts == 1, "one backend initialization");
		check(engine.start() == Status::busy, "duplicate start rejected");
		backend->queue.push_back(p); backend->queue.push_back(packet(2));
		auto malformed = p; malformed.data[0] = 255; backend->queue.push_back(malformed);
		auto fragment = p; fragment.legacy = false; backend->queue.push_back(fragment);
		check(engine.poll() == Status::ok, "process observations");
		check(obs->complete == 2 && obs->malformed == 1 && obs->fragments == 1,
		    "observer sees known unknown and malformed");
		check(engine.devices().size() == 1 && engine.devices()[0].packets == 1,
		    "discovery disabled does not create unknown device");
#if NABTOOLS_TPMS_STRUCT
		check(capture.typed == 1, "one typed action");
#endif
#if NABTOOLS_TPMS_CBOR
		check(capture.cbor == 1 && !capture.bytes.empty(), "one direct CBOR action");
		if(argc > 1)
		{
			std::ofstream out(argv[1], std::ios::binary);
			out.write(reinterpret_cast<const char *>(capture.bytes.data()), capture.bytes.size());
		}

		std::array<uint8_t, 1> tiny{}; CborWriter w(tiny); w.text("overflow");
		check(!w.good() && w.result().empty(), "bounded writer rejects partial output");
		std::array<uint8_t, 16> minbuf{}; CborWriter minw(minbuf); minw.integer(std::numeric_limits<int64_t>::min());
		check(minw.good() && minw.result().size() == 9, "INT64_MIN encoder");
#endif
		check(engine.remove_device(first) == Status::ok && !engine.device(first), "remove expires handle");
		auto replacement = tpms->add_known(engine, p.address);
		check(replacement.slot == first.slot && replacement.generation != first.generation,
		    "slot reuse has new generation");
		check(engine.remove_device(first) == Status::expired, "old handle cannot remove replacement");
#if NABTOOLS_TPMS_STRUCT
		capture.remove_self = true; backend->queue.push_back(p); engine.poll();
		check(capture.alive_in_callback && engine.devices().empty(), "self removal deferred until callback completes");
#endif
		check(engine.scan(false) == Status::ok && engine.backend().state == ScanState::stopped,
		    "scan control shares backend");
		std::cout << "PASS " << checks << " checks (STRUCT=" << NABTOOLS_TPMS_STRUCT << ", CBOR=" <<
		    NABTOOLS_TPMS_CBOR << ")\n";
		return 0;
	}catch(const std::exception &ex){std::cerr << "FAIL: " << ex.what() << "\n"; return 1;}
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/devices/samsung/smarttag/smarttag.hpp"
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace NaBTools::runtime;
void check(bool yes, const char *message)
{
	if(!yes)
		throw std::runtime_error(message);
}

struct Client : IGattClient {
	std::deque<GattEvent> events; unsigned discoveries{}, reads{}, subscriptions{}, writes{}, cancelled{};
	uint16_t read_handle{};
	GattStats stats()const override
	{
		return {true, 0, 0};
	}

	Status connect(DeviceId, Address, uint64_t, GattSessionId &s)override
	{
		s = {0, 7}; return Status::ok;
	}

	Status disconnect(GattSessionId, uint64_t)override
	{
		return Status::ok;
	}

	Status discover(GattSessionId, std::optional<Uuid> filter, uint64_t)override
	{
		check(!filter, "full discovery through generic transport"); ++discoveries; return Status::ok;
	}

	Status subscribe(GattSessionId, uint16_t value, uint16_t cccd, bool indications, uint64_t)override
	{
		check(value == 42 && cccd == 43 && indications, "button indication uses discovered handles"); ++subscriptions;
		return Status::ok;
	}

	Status read(GattSessionId, uint16_t handle, uint64_t)override
	{
		read_handle = handle; ++reads; return Status::ok;
	}

	Status write(GattSessionId, uint16_t, Bytes, bool, uint64_t)override
	{
		++writes; return Status::ok;
	}

	void cancel(DeviceId)override
	{
		++cancelled;
	}

	bool take(GattEvent &e)override
	{
		if(events.empty())
			return false;

		e = events.front(); events.pop_front(); return true;
	}

};
struct Backend : IBackend {
	Client client; Status start()override
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

	bool take(Advertisement &)override
	{
		return false;
	}

	BackendSnapshot snapshot()const override
	{
		return {};
	}

	IGattClient * gatt()override
	{
		return &client;
	}

};
Uuid uuid(const char *text)
{
	Uuid out; unsigned a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p;
	auto count = std::sscanf(text, "%2x%2x%2x%2x-%2x%2x-%2x%2x-%2x%2x-%2x%2x%2x%2x%2x%2x", &a, &b, &c, &d, &e, &f, &g,
	        &h, &i, &j, &k, &l, &m, &n, &o, &p); check(count == 16, "test UUID");
	out.bytes = {uint8_t(p), uint8_t(o), uint8_t(n), uint8_t(m), uint8_t(l), uint8_t(k), uint8_t(j), uint8_t(i),
		         uint8_t(h), uint8_t(g), uint8_t(f), uint8_t(e), uint8_t(d), uint8_t(c), uint8_t(b), uint8_t(a)};
	return out;
}

int main()
{
	try{
		auto backend = std::make_unique<Backend>(); auto *b = backend.get(); BtEngine engine(std::move(backend));
		engine.start();
		struct Capture {BtEngine *engine; bool remove{}; unsigned events{}, raw{}; std::vector<uint8_t> payload;
		} capture{&engine};
		SmartTagActions actions;
		actions.gatt = {&capture, [](void *context, const MessageInfo &meta, const SmartTagGattReading &reading){
							auto &c = *static_cast<Capture *>(context); ++c.events;
							if(reading.event.kind == GattEventKind::notification)
							{
								check(std::string(reading.field) == "tag_button",
								    "known UUID interpreted only by module"); ++c.raw; if(c.remove)
									c.engine->remove_device(meta.device);
							}

							return Delivery::accepted;
						}};
		actions.cbor = {&capture, [](void *context, const MessageInfo &, Bytes value){
							auto &c = *static_cast<Capture *>(context); c.payload.assign(value.begin(), value.end());
							return Delivery::accepted;
						}};
		auto module = SmartTagModule(actions); Address address; address.type = 1;
		auto id = module.add_known(engine, address);
		auto invoke = [&](const char *key, uint64_t request){
				return engine.invoke(id, {key, "", {}, request, 100});
			};
		check(invoke("alert.start", 1) == Status::unsupported && b->client.writes == 0,
		    "no unauthenticated alarm write");
		check(invoke("connection.connect", 2) == Status::ok, "connect accepted");
		check(invoke("connection.connect", 9) == Status::busy, "duplicate connect rejected");
		auto event = [&](GattEventKind kind, uint16_t handle = 0, uint64_t request = 2){
				GattEvent e; e.owner = id; e.session = {0, 7}; e.kind = kind; e.handle = handle; e.request_id = request;
				return e;
			};
		auto push = [&](GattEvent e){
				b->client.events.push_back(e); engine.poll();
			};
		auto stale = event(GattEventKind::connected); stale.session.generation = 6; push(stale);
		check(b->client.discoveries == 0, "late session ignored");
		push(event(GattEventKind::connected)); check(b->client.discoveries == 1, "connect starts discovery");
		auto model = event(GattEventKind::characteristic, 12); model.uuid = Uuid::from16(0x2a24); model.properties = 2;
		push(model);
		auto button = event(GattEventKind::characteristic, 42);
		button.uuid = uuid("dee30003-182d-5496-b1ad-14f216324184"); button.properties = 0x20; push(button);
		auto desc = event(GattEventKind::descriptor, 43); desc.uuid = Uuid::from16(0x2902); desc.parent_handle = 42;
		push(desc);
		push(event(GattEventKind::discovery_complete));
		check(invoke("device.read_info", 3) == Status::ok && b->client.read_handle == 12,
		    "only known unencrypted info is read");
		auto read = event(GattEventKind::read, 12, 3); read.data[0] = 'X'; read.size = 1; push(read);
		check(invoke("button.observe_raw", 4) == Status::ok && b->client.subscriptions == 1,
		    "explicit raw subscription");
		push(event(GattEventKind::subscribed, 42, 4));
		auto notification = event(GattEventKind::notification, 42, 0); notification.size = 16;
		notification.data.fill(0x5a); push(notification);
		check(capture.raw == 1, "encrypted notification remains raw");
		check(b->client.writes == 0, "research does not write authentication/reset/config");
		auto oversized = notification; oversized.size = 513; push(oversized);
		check(capture.raw == 1, "oversized event rejected without reading past owned storage");
		const auto before = capture.payload; capture.remove = true; push(notification);
		check(!engine.device(id) && b->client.cancelled == 1, "removal from action cancels session");
		check(capture.payload == before, "removal suppresses subsequent CBOR output"); push(notification);
		check(capture.raw == 2, "late owner event discarded");
		std::cout << "PASS SmartTag GATT, safe discovery, raw indications and callback removal\n"; return 0;
	}catch(const std::exception &e){std::cerr << e.what() << '\n'; return 1;}
}

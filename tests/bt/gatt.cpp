// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/bt/engine.hpp"
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace NaBTools::runtime;
void check(bool value, const char *message)
{
	if(!value)
		throw std::runtime_error(message);
}

struct Client : IGattClient {
	std::deque<GattEvent> events; std::vector<DeviceId> cancelled;
	GattStats stats()const override
	{
		return {true, 0, 0};
	}

	Status connect(DeviceId, Address, uint64_t, GattSessionId &out)override
	{
		out = {0, 1}; return Status::ok;
	}

	Status disconnect(GattSessionId, uint64_t)override
	{
		return Status::ok;
	}

	Status discover(GattSessionId, std::optional<Uuid>, uint64_t)override
	{
		return Status::ok;
	}

	Status subscribe(GattSessionId, uint16_t, uint16_t, bool, uint64_t)override
	{
		return Status::ok;
	}

	Status read(GattSessionId, uint16_t, uint64_t)override
	{
		return Status::ok;
	}

	Status write(GattSessionId, uint16_t, Bytes, bool, uint64_t)override
	{
		return Status::ok;
	}

	void cancel(DeviceId id)override
	{
		cancelled.push_back(id);
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
struct State {unsigned attached{}, received{}, destroyed{}; bool remove{};};
struct Device : IDevice {
	State &s; explicit Device(State &state)
	  : s(state)
	{
	}

	~Device()
	{
		++s.destroyed;
	}

	DeviceInfo info()const override
	{
		DeviceInfo out; out.protocol = "protocol-independent-test"; return out;
	}

	void on_attached(BtEngine &e, DeviceId id)override
	{
		check(e.device(id) == this && e.gatt()->stats().ready, "attach sees registered owner and transport");
		++s.attached;
	}

	void on_advertisement(BtEngine &, DeviceId, AdvertisementView)override
	{
	}

	void on_gatt(BtEngine &e, DeviceId id, const GattEvent &value)override
	{
		check(value.owner == id && value.value()[0] == 17, "owned event routed to its owner"); ++s.received;
		if(s.remove)
		{
			check(e.remove_device(id) == Status::ok && s.destroyed == 0, "GATT action removal deferred");
			check(!e.device(id), "removed handle immediately hidden");
		}
	}

};
int main()
{
	try{
		check(Uuid::from16(0x180d) == Uuid::from32(0x180d), "short UUIDs normalized");
		check(std::string(Uuid::from16(0x180d).text().data()) == "0000180d-0000-1000-8000-00805f9b34fb",
		    "UUID text byte order");
		State state; auto backend = std::make_unique<Backend>(); auto *b = backend.get();
		BtEngine engine(std::move(backend)); engine.start();
		auto first = engine.add_device(std::make_unique<Device>(state));
		check(state.attached == 1, "optional attachment hook");
		GattEvent event; event.owner = first; event.session = {0, 1}; event.kind = GattEventKind::notification;
		event.data[0] = 17; event.size = 1;
		b->client.events.push_back(event); state.remove = true; engine.poll();
		check(state.received == 1 && state.destroyed == 1 && b->client.cancelled.size() == 1 &&
		    b->client.cancelled[0] == first, "removal releases transport ownership");
		state.remove = false; auto second = engine.add_device(std::make_unique<Device>(state));
		b->client.events.push_back(event); engine.poll();
		check(state.received == 1, "late event cannot target reused device slot");
		event.owner = second; b->client.events.push_back(event); engine.poll();
		check(state.received == 2, "new generation receives its event");
		std::cout << "PASS generic GATT dispatch, cancellation and UUID normalization\n"; return 0;
	}catch(const std::exception &error){std::cerr << error.what() << '\n'; return 1;}
}

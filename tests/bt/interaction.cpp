// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/bt/engine.hpp"
#include <iostream>
#include <stdexcept>
using namespace NaBTools::runtime;
void check(bool value, const char *message)
{
	if(!value)
		throw std::runtime_error(message);
}

struct Backend : IBackend {
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

	bool take(Advertisement &)override
	{
		return false;
	}

	BackendSnapshot snapshot()const override
	{
		return {};
	}

};
struct State {unsigned events{}, destroyed{}; uint64_t request{}; bool remove{};};
struct Device : IDevice {
	State &state; explicit Device(State &value)
	  : state(value)
	{
	}

	~Device()
	{
		++state.destroyed;
	}

	DeviceInfo info()const override
	{
		DeviceInfo d; d.protocol = "test"; return d;
	}

	void on_advertisement(BtEngine &, DeviceId, AdvertisementView)override
	{
	}

	InteractionInfo interaction()const override
	{
		static constexpr OperationInfo commands[]{{"alert.start", "Sound"},
			{"alert.stop", "Stop", Status::busy, "Not ready"}};
		static constexpr OperationInfo events[]{{"button.press", "Button"}}; return {commands, events};
	}

	Status invoke(BtEngine &engine, DeviceId id, const DeviceCommand &cmd)override
	{
		if(cmd.format != "test/typed" || cmd.arguments.size() != 1 || cmd.arguments[0] != 7) return Status::invalid;

		// This represents an application Action, independent of web/CBOR and hardware.
		++state.events; state.request = cmd.request_id;
		if(state.remove)
		{
			check(engine.invoke(id, cmd) == Status::busy, "recursive dispatch rejected");
			check(engine.poll() == Status::busy, "recursive poll rejected");
			check(engine.remove_device(id) == Status::ok, "callback removal accepted");
			check(!engine.device(id) && state.destroyed == 0, "removed handle invalid; receiver still alive");
			check(engine.interaction(id).commands.empty(), "removed descriptions hidden");
		}

		return Status::ok;
	}

};
int main()
{
	try{
		State state; BtEngine engine(std::make_unique<Backend>());
		auto id = engine.add_device(std::make_unique<Device>(state));
		const uint8_t args[]{7}; DeviceCommand command{"alert.start", "test/typed", args, 42, 100};
		check(engine.invoke(id, command) == Status::invalid, "start required"); engine.start();
		check(engine.interaction(id).commands.size() == 2 && engine.interaction(id).events.size() == 1,
		    "optional operation catalog");
		auto bad = command; bad.key = "unknown";
		check(engine.invoke(id, bad) == Status::unsupported, "unknown command rejected");
		bad.key = "alert.stop"; check(engine.invoke(id, bad) == Status::busy, "availability enforced");
		bad = command; bad.request_id = 0;
		check(engine.invoke(id, bad) == Status::invalid, "request correlation required");
		bad = command; bad.format = "wrong";
		check(engine.invoke(id, bad) == Status::invalid, "module validates opaque input");
		check(state.events == 0, "invalid commands have no effects");
		check(engine.invoke(id, command) == Status::ok && state.events == 1 && state.request == 42,
		    "driver/action round trip");
		state.remove = true;
		check(engine.invoke(id, command) == Status::ok && state.destroyed == 1,
		    "destruction deferred until command returns");
		check(engine.invoke(id, command) == Status::expired, "stale command rejected");
		auto next = engine.add_device(std::make_unique<Device>(state));
		check(next.slot == id.slot && next.generation != id.generation, "generation changes on reuse");
		check(engine.invoke(id, command) == Status::expired && state.events == 2, "old page cannot control new device");
		std::cout << "PASS format-independent commands, Actions, availability and lifetime\n";
		return 0;
	}catch(const std::exception &error){std::cerr << error.what() << '\n'; return 1;}
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/profiles/heart_rate/hrm.hpp"
#include <algorithm>
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace NaBTools::runtime;
namespace
{
	void check(bool value, const char *message)
	{
		if(!value)
			throw std::runtime_error(message);
	}

	struct Backend final : IBackend {
		std::deque<Advertisement> queue;
		IGattClient *client{};
		IGattClient * gatt()override
		{
			return client;
		}

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

		bool take(Advertisement &packet)override
		{
			if(queue.empty())
				return false;

			packet = queue.front(); queue.pop_front(); return true;
		}

		BackendSnapshot snapshot()const override
		{
			return {};
		}

	};
	Advertisement advertisement()
	{
		Advertisement packet; packet.address.bytes = {0xe3, 0x7c, 0xa2, 0xb3, 0xbd, 0xa7}; packet.address.type = 1;
		// Synthetic fixture: advertised UUID list is evidence, address/name is not.
		const uint8_t raw[]{2, 1, 6, 5, 3, 0x0f, 0x18, 0x0d, 0x18};
		std::copy(std::begin(raw), std::end(raw), packet.data.begin()); packet.size = sizeof(raw); packet.time_ms = 100;
		return packet;
	}

	struct Capture {
		BtEngine *engine{}; bool remove{}, reentrant{};
		const uint8_t *expected_raw{}; unsigned typed{}, cbor{}; std::vector<uint8_t> encoded;
		uint64_t last_sequence{};
	};
	void parser_tests()
	{
		check(!HeartRateMeasurement::parse({}), "empty value rejected");
		const uint8_t narrow[]{0, 72}; auto packet = HeartRateMeasurement::parse(narrow);
		check(packet && packet->bpm() == 72 && !packet->wide_bpm(), "8 bit BPM");
		check(!packet->contact_detected() && !packet->energy_kj() && !packet->rr_count(), "absent optional fields");
		const uint8_t wide[]{1, 0x34, 0x12}; packet = HeartRateMeasurement::parse(wide);
		check(packet && packet->bpm() == 0x1234 && packet->wide_bpm(), "little endian 16 bit BPM");
		const uint8_t full[]{0x1f, 0x2c, 1, 0x34, 0x12, 0, 4, 0, 2, 0xff, 0xff};
		packet = HeartRateMeasurement::parse(full);
		check(packet && packet->bpm() == 300 && packet->contact_supported() && packet->contact_detected() == true,
		    "contact supported/detected");
		check(packet->energy_kj() == 0x1234 && packet->rr_count() == 3, "energy and multiple RR");
		check(packet->rr_1_1024_s(0) == 1024 && packet->rr_1_1024_s(1) == 512 && packet->rr_1_1024_s(2) == 65535,
		    "RR original units/order");
		check(!packet->rr_1_1024_s(3), "RR bounds checked");
		for(size_t n = 0; n < sizeof(full); ++n)
		{
			// A shortened packet ending after complete RR values is itself valid.
			const bool valid = n >= 7 && n % 2 == 1;
			check(HeartRateMeasurement::parse(Bytes(full, n)).has_value() == valid, "all truncation boundaries");
		}
		const uint8_t absent[]{4, 80}; packet = HeartRateMeasurement::parse(absent);
		check(packet && packet->contact_detected() == false, "supported but poor/no contact");
		const uint8_t unsupported[]{2, 80}; packet = HeartRateMeasurement::parse(unsupported);
		check(packet && !packet->contact_supported() && !packet->contact_detected(),
		    "unsupported contact is unknown, not detected");
		const uint8_t zero_rr[]{0x10, 80};
		check(!HeartRateMeasurement::parse(zero_rr), "RR flag requires at least one value");
		const uint8_t odd_rr[]{0x10, 80, 0}; check(!HeartRateMeasurement::parse(odd_rr), "odd RR bytes rejected");
		const uint8_t no_energy[]{8, 80, 1};
		check(!HeartRateMeasurement::parse(no_energy), "truncated energy rejected");
		const uint8_t unknown[]{0xe0, 80, 0xab, 0xcd}; packet = HeartRateMeasurement::parse(unknown);
		check(packet && packet->reserved_flags() == 0xe0 && !packet->standard_layout(),
		    "RFU preserved, no false conformance");
		check(packet->unknown_tail().size() == 2 && packet->unknown_tail()[0] == 0xab,
		    "undeclared trailing bytes preserved");
		// Every combination of defined/RFU flags: exact mandatory length, then
		// RR optionality. This covers changing UINT8/UINT16 formats per notification.
		for(unsigned flags = 0; flags < 256; ++flags)
		{
			std::vector<uint8_t> bytes{static_cast<uint8_t>(flags), 80};
			if(flags & 1) bytes.push_back(1);

			if(flags & 8){bytes.push_back(0xff); bytes.push_back(0xff);}

			if(flags & 0x10){bytes.push_back(0); bytes.push_back(4);}

			auto value = HeartRateMeasurement::parse(bytes); check(bool(value), "all flag layouts parse");
			check(value->reserved_flags() == (flags & 0xe0), "RFU exact bits");
			check(value->energy_kj().has_value() == bool(flags & 8), "energy optionality");
			if(flags & 8) check(value->energy_kj() == 65535, "saturated energy retained");
		}
	}

	struct FakeGatt final : IGattClient {
		bool ready{}; DeviceId owner; GattSessionId session;
		unsigned generation{}, connects{}, discovers{}, subscribes{}, writes{}, cancels{};
		std::deque<GattEvent> events; uint16_t subscribed_handle{}, subscribed_cccd{}, written_handle{};
		bool indications{}, with_response{}; std::vector<uint8_t> written;
		GattStats stats()const override
		{
			return {ready, connects, 0};
		}

		Status connect(DeviceId id, Address, uint64_t, GattSessionId &out)override
		{
			owner = id; session = {0, ++generation}; out = session; ++connects; return Status::ok;
		}

		Status disconnect(GattSessionId, uint64_t)override
		{
			return Status::ok;
		}

		Status discover(GattSessionId id, std::optional<Uuid> service, uint64_t)override
		{
			check(id == session && service == Uuid::from16(0x180d), "discover exact service/session"); ++discovers;
			return Status::ok;
		}

		Status subscribe(GattSessionId id, uint16_t value, uint16_t cccd, bool indicate, uint64_t)override
		{
			check(id == session, "subscribe same session"); subscribed_handle = value; subscribed_cccd = cccd;
			indications = indicate; ++subscribes; return Status::ok;
		}

		Status read(GattSessionId, uint16_t, uint64_t)override
		{
			return Status::unsupported;
		}

		Status write(GattSessionId id, uint16_t handle, Bytes data, bool response, uint64_t)override
		{
			check(id == session, "write same session"); written_handle = handle; with_response = response;
			written.assign(data.begin(), data.end()); ++writes; return Status::ok;
		}

		void cancel(DeviceId)override
		{
			++cancels;
		}

		bool take(GattEvent &event)override
		{
			if(events.empty())
				return false;

			event = events.front(); events.pop_front(); return true;
		}

		GattEvent event(GattEventKind kind, uint64_t request = 42)
		{
			GattEvent event; event.kind = kind; event.owner = owner; event.session = session;
			event.request_id = request; event.time_ms = 1000; return event;
		}

	};
	Status command(BtEngine &engine, DeviceId id, std::string_view key, uint64_t request = 42)
	{
		const uint8_t empty_map[]{0xa0};
		const DeviceCommand command{key, "application/cbor", empty_map, request, 10};
#if NABTOOLS_DEVICE_DESCRIPTIONS
		return engine.invoke(id, command);
#else
		// Minimal builds can use a typed driver through guarded engine dispatch
		// without compiling an optional command catalog for the generic UI.
		struct Input {BtEngine &engine; DeviceId id; const DeviceCommand &command;} input{engine, id, command};
		return engine.dispatch(id, &input, [](void *opaque, IDevice &device){
			auto &input = *static_cast<Input *>(opaque); return device.invoke(input.engine, input.id, input.command);
		});
#endif
	}

	void active_tests(const char *path)
	{
		(void)path;
		struct Capture {BtEngine *engine{}; unsigned typed{}, cbor{}, states{}; bool remove{};
			            std::vector<uint8_t> last_state;};
		FakeGatt gatt; auto backend = std::make_unique<Backend>(); backend->client = &gatt;
		BtEngine engine(std::move(backend));
		Capture capture; capture.engine = &engine; HeartRateActions actions;
#if NABTOOLS_HRM_STRUCT
		actions.measurement = {&capture, [](void *opaque, const MessageInfo &meta, const HeartRateMeasurement &packet){
								   auto &capture = *static_cast<Capture *>(opaque); ++capture.typed;
								   check(packet.bpm() == 72, "GATT value actually parsed");
								   if(capture.remove)
									   capture.engine->remove_device(meta.device);

								   return Delivery::accepted;
							   }};
		actions.state = {&capture, [](void *opaque, const MessageInfo &, const HeartRateStateInfo &){
							 ++static_cast<Capture *>(opaque)->states; return Delivery::accepted;
						 }};
#endif
#if NABTOOLS_HRM_CBOR
		actions.cbor = {&capture, [](void *opaque, const MessageInfo &meta, Bytes payload){
							auto &capture = *static_cast<Capture *>(opaque);
							if(std::string_view(meta.schema) == "nabtools.bluetooth.hrm.measurement.v1")
							{
								++capture.cbor;
#if !NABTOOLS_HRM_STRUCT
								if(capture.remove) capture.engine->remove_device(meta.device);

#endif
							}
							else {++capture.states; capture.last_state.assign(payload.begin(), payload.end());}

							return Delivery::accepted;
						}};
#endif
		engine.add_module(std::make_unique<HeartRateModule>(actions)); engine.start(); DeviceId id;
		check(engine.attach("nabtools.bluetooth.hrm", advertisement().view(), id) == Status::ok,
		    "active fixture admission");
		auto *device = static_cast<HeartRateDevice *>(engine.device(id));
		check(command(engine, id, "connection.connect") == Status::unsupported,
		    "late backend initialization unavailable");
		gatt.ready = true;
		check(command(engine, id, "connection.connect") == Status::ok && gatt.connects == 1,
		    "ready backend enables connect dynamically");
		check(capture.typed == 0 && capture.cbor == 0, "connect does not fabricate measurement");
		gatt.events.push_back(gatt.event(GattEventKind::connected)); engine.poll();
		check(gatt.discovers == 1 && device->state() == HeartRateState::discovering, "connected auto-discovers HRS");
		auto service = gatt.event(GattEventKind::service); service.uuid = Uuid::from16(0x180d); service.handle = 10;
		service.end_handle = 30; gatt.events.push_back(service);
		auto measurement = gatt.event(GattEventKind::characteristic); measurement.uuid = Uuid::from16(0x2a37);
		measurement.parent_handle = 10; measurement.handle = 13; measurement.properties = 0x10;
		gatt.events.push_back(measurement);
		auto wrong = measurement; wrong.parent_handle = 31; wrong.handle = 32; gatt.events.push_back(wrong);
		auto descriptor = gatt.event(GattEventKind::descriptor); descriptor.uuid = Uuid::from16(0x2902);
		descriptor.parent_handle = 13; descriptor.handle = 14; gatt.events.push_back(descriptor);
		auto control = measurement; control.uuid = Uuid::from16(0x2a39); control.handle = 16; control.properties = 8;
		gatt.events.push_back(control);
		gatt.events.push_back(gatt.event(GattEventKind::discovery_complete)); engine.poll();
		check(gatt.subscribes == 1 && gatt.subscribed_handle == 13 && gatt.subscribed_cccd == 14 && !gatt.indications,
		    "discovery selects Notify measurement plus CCCD");
		auto notification = gatt.event(GattEventKind::notification, 0); notification.handle = 13; notification.size = 2;
		notification.data[0] = 0; notification.data[1] = 72;
		gatt.events.push_back(notification); engine.poll();
		check(device->measurements() == 0, "notification before subscription completion ignored");
		auto subscribed = gatt.event(GattEventKind::subscribed); subscribed.handle = 13;
		subscribed.status = Status::backend_error; subscribed.error = 5; gatt.events.push_back(subscribed);
		engine.poll();
		check(device->state() == HeartRateState::connected && device->measurements() == 0,
		    "subscription failure is not a measurement");
		check(command(engine, id, "measurement.subscribe", 420) == Status::ok, "explicit subscription retry");
		subscribed = gatt.event(GattEventKind::subscribed, 420); subscribed.handle = 13;
		gatt.events.push_back(subscribed); engine.poll();
		check(device->state() == HeartRateState::subscribed, "subscription asynchronous success");
		gatt.events.push_back(notification); gatt.events.push_back(notification); engine.poll();
		check(device->measurements() == 2, "identical real notifications count separately");
		auto stale = notification; ++stale.session.generation; gatt.events.push_back(stale); wrong = notification;
		++wrong.handle; gatt.events.push_back(wrong); engine.poll();
		check(device->measurements() == 2, "wrong session and handle ignored");
		check(command(engine, id, "energy.reset",
		    43) == Status::ok && gatt.writes == 1 && gatt.written_handle == 16 && gatt.with_response &&
		    gatt.written == std::vector<uint8_t>{1}, "discovered control point sends exact reset opcode");
		auto old_session = gatt.session; gatt.events.push_back(gatt.event(GattEventKind::disconnected)); engine.poll();
		check(!device->session().valid() && device->state() == HeartRateState::disconnected,
		    "disconnect clears handles/session");
		check(command(engine, id, "energy.reset", 44) == Status::unsupported, "no control point before new discovery");
		check(command(engine, id, "connection.connect", 45) == Status::ok, "explicit reconnect");
		stale = gatt.event(GattEventKind::connected, 45); stale.session = old_session; gatt.events.push_back(stale);
		engine.poll(); check(gatt.discovers == 1, "late old connection ignored");
		gatt.events.push_back(gatt.event(GattEventKind::connected, 45)); engine.poll();
		service.request_id = 45; service.session = gatt.session; gatt.events.push_back(service);
		measurement.request_id = 45; measurement.session = gatt.session; measurement.properties = 0x20;
		gatt.events.push_back(measurement);
		descriptor.request_id = 45; descriptor.session = gatt.session; gatt.events.push_back(descriptor);
		gatt.events.push_back(gatt.event(GattEventKind::discovery_complete, 45)); engine.poll();
		check(gatt.indications, "Indicate used if Notify absent");
		subscribed = gatt.event(GattEventKind::subscribed, 45); subscribed.handle = 13;
		gatt.events.push_back(subscribed); engine.poll();
		check(command(engine, id, "energy.reset", 46) == Status::unsupported,
		    "missing optional control point remains unavailable");
		auto invalid = gatt.event(GattEventKind::notification, 0); invalid.handle = 13; invalid.size = 2;
		invalid.data[0] = 0x10; invalid.data[1] = 72;
		const auto states_before = capture.states; gatt.events.push_back(invalid); engine.poll();
		check(device->measurements() == 2 && device->invalid_measurements() == 1 && capture.states > states_before,
		    "malformed real payload reports state error without a measurement");
		notification.session = gatt.session; capture.remove = true; const auto cbor_before = capture.cbor;
		gatt.events.push_back(notification); engine.poll();
		check(!engine.device(id) && gatt.cancels == 1, "GATT callback removal cancels owner transport");
#if NABTOOLS_HRM_STRUCT && NABTOOLS_HRM_CBOR
		check(capture.cbor == cbor_before, "typed removal prevents later CBOR from GATT notification");
#else
		(void)cbor_before;
#endif
		check(capture.states > 0, "state/error path independent of measurements");
		// A separate session exercises missing CCCD, subscription errors and failed
		// connection recovery without inventing any measurement data.
		capture.remove = false;
		check(engine.attach("nabtools.bluetooth.hrm", advertisement().view(), id) == Status::ok,
		    "new instance for failure paths");
		device = static_cast<HeartRateDevice *>(engine.device(id));
		check(command(engine, id, "connection.connect", 50) == Status::ok, "failure fixture connect");
		gatt.events.push_back(gatt.event(GattEventKind::connected, 50)); engine.poll();
		service = gatt.event(GattEventKind::service, 50); service.uuid = Uuid::from16(0x180d); service.handle = 10;
		service.end_handle = 30; gatt.events.push_back(service);
		measurement = gatt.event(GattEventKind::characteristic, 50); measurement.uuid = Uuid::from16(0x2a37);
		measurement.parent_handle = 10; measurement.handle = 13; measurement.properties = 0x10;
		gatt.events.push_back(measurement);
		const auto subscribe_count = gatt.subscribes;
		gatt.events.push_back(gatt.event(GattEventKind::discovery_complete, 50)); engine.poll();
		check(gatt.subscribes == subscribe_count && device->state() == HeartRateState::connected &&
		    device->measurements() == 0, "missing CCCD never subscribes or emits fake measurements");
		gatt.events.push_back(gatt.event(GattEventKind::disconnected, 50)); engine.poll();
		check(command(engine, id, "connection.connect", 51) == Status::ok, "retry explicit connect");
		auto failure = gatt.event(GattEventKind::error, 51); failure.status = Status::backend_error;
		failure.error = 133; gatt.events.push_back(failure); engine.poll();
		check(device->state() == HeartRateState::disconnected && !device->session().valid(),
		    "asynchronous connect error allows explicit retry");
#if NABTOOLS_HRM_CBOR
		if(path)
		{
			std::ofstream output(std::string(path) + ".state", std::ios::binary);
			output.write(reinterpret_cast<const char *>(capture.last_state.data()), capture.last_state.size());
		}

#endif
	}

}
int main(int argc, char **argv)
{
	try{
		(void)argc; (void)argv;
		parser_tests(); auto advertisement_packet = advertisement();
		check(HeartRateRecognizer::matches(advertisement_packet.view()), "standard UUID list recognized");
		auto incomplete = advertisement_packet; incomplete.data[4] = 2;
		check(HeartRateRecognizer::matches(incomplete.view()), "incomplete service list recognized");
		auto odd = advertisement_packet; --odd.data[3]; --odd.size;
		check(!HeartRateRecognizer::matches(odd.view()), "odd service UUID list rejected");
		auto named = advertisement_packet; named.data[4] = 9;
		check(!HeartRateRecognizer::matches(named.view()), "name/company does not establish HRS support");
		auto fragment = advertisement_packet; fragment.complete = false;
		check(!HeartRateRecognizer::matches(fragment.view()), "fragment not a complete claim");
		auto backend = std::make_unique<Backend>(); auto *source = backend.get(); BtEngine engine(std::move(backend));
		Capture capture; capture.engine = &engine; HeartRateActions actions;
#if NABTOOLS_HRM_STRUCT
		actions.measurement = {&capture,
			                   [](void *opaque, const MessageInfo &meta, const HeartRateMeasurement &measurement){
								   auto &capture = *static_cast<Capture *>(opaque); ++capture.typed;
								   check(measurement.raw().data() == capture.expected_raw, "typed output is borrowed");
								   check(meta.sequence > capture.last_sequence, "each notification has a new sequence");
								   capture.last_sequence = meta.sequence;
								   if(capture.reentrant)
								   {
									   auto *device =
									       static_cast<HeartRateDevice *>(capture.engine->device(meta.device));
									   check(device->on_measurement(*capture.engine, meta.device, measurement.raw(),
									       meta.time_ms) == Status::busy, "nested ingress rejected");
								   }

								   if(capture.remove)
								   {
									   check(capture.engine->remove_device(meta.device) == Status::ok,
									       "self-removal accepted");
									   check(!capture.engine->device(meta.device),
									       "removed generation immediately invisible");
									   check(measurement.bpm() > 0,
									       "borrowed input survives instance removal within callback");
								   }

								   return Delivery::accepted;
							   }};
#endif
#if NABTOOLS_HRM_CBOR
		actions.cbor = {&capture, [](void *opaque, const MessageInfo &meta, Bytes payload){
							(void)meta;
							auto &capture = *static_cast<Capture *>(opaque); ++capture.cbor;
							capture.encoded.assign(payload.begin(), payload.end());
#if !NABTOOLS_HRM_STRUCT
							if(capture.remove)
								check(capture.engine->remove_device(meta.device) == Status::ok,
								    "CBOR-only self removal");

#endif
							return Delivery::accepted;
						}};
#endif
		check(engine.add_module(std::make_unique<HeartRateModule>(actions)) == Status::ok, "module installed");
		check(engine.start() == Status::ok, "engine start");
		source->queue.push_back(advertisement_packet); engine.poll();
		check(engine.devices().empty(), "recognition does not admit");
		auto matches = engine.recognize(advertisement_packet.view());
		check(matches.size() == 1 && !matches[0].detection.decoded, "advertising not measurement decode");
		DeviceId id;
		check(engine.attach("nabtools.bluetooth.hrm", advertisement_packet.view(), id) == Status::ok,
		    "explicit create");
		DeviceId same;
		check(engine.attach("nabtools.bluetooth.hrm", advertisement_packet.view(), same) == Status::ok && same == id,
		    "idempotent attach");
		source->queue.push_back(advertisement_packet); engine.poll();
		check(engine.devices()[0].packets == 1, "matching advertisement counted");
		check(capture.typed == 0 && capture.cbor == 0, "no measurement fabricated from advertisements");
		auto *device = static_cast<HeartRateDevice *>(engine.device(id));
#if NABTOOLS_DEVICE_DESCRIPTIONS
		check(device->interaction().commands.size() == 4, "optional command descriptions");
		check(device->interaction().commands[0].status == Status::unsupported, "connect honestly unavailable");
		check(engine.invoke(id, {"connection.connect", "", {}, 1, 10}) == Status::unsupported,
		    "no radio operation fabricated");
#else
		check(device->interaction().commands.empty(), "descriptions removable");
#endif
		const uint8_t notification[]{0x1f, 0x2c, 1, 0x34, 0x12, 0, 4, 0, 2}; capture.expected_raw = notification;
		capture.reentrant = true;
		check(device->on_measurement(engine, id, notification, 1000) == Status::ok, "explicit characteristic ingress");
		check(device->on_measurement(engine, id, notification, 2000) == Status::ok,
		    "identical notification remains new measurement");
		check(device->measurements() == 2, "new measurements counted independently of equality");
#if NABTOOLS_HRM_STRUCT
		check(capture.typed == 2, "typed independent output");
#endif
#if NABTOOLS_HRM_CBOR
		check(capture.cbor == 2 && !capture.encoded.empty(), "CBOR independent output");
		if(argc > 1)
		{
			std::ofstream output(argv[1], std::ios::binary);
			output.write(reinterpret_cast<const char *>(capture.encoded.data()), capture.encoded.size());
		}

#endif
		const uint8_t invalid[]{0x10, 80};
		check(device->on_measurement(engine, id, invalid,
		    3000) == Status::invalid && device->invalid_measurements() == 1, "invalid measurement not emitted");
		std::vector<uint8_t> maximum(512, 0xff); maximum[0] = 0x10; maximum[1] = 80;
		capture.expected_raw = maximum.data();
		check(device->on_measurement(engine, id, maximum, 4000) == Status::ok, "maximum bounded payload with 255 RR");
		maximum.push_back(0);
		check(device->on_measurement(engine, id, maximum, 4100) == Status::invalid, "ingress bound enforced");
		capture.expected_raw = notification; capture.remove = true;
		check(device->on_measurement(engine, id, notification, 5000) == Status::ok, "removal during borrowed action");
		check(!engine.device(id), "engine reaps after protected dispatch");
#if NABTOOLS_HRM_STRUCT && NABTOOLS_HRM_CBOR
		check(capture.typed == 4 && capture.cbor == 3, "self-removal cancels following CBOR output");
#endif
		check(engine.remove_device(id) == Status::expired, "old generation expired");
		check(engine.attach("nabtools.bluetooth.hrm", advertisement_packet.view(),
		    same) == Status::ok && same.slot == id.slot && same.generation != id.generation,
		    "generation changes on re-add");
		device = static_cast<HeartRateDevice *>(engine.device(same));
		check(device->on_measurement(engine, id, notification, 6000) == Status::expired,
		    "late input for old generation rejected");
		active_tests(argc > 1 ? argv[1] : nullptr);
		std::cout << "PASS HRM parser, GATT connect/discovery/subscribe/measurement/control and lifecycle\n"; return 0;
	}catch(const std::exception &error){std::cerr << error.what() << '\n'; return 1;}
}

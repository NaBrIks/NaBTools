// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/profiles/heart_rate/hrm.hpp"
#if NABTOOLS_HRM_CBOR
#include "NaBTools/core/cbor.hpp"
#endif

namespace NaBTools::runtime
{
	namespace
	{
		uint16_t le16(Bytes bytes, size_t offset)
		{
			return uint16_t(bytes[offset]) | (uint16_t(bytes[offset + 1]) << 8);
		}

		constexpr const char *protocol = "Heart Rate";
	}
	const char * name(HeartRateState state)
	{
		switch(state)
		{
		case HeartRateState::unavailable: return "unavailable";
		case HeartRateState::disconnected: return "disconnected";
		case HeartRateState::connecting: return "connecting";
		case HeartRateState::connected: return "connected";
		case HeartRateState::discovering: return "discovering";
		case HeartRateState::subscribing: return "subscribing";
		case HeartRateState::subscribed: return "subscribed";
		case HeartRateState::disconnecting: return "disconnecting";
		}
		return "unknown";
	}

	std::optional<HeartRateMeasurement> HeartRateMeasurement::parse(Bytes bytes)
	{
		if(bytes.empty()) return {};

		const auto flags = bytes[0]; size_t offset = 1 + ((flags & 1) ? 2 : 1);
		if(flags & 8) offset += 2;

		if(bytes.size() < offset) return {};

		// HRS 1.0 section 3.1.1.4 requires at least one complete RR value
		// when bit 4 is set. A prefix containing whole RR values is a valid packet.
		if((flags & 0x10) && (bytes.size() == offset || (bytes.size() - offset) % 2)) return {};

		return HeartRateMeasurement(bytes, offset);
	}

	uint16_t HeartRateMeasurement::bpm()const
	{
		return wide_bpm() ? le16(bytes_, 1) : bytes_[1];
	}

	std::optional<uint16_t> HeartRateMeasurement::energy_kj()const
	{
		if(!(flags() & 8)) return {};

		return le16(bytes_, wide_bpm() ? 3 : 2);
	}

	std::optional<uint16_t> HeartRateMeasurement::rr_1_1024_s(size_t index)const
	{
		if(index >= rr_count()) return {};

		return le16(bytes_, rr_offset_ + 2 * index);
	}

	bool HeartRateRecognizer::matches(AdvertisementView view)
	{
		if(validate(view) != AdStatus::complete) return false;

		bool found = false; AdCursor cursor(view.payload);
		while(auto field = cursor.next())
			if(field->type == 0x02 || field->type == 0x03)
			{
				// A malformed UUID16 list is not reliable evidence, even if an earlier
				// complete pair happens to contain 180D.
				if(field->value.size() % 2) return false;

				for(size_t n = 0; n < field->value.size(); n += 2)
					if(le16(field->value, n) == 0x180d) found = true;
			}

		return found;
	}

	std::optional<Detection> HeartRateRecognizer::recognize(AdvertisementView view)
	{
		if(!matches(view)) return {};

		return Detection{protocol, "Advertised Heart Rate Service UUID 0x180D; GATT not discovered", false};
	}

	DeviceInfo HeartRateDevice::info()const
	{
		return {{}, address_, protocol, last_seen_, packets_, delivered_, dropped_};
	}

	InteractionInfo HeartRateDevice::interaction()const
	{
#if NABTOOLS_DEVICE_DESCRIPTIONS
		const bool ready = gatt_ && gatt_->stats().ready;
		const bool connected = session_.valid() && state_ != HeartRateState::connecting &&
		    state_ != HeartRateState::disconnecting;
		const char *unavailable = "GATT backend not ready";
		const bool handles = connected && measurement_handle_ && cccd_handle_;
		const auto subscribe_status = !ready ? Status::unsupported : !handles ? Status::invalid : state_ !=
		    HeartRateState::connected ? Status::busy : Status::ok;
		const char *subscribe_reason =
		    !ready ? unavailable : !handles ? "Measurement and CCCD not discovered" : state_ !=
		    HeartRateState::connected ? "Subscription or discovery already active" : nullptr;
		const auto energy_status = ready && connected && energy_handle_ ? Status::ok : Status::unsupported;
		commands_ = {{{"connection.connect", "Connect",
			!ready ? Status::unsupported : session_.valid() ? Status::busy : Status::ok,
			!ready ? unavailable : session_.valid() ? "Session already exists" : nullptr},
			{"connection.disconnect", "Disconnect",
			 !ready ? Status::unsupported : !session_.valid() ? Status::invalid : state_ ==
			 HeartRateState::disconnecting ? Status::busy : Status::ok,
			 !ready ? unavailable : !session_.valid() ? "No session" : state_ ==
			 HeartRateState::disconnecting ? "Disconnect already pending" : nullptr},
			{"measurement.subscribe", "Subscribe to heart rate", subscribe_status, subscribe_reason},
			{"energy.reset", "Reset energy expended", energy_status,
			 !ready ? unavailable : energy_status !=
			 Status::ok ? "Writable control point not discovered in a live connection" : nullptr}}};
		events_ = {{{"state", "Heart rate connection state"},
			{"measurement", "Heart rate measurement",
			 !ready ? Status::unsupported : state_ == HeartRateState::subscribed ? Status::ok : Status::busy,
			 !ready ? unavailable : state_ != HeartRateState::subscribed ? "Not subscribed" : nullptr}}};
		return {commands_, events_};
#else
		return {};
#endif
	}

	void HeartRateDevice::on_attached(BtEngine &engine, DeviceId owner)
	{
		owner_ = owner; gatt_ = engine.gatt();
		state_ = gatt_ ? HeartRateState::disconnected : HeartRateState::unavailable;
	}

	void HeartRateDevice::clear_handles()
	{
		service_handle_ = service_end_ = measurement_handle_ = measurement_properties_ = cccd_handle_ = energy_handle_ =
		    0;
	}

	Status HeartRateDevice::subscribe(uint64_t request)
	{
		if(!gatt_ || !session_.valid() || !measurement_handle_ || !cccd_handle_) return Status::unsupported;

		subscribe_request_ = request;
		const auto result = gatt_->subscribe(session_, measurement_handle_, cccd_handle_,
		        !(measurement_properties_ & 0x10), request);
		if(result == Status::ok) state_ = HeartRateState::subscribing;

		return result;
	}

	Status HeartRateDevice::invoke(BtEngine &engine, DeviceId id, const DeviceCommand &command)
	{
		if(!gatt_ || !gatt_->stats().ready) return Status::unsupported;

		if(!((command.format.empty() && command.arguments.empty()) ||
		    ((command.format == "application/cbor" || command.format == "cbor") && command.arguments.size() == 1 &&
		    command.arguments[0] == 0xa0))) return Status::invalid;

		Status result = Status::unsupported;
		if(command.key == "connection.connect")
		{
			if(session_.valid()) return Status::busy;

			clear_handles(); flow_request_ = command.request_id;
			result = gatt_->connect(id, address_, command.request_id, session_);
			if(result == Status::ok) state_ = HeartRateState::connecting; else session_ = {};
		}
		else if(command.key == "connection.disconnect")
		{
			if(!session_.valid()) return Status::invalid;

			if(state_ == HeartRateState::disconnecting) return Status::busy;

			result = gatt_->disconnect(session_, command.request_id); if(result == Status::ok)
				state_ = HeartRateState::disconnecting;
		}
		else if(command.key == "measurement.subscribe")
		{
			if(state_ != HeartRateState::connected) return Status::busy;

			result = subscribe(command.request_id);
		}
		else if(command.key == "energy.reset")
		{
			if(!session_.valid() || !energy_handle_) return Status::unsupported;

			const uint8_t reset[]{1}; result = gatt_->write(session_, energy_handle_, reset, true, command.request_id);
		}
		else return Status::unsupported;

		emit_state(engine, id, result == Status::ok ? "command.accepted" : "command.rejected", result, 0,
		    command.request_id, command.time_ms);
		return result;
	}

	void HeartRateDevice::emit_state(BtEngine &engine, DeviceId id, const char *event, Status status, int error,
	    uint64_t request, uint64_t time_ms)
	{
		const MessageInfo meta{id, time_ms, ++state_events_, "nabtools.bluetooth.hrm.state.v1"};
		const auto actions = actions_;
#if NABTOOLS_HRM_STRUCT
		if(actions.state.invoke)
		{
			const auto result = actions.state.invoke(actions.state.context, meta,
			        {state_, event, status, error, request, session_});
			if(engine.device(id) != this) return;

			if(result == Delivery::accepted) ++delivered_; else ++dropped_;
		}

#endif
#if NABTOOLS_HRM_CBOR
		if(actions.cbor.invoke)
		{
			std::array<uint8_t, 512> output; CborWriter writer(output); writer.map(10);
			writer.text("type"); writer.text("state"); writer.text("event"); writer.text(event);
			writer.text("state"); writer.text(name(state_)); writer.text("status"); writer.text(name(status));
			writer.text("error"); writer.integer(error); writer.text("request_id"); writer.number(request);
			writer.text("session_slot"); writer.number(session_.slot); writer.text("session_generation");
			writer.number(session_.generation);
			writer.text("measurement_handle"); writer.number(measurement_handle_); writer.text("control_handle");
			writer.number(energy_handle_);
			const auto result = writer.good() ? actions.cbor.invoke(actions.cbor.context, meta,
			        writer.result()) : Delivery::error;
			if(engine.device(id) != this) return;

			if(result == Delivery::accepted) ++delivered_; else ++dropped_;
		}

#endif
	}

	void HeartRateDevice::on_gatt(BtEngine &engine, DeviceId id, const GattEvent &event)
	{
		if(event.owner != id || !session_.valid() || event.session != session_) return;

		last_seen_ = event.time_ms;
		if(event.kind == GattEventKind::disconnected)
		{
			state_ = HeartRateState::disconnected; clear_handles();
			emit_state(engine, id, "disconnected", event.status, event.error, event.request_id, event.time_ms);
			session_ = {}; return;
		}

		if(event.kind == GattEventKind::connected)
		{
			if(state_ != HeartRateState::connecting) return;

			if(event.status != Status::ok)
			{
				state_ = HeartRateState::disconnected; session_ = {};
				emit_state(engine, id, "connect.failed", event.status, event.error, event.request_id, event.time_ms);
				return;
			}

			clear_handles(); state_ = HeartRateState::discovering;
			const auto result = gatt_->discover(session_, Uuid::from16(0x180d), flow_request_);
			if(result != Status::ok) state_ = HeartRateState::connected;

			emit_state(engine, id, result == Status::ok ? "discovery.started" : "discovery.failed", result, event.error,
			    event.request_id, event.time_ms); return;
		}

		if(event.kind == GattEventKind::error)
		{
			if(state_ == HeartRateState::connecting){state_ = HeartRateState::disconnected; session_ = {};}
			else if(state_ == HeartRateState::discovering || state_ == HeartRateState::subscribing)
				state_ = HeartRateState::connected;

			emit_state(engine, id, "error", event.status, event.error, event.request_id, event.time_ms); return;
		}

		if(event.kind == GattEventKind::notification)
		{
			if(state_ == HeartRateState::subscribed && event.handle == measurement_handle_)
			{
				const auto result = event.size <= GattEvent::capacity ? process_measurement(engine, id, event.value(),
				        event.time_ms) : Status::invalid;
				if(engine.device(id) == this && result != Status::ok)
					emit_state(engine, id, "measurement.invalid", result, event.error, event.request_id, event.time_ms);
			}

			return;
		}

		if(event.kind == GattEventKind::subscribed)
		{
			if(state_ != HeartRateState::subscribing || event.request_id != subscribe_request_ ||
			    event.handle != measurement_handle_) return;

			state_ = event.status == Status::ok ? HeartRateState::subscribed : HeartRateState::connected;
			emit_state(engine, id, "subscription.completed", event.status, event.error, event.request_id,
			    event.time_ms); return;
		}

		if(event.kind == GattEventKind::write)
		{
			emit_state(engine, id, "write.completed", event.status, event.error, event.request_id, event.time_ms);
			return;
		}

		if(state_ != HeartRateState::discovering || event.request_id != flow_request_) return;

		if(event.kind == GattEventKind::service && event.uuid == Uuid::from16(0x180d) && !service_handle_ &&
		    event.handle && event.end_handle >= event.handle)
		{
			service_handle_ = event.handle; service_end_ = event.end_handle;
		}
		else if(event.kind == GattEventKind::characteristic && service_handle_ &&
		    event.parent_handle == service_handle_ && event.handle >= service_handle_ && event.handle <= service_end_)
		{
			if(event.uuid == Uuid::from16(0x2a37) && (event.properties & 0x30) && !measurement_handle_)
			{
				measurement_handle_ = event.handle; measurement_properties_ = event.properties;
			}

			if(event.uuid == Uuid::from16(0x2a39) && (event.properties & 8) && !energy_handle_)
				energy_handle_ = event.handle;
		}
		else if(event.kind == GattEventKind::descriptor && event.uuid == Uuid::from16(0x2902) &&
		    event.parent_handle == measurement_handle_ && measurement_handle_ && event.handle > measurement_handle_ &&
		    event.handle <= service_end_)
		{
			cccd_handle_ = event.handle;
		}
		else if(event.kind == GattEventKind::discovery_complete)
		{
			state_ = HeartRateState::connected;
			const auto result = event.status == Status::ok ? subscribe(flow_request_) : event.status;
			emit_state(engine, id, result == Status::ok ? "subscription.started" : "discovery.failed", result,
			    event.error, event.request_id, event.time_ms);
		}
	}

	void HeartRateDevice::on_advertisement(BtEngine &, DeviceId, AdvertisementView view)
	{
		if(view.address == address_ && HeartRateRecognizer::matches(view)){last_seen_ = view.time_ms; ++packets_;}
	}

	Status HeartRateDevice::on_measurement(BtEngine &engine, DeviceId id, Bytes bytes, uint64_t time_ms)
	{
		struct Input {HeartRateDevice *self; BtEngine &engine; DeviceId id; Bytes bytes; uint64_t time_ms;};
		Input input{this, engine, id, bytes, time_ms};
		return engine.dispatch(id, &input, [](void *opaque, IDevice &target){
			auto &input = *static_cast<Input *>(opaque);
			if(&target != input.self) return Status::expired;

			return input.self->process_measurement(input.engine, input.id, input.bytes, input.time_ms);
		});
	}

	Status HeartRateDevice::process_measurement(BtEngine &engine, DeviceId id, Bytes bytes, uint64_t time_ms)
	{
		if(engine.device(id) != this) return Status::expired;

		if(bytes.size() > max_measurement_size){++invalid_; return Status::invalid;}

		const auto measurement = HeartRateMeasurement::parse(bytes);
		if(!measurement){++invalid_; return Status::invalid;}

		last_seen_ = time_ms;
		const MessageInfo meta{id, time_ms, ++measurements_, "nabtools.bluetooth.hrm.measurement.v1"};
		// Protected engine dispatch keeps this object alive until return. Removal
		// invalidates the handle immediately and cancels subsequent outputs.
		const auto actions = actions_;
#if NABTOOLS_HRM_STRUCT
		if(actions.measurement.invoke)
		{
			const auto delivery = actions.measurement.invoke(actions.measurement.context, meta, *measurement);
			if(engine.device(id) != this) return Status::ok;

			if(delivery == Delivery::accepted) ++delivered_; else ++dropped_;
		}

#endif
#if NABTOOLS_HRM_CBOR
		if(actions.cbor.invoke)
		{
			std::array<uint8_t, 2048> output; CborWriter writer(output);
			writer.map(11);
			writer.text("type"); writer.text("measurement");
			writer.text("raw"); writer.bytes(measurement->raw());
			writer.text("flags"); writer.number(measurement->flags());
			writer.text("reserved_flags"); writer.number(measurement->reserved_flags());
			writer.text("standard_layout"); writer.boolean(measurement->standard_layout());
			writer.text("heart_rate_bpm"); writer.number(measurement->bpm());
			writer.text("contact_supported"); writer.boolean(measurement->contact_supported());
			writer.text("contact_detected");
			if(auto contact = measurement->contact_detected()) writer.boolean(*contact); else writer.null();

			writer.text("energy_kj");
			if(auto energy = measurement->energy_kj()) writer.number(*energy); else writer.null();

			writer.text("rr_1_1024_s"); writer.array(measurement->rr_count());
			for(size_t i = 0; i < measurement->rr_count(); ++i)
				writer.number(*measurement->rr_1_1024_s(i));
			writer.text("unknown_tail"); writer.bytes(measurement->unknown_tail());
			const auto delivery = writer.good() ? actions.cbor.invoke(actions.cbor.context, meta,
			        writer.result()) : Delivery::error;
			if(engine.device(id) != this) return Status::ok;

			if(delivery == Delivery::accepted) ++delivered_; else ++dropped_;
		}

#endif
		return Status::ok;
	}

	std::unique_ptr<IDevice> HeartRateModule::create(AdvertisementView view)const
	{
		if(!recognize(view)) return {};

		return std::make_unique<HeartRateDevice>(view.address, actions_);
	}

	DeviceId HeartRateModule::add_known(BtEngine &engine, Address address)const
	{
		if(auto existing = engine.find(address, protocol); existing.valid()) return existing;

		return engine.add_device(std::make_unique<HeartRateDevice>(address, actions_));
	}

}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/devices/samsung/smarttag/smarttag.hpp"
#if NABTOOLS_SMARTTAG_CBOR
#include "NaBTools/core/cbor.hpp"
#endif
#if NABTOOLS_SIG_PROFILE
#include "NaBTools/catalogs/sig/sig.hpp"
#endif
#include <string_view>

namespace NaBTools::runtime
{
	namespace
	{
// Protocol identifiers only; no Samsung UUIDs belong in BtEngine or the backend.
// Observed on original SmartTags, not a claim about every model/firmware:
// https://arxiv.org/pdf/2210.14702 (Appendix C)
// https://github.com/pogen300/galaxy_smarttag_research/tree/main/investigation/web_traffic_analysis/command_set
		constexpr Uuid uuid(std::string_view text)
		{
			Uuid out; size_t n = 0; uint8_t byte = 0;
			for(char c:text)
			{
				if(c == '-')
					continue;

				const auto v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10;
				byte = static_cast<uint8_t>((byte << 4) | v); if(n++ % 2) out.bytes[16 - n / 2] = byte;
			}
			return out;
		}

		struct Definition {Uuid uuid; const char *name;};
		constexpr Definition definitions[]{
			// Use the standard Model Number String. The paper assigns d19ddd83 both
			// MODEL_NAME (Table 12) and Privacy ID Seed (Table 13); do not guess/read it
			// as plaintext model information on an unauthenticated registered tag.
			{Uuid::from16(0x2a24), "model_name"},
			{uuid("30c48d2a-6ccb-4240-9f97-7f97a3f1c030"), "firmware_version"},
			{uuid("50f98bfd-158c-4efa-add4-0a70c2f5df5d"), "supported_cipher"},
			{uuid("dee30001-182d-5496-b1ad-14f216324184"), "owner_alarm"},
			{uuid("dee30003-182d-5496-b1ad-14f216324184"), "tag_button"},
			{uuid("a12be31c-5b38-4773-9b9d-3d5735233a7c"), "nonce"},
			{uuid("4ebe81f6-b952-465e-9ece-5ca39d4e8955"), "encrypted_nonce"}
		};
		constexpr const char *auth_reason = "Authenticated owner session required";
		const char * field_name(Uuid value, GattEventKind kind)
		{
			for(const auto &d:definitions)
				if(d.uuid == value) return d.name;

			if(value == uuid("d19ddd83-bbe1-4144-bb18-f3ceb57c480a")) return "model_or_privacy_seed_unverified";

			if(value == Uuid::from16(0xfd5a)) return "command_service";

			if(value == Uuid::from16(0xfd59)) return "onboarding_service";

			if(value == uuid("eedd5e73-6aa8-4673-8219-398a489da87c")) return "authentication_service";

			if(value == Uuid::from16(0x2902)) return "client_configuration";

#if NABTOOLS_SIG_PROFILE
			const auto short_id = uint16_t(value.bytes[12]) | (uint16_t(value.bytes[13]) << 8);
			if(value == Uuid::from16(short_id))
			{
				const char *label = kind == GattEventKind::service ? sig::service(short_id) : kind ==
				    GattEventKind::characteristic ? sig::characteristic(short_id) : nullptr;
				if(label) return label;
			}

#else
			(void)kind;
#endif
			return "unknown";
		}

	}
	const char * SmartTagDevice::connection_state()const
	{
		if(owner_step_ != OwnerStep::none)
			return owner_step_ <= OwnerStep::proof_wait ? "authenticating" : "writing_command";

		switch(state_)
		{
		case State::disconnected: return "disconnected"; case State::connecting: return "connecting";
		case State::discovering: return "discovering"; case State::connected: return "connected";
		case State::reading: return "reading_info"; case State::subscribing: return "subscribing_raw";
		case State::disconnecting: return "disconnecting";
		}
		return "unknown";
	}

	InteractionInfo SmartTagDevice::interaction()const
	{
#if NABTOOLS_DEVICE_DESCRIPTIONS
		const bool ready = gatt_ && gatt_->stats().ready,
		    connected = session_.valid() && state_ == State::connected && owner_step_ == OwnerStep::none;
		const bool authenticated = owner_session_.authenticated();
		const auto availability = !ready ? Status::unsupported : connected ? Status::ok : Status::busy;
		const char *reason =
		    !ready ? "GATT backend not ready" : !connected ? "A connected idle session is required" : nullptr;
		const bool raw = connected && attributes_[4].handle && attributes_[4].cccd &&
		    (attributes_[4].properties & 0x30);
		commands_ = {{{"connection.connect", "Connect",
			!ready ? Status::unsupported : session_.valid() ? Status::busy : Status::ok,
			!ready ? "GATT backend not ready" : session_.valid() ? "Session already exists" : nullptr},
			{"connection.disconnect", "Disconnect",
			 !ready ? Status::unsupported : !session_.valid() ? Status::invalid : state_ ==
			 State::disconnecting ? Status::busy : Status::ok, "Requires a live session"},
			{"gatt.discover", "Discover services",
			 (button_subscribed_ || owner_attempted_) ? Status::busy : availability,
			 (button_subscribed_ ||
			 owner_attempted_) ? "Disconnect before rediscovery with active subscriptions" : reason},
			{"device.read_info", "Read model / firmware / cipher", availability, reason},
			{"button.observe_raw", "Observe raw button indications",
			 !ready ? Status::unsupported : raw && !button_subscribed_ ? Status::ok : Status::busy,
			 !ready ? reason : button_subscribed_ ? "Already subscribed" : !raw ?
			 "Button characteristic and CCCD not discovered in an idle session" :
			 "Encrypted indications remain raw until authenticated"},
			{"owner.authenticate", "Authenticate owner",
			 !owner_access_ ? Status::unsupported : !connected || owner_attempted_ ? Status::busy : Status::ok,
			 !owner_access_ ? "Application owner-key provider / crypto not configured" : owner_attempted_ ?
			 "Disconnect before a new authentication attempt" : reason},
			{"button.observe", "Enable button events",
			 authenticated && raw && !button_enabled_ ? Status::ok : Status::busy,
			 authenticated ? reason : auth_reason},
			{"alert.start", "Start sound",
			 authenticated && connected && attributes_[3].handle ? Status::ok : Status::unsupported,
			 authenticated ? reason : auth_reason},
			{"alert.stop", "Stop sound",
			 authenticated && connected && attributes_[3].handle ? Status::ok : Status::unsupported,
			 authenticated ? reason : auth_reason}}};
		events_ = {{{"advertisement", "Advertising payload changed"}, {"gatt", "GATT discovery and connection state"},
			{"button.raw", "Raw button indication", button_subscribed_ ? Status::ok : Status::busy,
			 "Raw bytes are not decoded button presses"},
			{"button.press", "Button press / hold / double press",
			 authenticated && button_enabled_ ? Status::ok : Status::busy,
			 authenticated ? "Enable button events" : auth_reason}}};
		return {commands_, events_};
#else
		return {};
#endif
	}

	void SmartTagDevice::on_attached(BtEngine &engine, DeviceId)
	{
		gatt_ = engine.gatt();
	}

	Status SmartTagDevice::read_next()
	{
		for(; read_index_ < 3; ++read_index_)
		{
			const auto &a = attributes_[read_index_]; if(!a.handle || !(a.properties & 2)) continue;

			const auto result = gatt_->read(session_, a.handle, flow_request_); if(result == Status::ok)
			{
				state_ = State::reading; return result;
			}

			return result;
		}
		state_ = State::connected; return Status::ok;
	}

	Status SmartTagDevice::invoke(BtEngine &engine, DeviceId id, const DeviceCommand &command)
	{
		if(!gatt_ || !gatt_->stats().ready) return Status::unsupported;

		if(!((command.format.empty() && command.arguments.empty()) ||
		    ((command.format == "application/cbor" || command.format == "cbor") && command.arguments.size() == 1 &&
		    command.arguments[0] == 0xa0))) return Status::invalid;

		Status result = Status::unsupported;
		if(command.key == "connection.connect")
		{
			if(session_.valid()) return Status::busy;

			flow_request_ = command.request_id; attributes_ = {}; button_subscribed_ = false; reset_owner();
			result = gatt_->connect(id, address_, flow_request_, session_);
			if(result == Status::ok) state_ = State::connecting; else session_ = {};
		}
		else if(command.key == "connection.disconnect")
		{
			if(!session_.valid()) return Status::invalid;

			if(state_ == State::disconnecting) return Status::busy;

			result = gatt_->disconnect(session_, command.request_id); if(result == Status::ok)
			{
				reset_owner(); flow_request_ = command.request_id; state_ = State::disconnecting;
			}
		}
		else if(command.key == "gatt.discover")
		{
			if(state_ != State::connected || !session_.valid() || button_subscribed_ || owner_attempted_ ||
			    owner_step_ != OwnerStep::none) return Status::busy;

			flow_request_ = command.request_id; attributes_ = {}; button_subscribed_ = false;
			result = gatt_->discover(session_, std::nullopt, flow_request_); if(result == Status::ok)
				state_ = State::discovering;
		}
		else if(command.key == "device.read_info")
		{
			if(state_ != State::connected || !session_.valid() || owner_step_ != OwnerStep::none) return Status::busy;

			flow_request_ = command.request_id; read_index_ = 0; result = read_next(); if(result != Status::ok)
				state_ = State::connected;
		}
		else if(command.key == "button.observe_raw")
		{
			if(state_ != State::connected || button_subscribed_ || owner_step_ != OwnerStep::none) return Status::busy;

			const auto &a = attributes_[4]; if(!session_.valid() || !a.handle || !a.cccd || !(a.properties & 0x30))
				return Status::unsupported;

			flow_request_ = command.request_id;
			result = gatt_->subscribe(session_, a.handle, a.cccd, bool(a.properties & 0x20), flow_request_);
			if(result == Status::ok) state_ = State::subscribing;
		}
		else
		{
			result = invoke_owner(engine, id, command);
			if(engine.device(id) != this) return result;
		}

		GattEvent event; event.kind = GattEventKind::write; event.status = result;
		event.request_id = command.request_id; event.time_ms = command.time_ms;
		// A command acceptance is explicitly separate from a radio completion.
		event.owner = id; event.session = session_;
		emit_gatt(engine, id, event, result == Status::ok ? "command.accepted" : "command.rejected"); return result;
	}

	void SmartTagDevice::emit_gatt(BtEngine &engine, DeviceId id, const GattEvent &event, const char *event_name)
	{
		const MessageInfo meta{id, event.time_ms, ++gatt_sequence_, "nabtools.samsung.smarttag.gatt.v1"};
		const auto actions = actions_;
		Uuid actual_uuid = event.uuid; auto properties = event.properties;
		const char *field = field_name(actual_uuid, event.kind);
		for(size_t i = 0; i < attributes_.size(); ++i)
			if(event.handle && event.handle == attributes_[i].handle)
			{
				field = definitions[i].name; actual_uuid = definitions[i].uuid; properties = attributes_[i].properties;
				break;
			}

#if NABTOOLS_SMARTTAG_STRUCT
		if(actions.gatt.invoke)
		{
			const auto result = actions.gatt.invoke(actions.gatt.context, meta,
			        {connection_state(), field, event_name ? event_name : name(event.kind), event});
			if(engine.device(id) != this) return;

			if(result == Delivery::accepted) ++delivered_; else ++dropped_;}

#endif
#if NABTOOLS_SMARTTAG_CBOR
		if(actions.cbor.invoke)
		{
			std::array<uint8_t, 1280> bytes; CborWriter w(bytes); w.map(16);
			w.text("type");
			w.text(event.kind == GattEventKind::notification &&
			    event.handle == attributes_[4].handle ? "button.raw" : "gatt");
			w.text("state"); w.text(connection_state()); w.text("event");
			w.text(event_name ? event_name : name(event.kind));
			w.text("status"); w.text(name(event.status)); w.text("error"); w.integer(event.error);
			w.text("field"); w.text(field); w.text("uuid"); w.text(actual_uuid.text().data());
			w.text("handle"); w.number(event.handle); w.text("parent_handle"); w.number(event.parent_handle);
			w.text("end_handle"); w.number(event.end_handle); w.text("properties"); w.number(properties);
			w.text("request_id"); w.number(event.request_id); w.text("raw");
			w.bytes((event.handle == attributes_[5].handle ||
			    event.handle == attributes_[6].handle) ? Bytes{} : event.value());
			w.text("value_size"); w.number(event.size);
			w.text("authenticated"); w.boolean(owner_session_.authenticated()); w.text("button_decoded");
			w.boolean(false);
			const auto result = w.good() ? actions.cbor.invoke(actions.cbor.context, meta,
			        w.result()) : Delivery::error;
			if(engine.device(id) != this) return;

			if(result == Delivery::accepted) ++delivered_; else ++dropped_;
		}

#endif
	}

	void SmartTagDevice::on_gatt(BtEngine &engine, DeviceId id, const GattEvent &event)
	{
		if(event.owner != id || !session_.valid() || event.session != session_) return;

		if(event.size > GattEvent::capacity)
		{
			if(owner_step_ != OwnerStep::none || owner_session_.authenticated())
			{
				owner_fault(engine, id, event); return;
			}

			auto report = event; report.size = 0; report.status = Status::invalid; report.kind = GattEventKind::error;
			emit_gatt(engine, id, report); return;
		}

		last_seen_ = event.time_ms;
		if(event.kind == GattEventKind::disconnected)
		{
			session_ = {}; state_ = State::disconnected; attributes_ = {}; button_subscribed_ = false; reset_owner();
			emit_gatt(engine, id, event); return;
		}

		if(state_ == State::disconnecting){emit_gatt(engine, id, event); return;}

		if(on_owner_gatt(engine, id, event)) return;

		if(event.kind == GattEventKind::connected)
		{
			if(state_ != State::connecting) return;

			if(event.status != Status::ok)
			{
				session_ = {}; state_ = State::disconnected; emit_gatt(engine, id, event); return;
			}

			const auto result = gatt_->discover(session_, std::nullopt, flow_request_);
			state_ = result == Status::ok ? State::discovering : State::connected;
			auto report = event; if(result != Status::ok)
			{
				report.status = result; report.kind = GattEventKind::error;
			}

			emit_gatt(engine, id, report); return;
		}

		if(event.kind == GattEventKind::characteristic && state_ == State::discovering &&
		    event.request_id == flow_request_)
		{
			for(size_t i = 0; i < attributes_.size(); ++i)
				if(event.uuid == definitions[i].uuid) attributes_[i] = {event.handle, 0, event.properties};
		}
		else if(event.kind == GattEventKind::descriptor && state_ == State::discovering &&
		    event.request_id == flow_request_ && event.uuid == Uuid::from16(0x2902))
		{
			for(auto &a:attributes_)
				if(a.handle == event.parent_handle) a.cccd = event.handle;
		}
		else if(event.kind == GattEventKind::discovery_complete && state_ == State::discovering &&
		    event.request_id == flow_request_)
		{
			state_ = State::connected;
		}
		else if(event.kind == GattEventKind::read && state_ == State::reading && event.request_id == flow_request_ &&
		    event.handle == attributes_[read_index_].handle)
		{
			// Deliver this value before issuing the next read, preserving ownership.
			emit_gatt(engine, id, event);
			if(engine.device(id) != this || state_ != State::reading || event.request_id != flow_request_) return;

			++read_index_; const auto result = read_next(); if(result != Status::ok)
			{
				state_ = State::connected; auto report = event; report.kind = GattEventKind::error;
				report.status = result; emit_gatt(engine, id, report);
			}

			return;
		}
		else if(event.kind == GattEventKind::subscribed && state_ == State::subscribing &&
		    event.request_id == flow_request_ && event.handle == attributes_[4].handle)
		{
			state_ = State::connected; button_subscribed_ = event.status == Status::ok;
		}
		else if(event.kind == GattEventKind::error && event.request_id == flow_request_)
		{
			if(state_ == State::connecting){session_ = {}; state_ = State::disconnected;}else state_ = State::connected;
		}

		emit_gatt(engine, id, event);
	}

}

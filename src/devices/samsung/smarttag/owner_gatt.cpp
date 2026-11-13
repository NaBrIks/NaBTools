// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/devices/samsung/smarttag/smarttag.hpp"
#if NABTOOLS_SMARTTAG_CBOR
#include "NaBTools/core/cbor.hpp"
#endif

namespace NaBTools::runtime
{
	void SmartTagDevice::reset_owner()
	{
		owner_session_.reset(); owner_step_ = OwnerStep::none;
		owner_attempted_ = nonce_seen_ = proof_seen_ = button_enabled_ = false; smarttag_erase(owner_proof_);
	}

	void SmartTagDevice::owner_fault(BtEngine &engine, DeviceId id, const GattEvent &event)
	{
		owner_session_.reset(); owner_step_ = OwnerStep::none; button_enabled_ = false; smarttag_erase(owner_proof_);
		// A failed/ambiguous encrypted write must not reuse a session counter.
		const auto result = gatt_->disconnect(session_, flow_request_);
		state_ = result == Status::ok ? State::disconnecting : State::connected;
		auto report = event; report.kind = GattEventKind::error; report.status = Status::invalid; report.size = 0;
		emit_gatt(engine, id, report, "owner.failed");
	}

	Status SmartTagDevice::write_owner_command(size_t index, uint8_t opcode, Bytes arguments)
	{
		const auto &attribute = attributes_[index];
		if(!attribute.handle || !(attribute.properties & 8)) return Status::unsupported;

		std::array<uint8_t, 16> encrypted{};
		if(!owner_session_.command(opcode, arguments, encrypted)) return Status::invalid;

		const auto result = gatt_->write(session_, attribute.handle, encrypted, true, flow_request_);
		smarttag_erase(encrypted); return result;
	}

	Status SmartTagDevice::invoke_owner(BtEngine &engine, DeviceId id, const DeviceCommand &command)
	{
		if(command.key != "owner.authenticate" && command.key != "button.observe" && command.key != "alert.start" &&
		    command.key != "alert.stop") return Status::unsupported;

		if(command.key != "owner.authenticate" && !owner_session_.authenticated()) return Status::unsupported;

		if(!session_.valid() || state_ != State::connected || owner_step_ != OwnerStep::none) return Status::busy;

		flow_request_ = command.request_id;
		Status result;
		if(command.key == "owner.authenticate")
		{
			if(!owner_access_) return Status::unsupported;

			if(owner_attempted_) return Status::busy;

			for(size_t i = 5; i < 7; ++i)
				if(!attributes_[i].handle || !attributes_[i].cccd || !(attributes_[i].properties & 0x20) ||
				    !(attributes_[i].properties & 8))
					return Status::unsupported;

			std::array<uint8_t, 16> master{};
			const bool loaded = owner_access_.load(owner_access_.context, id, address_, master);
			if(engine.device(id) != this){smarttag_erase(master); return Status::invalid;}

			const bool begun = loaded && owner_session_.begin(owner_access_.crypto, master); smarttag_erase(master);
			if(!begun) return Status::invalid;

			owner_attempted_ = true; nonce_seen_ = proof_seen_ = false; owner_step_ = OwnerStep::sub_nonce;
			result = gatt_->subscribe(session_, attributes_[5].handle, attributes_[5].cccd, true, flow_request_);
		}
		else if(command.key == "button.observe")
		{
			const auto &button = attributes_[4];
			if(button_enabled_) return Status::busy;

			if(!button.handle || !button.cccd || !(button.properties & 0x20) || !(button.properties & 8))
				return Status::unsupported;

			if(!button_subscribed_)
			{
				owner_step_ = OwnerStep::button_sub;
				result = gatt_->subscribe(session_, button.handle, button.cccd, true, flow_request_);
			}
			else
			{
				owner_step_ = OwnerStep::press_write; const uint8_t enabled = 1;
				result = write_owner_command(4, 0, Bytes(&enabled, 1));
			}
		}
		else
		{
			if(!attributes_[3].handle || !(attributes_[3].properties & 8)) return Status::unsupported;

			owner_step_ = OwnerStep::alarm_write;
			result = write_owner_command(3, command.key == "alert.start" ? 1 : 0, {});
		}

		if(result != Status::ok)
		{
			GattEvent event; event.owner = id; event.session = session_; event.request_id = flow_request_;
			event.time_ms = command.time_ms; owner_fault(engine, id, event);
		}

		return result;
	}

	bool SmartTagDevice::on_owner_gatt(BtEngine &engine, DeviceId id, const GattEvent &event)
	{
		if(owner_step_ != OwnerStep::none && event.kind == GattEventKind::error && event.request_id == flow_request_)
		{
			owner_fault(engine, id, event); return true;
		}

		if(event.kind == GattEventKind::notification && event.handle == attributes_[4].handle &&
		    owner_session_.authenticated())
		{
			uint32_t counter{}; uint8_t action{};
			const bool decoded = button_enabled_ && owner_session_.button(event.value(), counter, action);
			emit_gatt(engine, id, event, decoded ? "button.decoded" : "button.raw");
			if(decoded && engine.device(id) == this) emit_button(engine, id, event, counter, action);

			return true;
		}

		const bool nonce = event.kind == GattEventKind::notification && event.handle == attributes_[5].handle;
		const bool proof = event.kind == GattEventKind::notification && event.handle == attributes_[6].handle;
		if(nonce || proof)
		{
			bool ok = false;
			if(nonce && (owner_step_ == OwnerStep::nonce_write || owner_step_ == OwnerStep::nonce_wait) && !nonce_seen_)
			{
				ok = owner_session_.challenge(event.value(), owner_proof_); nonce_seen_ = ok;
				if(ok && owner_step_ == OwnerStep::nonce_wait)
				{
					owner_step_ = OwnerStep::proof_write;
					ok = gatt_->write(session_, attributes_[6].handle, owner_proof_, true, flow_request_) == Status::ok;
					smarttag_erase(owner_proof_);
				}
			}
			else if(proof && (owner_step_ == OwnerStep::proof_write || owner_step_ == OwnerStep::proof_wait) &&
			    !proof_seen_)
			{
				emit_gatt(engine, id, event, "owner.proof_received");
				if(engine.device(id) != this) return true;

				ok = owner_session_.verify(event.value()); proof_seen_ = ok;
				if(ok && owner_step_ == OwnerStep::proof_wait) owner_step_ = OwnerStep::none;
			}

			if(!ok){owner_fault(engine, id, event); return true;}

			emit_gatt(engine, id, event, owner_step_ == OwnerStep::none ? "owner.authenticated" : "owner.progress");
			return true;
		}

		if(owner_step_ == OwnerStep::none || event.request_id != flow_request_) return false;

		Status result = Status::ok; bool handled = true; const char *name = "owner.progress";
		const auto subscribed = [&](size_t index){
				return event.kind == GattEventKind::subscribed && event.handle == attributes_[index].handle;
			};
		const auto written = [&](size_t index){
				return event.kind == GattEventKind::write && event.handle == attributes_[index].handle;
			};
		if(owner_step_ == OwnerStep::sub_nonce && subscribed(5))
		{
			if(event.status != Status::ok){owner_fault(engine, id, event); return true;}

			owner_step_ = OwnerStep::sub_proof;
			result = gatt_->subscribe(session_, attributes_[6].handle, attributes_[6].cccd, true, flow_request_);
		}
		else if(owner_step_ == OwnerStep::sub_proof && subscribed(6))
		{
			if(event.status != Status::ok){owner_fault(engine, id, event); return true;}

			owner_step_ = OwnerStep::nonce_write;
			result = gatt_->write(session_, attributes_[5].handle, owner_session_.nonce(), true, flow_request_);
		}
		else if(owner_step_ == OwnerStep::nonce_write && written(5))
		{
			if(event.status != Status::ok){owner_fault(engine, id, event); return true;}

			owner_step_ = OwnerStep::nonce_wait;
			if(nonce_seen_)
			{
				owner_step_ = OwnerStep::proof_write;
				result = gatt_->write(session_, attributes_[6].handle, owner_proof_, true, flow_request_);
				smarttag_erase(owner_proof_);
			}
		}
		else if(owner_step_ == OwnerStep::proof_write && written(6))
		{
			if(event.status != Status::ok){owner_fault(engine, id, event); return true;}

			owner_step_ = proof_seen_ ? OwnerStep::none : OwnerStep::proof_wait;
			if(proof_seen_) name = "owner.authenticated";
		}
		else if(owner_step_ == OwnerStep::button_sub && subscribed(4))
		{
			if(event.status != Status::ok){owner_fault(engine, id, event); return true;}

			button_subscribed_ = true; owner_step_ = OwnerStep::press_write; const uint8_t enabled = 1;
			result = write_owner_command(4, 0, Bytes(&enabled, 1));
		}
		else if(owner_step_ == OwnerStep::press_write && written(4))
		{
			if(event.status != Status::ok){owner_fault(engine, id, event); return true;}

			owner_step_ = OwnerStep::hold_write; const uint8_t enabled = 1;
			result = write_owner_command(4, 1, Bytes(&enabled, 1));
		}
		else if(owner_step_ == OwnerStep::hold_write && written(4))
		{
			if(event.status != Status::ok){owner_fault(engine, id, event); return true;}

			button_enabled_ = true; owner_step_ = OwnerStep::none; name = "button.enabled";
		}
		else if(owner_step_ == OwnerStep::alarm_write && written(3))
		{
			if(event.status != Status::ok){owner_fault(engine, id, event); return true;}

			owner_step_ = OwnerStep::none; name = "command.written";
		}
		else handled = false;

		if(!handled) return false;

		if(result != Status::ok){owner_fault(engine, id, event); return true;}

		emit_gatt(engine, id, event, name); return true;
	}

	void SmartTagDevice::emit_button(BtEngine &engine, DeviceId id, const GattEvent &event, uint32_t counter,
	    uint8_t action)
	{
		const char *label = action == 1 ? "press" : action == 2 ? "hold" : "double_press";
		const MessageInfo meta{id, event.time_ms, ++gatt_sequence_, "nabtools.samsung.smarttag.button.v1"};
		const auto actions = actions_;
#if NABTOOLS_SMARTTAG_STRUCT
		if(actions.button.invoke)
		{
			const auto result = actions.button.invoke(actions.button.context, meta, {counter, action, label});
			if(engine.device(id) != this) return;

			if(result == Delivery::accepted) ++delivered_; else ++dropped_;}

#endif
#if NABTOOLS_SMARTTAG_CBOR
		if(actions.cbor.invoke)
		{
			std::array<uint8_t, 160> data; CborWriter w(data); w.map(5);
			w.text("type"); w.text("button"); w.text("action"); w.text(label); w.text("counter"); w.number(counter);
			w.text("authenticated"); w.boolean(true); w.text("button_decoded"); w.boolean(true);
			const auto result = w.good() ? actions.cbor.invoke(actions.cbor.context, meta,
			        w.result()) : Delivery::error;
			if(engine.device(id) != this) return;

			if(result == Delivery::accepted) ++delivered_; else ++dropped_;
		}

#endif
	}

}

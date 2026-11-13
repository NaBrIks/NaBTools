// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/bt/engine.hpp"
#include <cstring>

namespace NaBTools::runtime
{
	BtEngine::~BtEngine()
	{
		if(auto *client = gatt())
			for(size_t i = 0; i < devices_.size(); ++i)
				if(devices_[i].device)
					client->cancel({static_cast<uint16_t>(i), devices_[i].generation});
	}

	const char * name(Status s)
	{
		switch(s)
		{
		case Status::ok: return "ok"; case Status::busy: return "busy"; case Status::invalid: return "invalid";
		case Status::full: return "full"; case Status::expired: return "expired";
		case Status::unsupported: return "unsupported";
		case Status::backend_error: return "backend_error";
		}
		return "unknown";
	}

	const char * name(ScanState s)
	{
		switch(s)
		{
		case ScanState::idle: return "idle"; case ScanState::starting: return "starting";
		case ScanState::scanning: return "scanning"; case ScanState::stopping: return "stopping";
		case ScanState::stopped: return "stopped"; case ScanState::fault: return "fault";
		}
		return "unknown";
	}

	const char * name(ScanMode mode)
	{
		return mode == ScanMode::active ? "active" : "passive";
	}

	Status BtEngine::add_module(std::unique_ptr<IModule> m)
	{
		if(started_) return Status::busy;

		if(!m || !m->key() || !*m->key()) return Status::invalid;

		for(const auto &current:modules_)
			if(std::strcmp(current->key(), m->key()) == 0) return Status::invalid;

		modules_.push_back(std::move(m)); return Status::ok;
	}

	Status BtEngine::start()
	{
		if(started_) return Status::busy;

		if(!backend_) return Status::invalid;

		const auto result = backend_->start();
		if(result == Status::ok) started_ = true;

		return result;
	}

	void BtEngine::reap()
	{
		for(size_t i = 0; i < devices_.size(); ++i)
		{
			auto &s = devices_[i]; if(s.removing)
			{
				if(auto *client = gatt()) client->cancel({static_cast<uint16_t>(i), s.generation});

				s.device.reset(); s.removing = false;
			}
		}
	}

	DeviceId BtEngine::add_device(std::unique_ptr<IDevice> d)
	{
		if(!d) return {};

		for(size_t i = 0; i < devices_.size(); ++i)
		{
			auto &s = devices_[i];
			if(s.device || s.removing) continue;

			if(++s.generation == 0) ++s.generation;

			s.device = std::move(d); const DeviceId id{static_cast<uint16_t>(i), s.generation};
			const bool outer = dispatching_; dispatching_ = true; s.device->on_attached(*this, id);
			dispatching_ = outer;
			if(!outer){reap();}

			return device(id) ? id : DeviceId{};
		}
		return {};
	}

	IDevice * BtEngine::device(DeviceId id)
	{
		if(id.slot >= devices_.size()) return nullptr;

		auto &s = devices_[id.slot];
		return s.generation == id.generation && !s.removing ? s.device.get() : nullptr;
	}

	Status BtEngine::remove_device(DeviceId id)
	{
		if(id.slot >= devices_.size()) return Status::expired;

		auto &s = devices_[id.slot];
		if(s.generation != id.generation || !s.device) return Status::expired;

		s.removing = true;
		if(!dispatching_) reap();

		return Status::ok;
	}

	DeviceId BtEngine::find(Address address, const char *protocol) const
	{
		for(size_t i = 0; i < devices_.size(); ++i)
		{
			const auto &s = devices_[i];
			if(!s.device || s.removing) continue;

			const auto info = s.device->info();
			if(info.address == address && std::strcmp(info.protocol, protocol) == 0)
				return {static_cast<uint16_t>(i), s.generation};
		}
		return {};
	}

	std::vector<DeviceInfo> BtEngine::devices() const
	{
		std::vector<DeviceInfo> out;
		for(size_t i = 0; i < devices_.size(); ++i)
		{
			const auto &s = devices_[i]; if(!s.device || s.removing) continue;

			auto info = s.device->info(); info.id = {static_cast<uint16_t>(i), s.generation}; out.push_back(info);
		}
		return out;
	}

	Status BtEngine::scan(bool enabled)
	{
		return started_ ? backend_->scan(enabled) : Status::invalid;
	}

	Status BtEngine::set_scan_mode(ScanMode mode)
	{
		if(!backend_ || (mode != ScanMode::passive && mode != ScanMode::active)) return Status::invalid;

		return backend_->set_scan_mode(mode);
	}

	Status BtEngine::dispatch(DeviceId id, void *context, Status (*handler)(void *, IDevice &))
	{
		if(!started_) return Status::invalid;

		if(dispatching_ || polling_) return Status::busy;

		auto *target = device(id); if(!target) return Status::expired;

		if(!handler) return Status::invalid;

		dispatching_ = true; const auto result = handler(context, *target); dispatching_ = false; reap();
		return result;
	}

	InteractionInfo BtEngine::interaction(DeviceId id)const
	{
		if(id.slot >= devices_.size()) return {};

		const auto &s = devices_[id.slot];
		return s.generation == id.generation && s.device && !s.removing ? s.device->interaction() : InteractionInfo{};
	}

	Status BtEngine::invoke(DeviceId id, const DeviceCommand &command)
	{
		if(!started_) return Status::invalid;

		if(dispatching_ || polling_) return Status::busy;

		auto *target = device(id); if(!target) return Status::expired;

		if(command.key.empty() || command.key.size() > 47 || !command.request_id) return Status::invalid;

		bool known = false;
		for(const auto &op:target->interaction().commands)
			if(op.key && command.key == op.key)
			{
				if(op.status != Status::ok) return op.status;

				known = true; break;
			}

		if(!known) return Status::unsupported;

		struct Context {BtEngine &engine; DeviceId id; const DeviceCommand &command;} context{*this, id, command};
		return dispatch(id, &context, [](void *ptr, IDevice &device){
			auto &c = *static_cast<Context *>(ptr); return device.invoke(c.engine, c.id, c.command);
		});
	}

	std::vector<HandlerMatch> BtEngine::recognize(AdvertisementView view)const
	{
		std::vector<HandlerMatch> out;
		if(validate(view) != AdStatus::complete) return out;

		for(const auto &m:modules_)
			if(auto match = m->recognize(view)) out.push_back({m->key(), *match});

		return out;
	}

	Status BtEngine::attach(const char *key, AdvertisementView view, DeviceId &out)
	{
		out = {}; if(!key || validate(view) != AdStatus::complete) return Status::invalid;

		for(const auto &m:modules_)
			if(std::strcmp(m->key(), key) == 0)
			{
				const auto match = m->recognize(view); if(!match) return Status::unsupported;

				if(!match->label || !*match->label) return Status::invalid;

				if(auto existing = find(view.address, match->label); existing.valid())
				{
					out = existing; return Status::ok;
				}

				auto candidate = m->create(view); if(!candidate) return Status::unsupported;

				const auto info = candidate->info();
				if(auto existing = find(info.address, info.protocol); existing.valid())
				{
					out = existing; return Status::ok;
				}

				out = add_device(std::move(candidate)); return out.valid() ? Status::ok : Status::full;
			}

		return Status::unsupported;
	}

	Status BtEngine::poll(size_t budget)
	{
		if(!started_) return Status::invalid;

		if(polling_ || dispatching_) return Status::busy;

		polling_ = true; backend_->pump();
		if(auto *client = gatt())
		{
			GattEvent event;
			for(size_t n = 0; n < budget && client->take(event); ++n)
			{
				if(auto *target = device(event.owner))
				{
					dispatching_ = true; target->on_gatt(*this, event.owner, event); dispatching_ = false; reap();
				}
			}
		}

		Advertisement owned;
		for(size_t n = 0; n < budget && backend_->take(owned); ++n)
		{
			auto view = owned.view(); const auto validity = validate(view);
			++counters_.observed;
			if(validity == AdStatus::malformed) ++counters_.malformed;

			if(validity == AdStatus::fragment) ++counters_.fragments;

			dispatching_ = true;
			for(auto &module:modules_)
				module->observe(*this, view, validity);
			// Include discoveries made by modules. Devices attached from a device
			// callback start receiving on the NEXT packet, never recursively.
			std::array<DeviceId, max_devices> targets{}; size_t count = 0;
			if(validity == AdStatus::complete)
				for(size_t i = 0; i < devices_.size(); ++i)
					if(devices_[i].device && !devices_[i].removing)
						targets[count++] = {static_cast<uint16_t>(i), devices_[i].generation};

			for(size_t i = 0; i < count; ++i)
				if(auto *d = device(targets[i])) d->on_advertisement(*this, targets[i], view);

			dispatching_ = false; reap();
		}
		polling_ = false; return Status::ok;
	}

}

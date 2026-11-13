// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/gap/advertisement.hpp"
#include "NaBTools/bt/interaction.hpp"
#include "NaBTools/bt/gap/classification.hpp"
#include "NaBTools/bt/gatt/client.hpp"
#include <array>
#include <memory>
#include <vector>

namespace NaBTools::runtime
{
	enum class ScanState { idle, starting, scanning, stopping, stopped, fault };
	const char *name(ScanState);
	enum class ScanMode {passive, active};
	const char *name(ScanMode);
	struct BackendSnapshot {
		ScanState state{ScanState::idle};
		int error{};
		uint32_t reports{}, dropped{};
		ScanMode scan_mode{ScanMode::passive}, requested_scan_mode{ScanMode::passive};
		bool scan_mode_pending{}, scan_mode_supported{};
	};
	class IBackend
	{
	public:
		virtual ~IBackend() = default;
		virtual Status start() = 0;
		virtual void pump() = 0;
		virtual Status scan(bool enabled) = 0;
		virtual Status set_scan_mode(ScanMode)
		{
			return Status::unsupported;
		}

		virtual bool take(Advertisement &) = 0;
		virtual BackendSnapshot snapshot() const = 0;
		virtual IGattClient * gatt()
		{
			return nullptr;
		}

	};
	struct DeviceInfo {
		DeviceId id;
		Address address;
		const char *protocol{};
		uint64_t last_seen_ms{};
		uint32_t packets{}, delivered{}, dropped{};
	};
	class BtEngine;
	struct Detection {const char *label{}; const char *evidence{}; bool decoded{}; DeviceKind kind{DeviceKind::unknown};
	};
	struct HandlerMatch {const char *key{}; Detection detection;};
	class IDevice
	{
	public:
		virtual ~IDevice() = default;
		virtual DeviceInfo info() const = 0;
		virtual void on_advertisement(BtEngine &, DeviceId, AdvertisementView) = 0;
		virtual void on_attached(BtEngine &, DeviceId)
		{
		}

		virtual void on_gatt(BtEngine &, DeviceId, const GattEvent &)
		{
		}

		virtual InteractionInfo interaction()const
		{
			return {};
		}

		// ok means accepted by the driver, not necessarily completed on the radio.
		// Completion/events use the driver's configured application Actions.
		virtual Status invoke(BtEngine &, DeviceId, const DeviceCommand &)
		{
			return Status::unsupported;
		}

	};
	class IModule
	{
	public:
		virtual ~IModule() = default;
		virtual const char * key() const = 0;
		virtual void observe(BtEngine &, AdvertisementView, AdStatus) = 0;
		// Optional recognizer/factory of this module. Recognition never creates a
		// device; admission remains an explicit application decision.
		virtual std::optional<Detection> recognize(AdvertisementView)const
		{
			return {};
		}

		virtual std::unique_ptr<IDevice> create(AdvertisementView)const
		{
			return {};
		}

	};
	struct EngineCounters { uint64_t observed{}, malformed{}, fragments{}; };
	class BtEngine
	{
	public:
		static constexpr size_t max_devices = 32;
	private:
		struct Slot { std::unique_ptr<IDevice> device; uint32_t generation{}; bool removing{}; };
		std::unique_ptr<IBackend> backend_;
		std::array<Slot, max_devices> devices_{};
		std::vector<std::unique_ptr<IModule> > modules_;
		bool started_{}, dispatching_{}, polling_{};
		EngineCounters counters_;
		void reap();
	public:
		explicit BtEngine(std::unique_ptr<IBackend> backend)
		  : backend_(std::move(backend))
		{
		}

		~BtEngine();
		BtEngine(const BtEngine &) = delete;
		BtEngine& operator=(const BtEngine &) = delete;
		Status add_module(std::unique_ptr<IModule>);
		Status start();
		Status poll(size_t budget = 32);
		Status scan(bool enabled);
		Status set_scan_mode(ScanMode);
		DeviceId add_device(std::unique_ptr<IDevice>);
		Status remove_device(DeviceId);
		std::vector<HandlerMatch> recognize(AdvertisementView)const;
		Status attach(const char *module_key, AdvertisementView, DeviceId &);
		IDevice * device(DeviceId); // borrowed; engine context only
		InteractionInfo interaction(DeviceId)const;
		Status invoke(DeviceId, const DeviceCommand &); // serialized engine context only
		// Synchronous ingress for optional protocol adapters; no wire format or SDK
		// dependency. Keeps the target alive through removal from application Actions.
		Status dispatch(DeviceId, void *context, Status (*handler)(void *, IDevice &));
		DeviceId find(Address, const char *protocol) const;
		std::vector<DeviceInfo> devices() const;
		BackendSnapshot backend() const
		{
			return backend_->snapshot();
		}

		IGattClient * gatt()const
		{
			return backend_ ? backend_->gatt() : nullptr;
		}

		EngineCounters counters() const
		{
			return counters_;
		}

	};
}

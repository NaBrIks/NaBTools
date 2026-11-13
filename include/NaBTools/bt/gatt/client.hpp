// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/core/device_id.hpp"
#include "NaBTools/core/status.hpp"
#include "NaBTools/core/uuid.hpp"
#include "NaBTools/bt/gap/advertisement.hpp"
#include <optional>

namespace NaBTools::runtime
{
	struct GattSessionId {
		uint8_t slot{255}; uint32_t generation{};
		bool valid()const
		{
			return slot != 255 && generation;
		}

		bool operator==(const GattSessionId &)const = default;
	};
	enum class GattEventKind {connected, disconnected, service, characteristic, descriptor,
		                      discovery_complete, subscribed, read, write, notification, error};
	const char *name(GattEventKind);
// Bounded owned event: SDK callback buffers never cross tasks as borrowed pointers.
	struct GattEvent {
		static constexpr size_t capacity = 512;
		GattEventKind kind{GattEventKind::error}; DeviceId owner; GattSessionId session;
		uint64_t request_id{}, time_ms{}; Status status{Status::ok}; int error{};
		Uuid uuid; uint16_t handle{}, end_handle{}, parent_handle{}, properties{};
		std::array<uint8_t, capacity> data{}; uint16_t size{};
		Bytes value()const
		{
			return {data.data(), size};
		}

	};
	struct GattStats {bool ready{}; uint32_t connections{}, dropped{};};
	class IGattClient
	{
	public:
		virtual ~IGattClient() = default;
		virtual GattStats stats()const = 0;
		// All commands are serialized engine-context calls. ok means accepted;
		// asynchronous results carry owner, session and request_id.
		virtual Status connect(DeviceId, Address, uint64_t request_id, GattSessionId &) = 0;
		virtual Status disconnect(GattSessionId, uint64_t request_id) = 0;
		virtual Status discover(GattSessionId, std::optional<Uuid> service, uint64_t request_id) = 0;
		virtual Status subscribe(GattSessionId, uint16_t value_handle, uint16_t cccd_handle, bool indications,
		    uint64_t request_id) = 0;
		virtual Status read(GattSessionId, uint16_t handle, uint64_t request_id) = 0;
		virtual Status write(GattSessionId, uint16_t handle, Bytes, bool with_response, uint64_t request_id) = 0;
		virtual void cancel(DeviceId) = 0;
		virtual bool take(GattEvent &) = 0;
	};
}

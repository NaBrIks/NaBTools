// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/gatt/client.hpp"
#include <memory>
namespace NaBTools::runtime
{
	class EspIdfGattClient final : public IGattClient
	{
		struct Impl; std::unique_ptr<Impl> impl_;
	public:
		EspIdfGattClient(); ~EspIdfGattClient()override;
		Status start(); void pump(bool may_open);
		bool opening()const;
		GattStats stats()const override;
		Status connect(DeviceId, Address, uint64_t, GattSessionId &)override;
		Status disconnect(GattSessionId, uint64_t)override;
		Status discover(GattSessionId, std::optional<Uuid>, uint64_t)override;
		Status subscribe(GattSessionId, uint16_t, uint16_t, bool, uint64_t)override;
		Status read(GattSessionId, uint16_t, uint64_t)override;
		Status write(GattSessionId, uint16_t, Bytes, bool, uint64_t)override;
		void cancel(DeviceId)override;
		bool take(GattEvent &)override;
	};
}

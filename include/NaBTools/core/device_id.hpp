// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include <cstdint>
namespace NaBTools::runtime
{
	struct DeviceId {
		uint16_t slot{65535};
		uint32_t generation{};
		bool operator==(const DeviceId &)const = default;
		bool valid()const
		{
			return slot != 65535 && generation;
		}

	};
}

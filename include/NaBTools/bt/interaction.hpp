// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/gap/advertisement.hpp"
#include "NaBTools/core/status.hpp"
#include <string_view>

namespace NaBTools::runtime
{
// Optional static knowledge. These views must remain valid for the device lifetime.
// status==ok means supported now; reason explains unavailable capabilities.
	struct OperationInfo {
		const char *key{};
		const char *label{};
		Status status{Status::ok};
		const char *reason{};
	};
	struct InteractionInfo {
		std::span<const OperationInfo> commands;
		std::span<const OperationInfo> events;
	};
// Opaque borrowed input: the engine does not parse or require CBOR. A module's
// optional wire adapter validates format/arguments and calls its typed methods.
	struct DeviceCommand {
		std::string_view key, format;
		Bytes arguments;
		uint64_t request_id{}, time_ms{};
	};
}

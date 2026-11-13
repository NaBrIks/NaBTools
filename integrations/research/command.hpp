// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/engine.hpp"
#include <optional>
namespace NaBTools::runtime::research
{
	struct Command {
		enum Kind {scan, remove, add, invoke, snapshot, scan_mode} kind{};
		ScanMode mode{ScanMode::passive};
		bool enabled{}; DeviceId device; Address address; uint8_t sid{255};
		std::array<char, 40> handler{}; uint64_t request_id{};
		uint64_t boot_id{};
		std::array<char, 48> operation{};
		std::array<uint8_t, 64> arguments{}; uint8_t argument_size{};
	};
// Deliberately restricted definite-map wire contract, not a general CBOR parser.
	std::optional<Command> parse_command(Bytes);
}

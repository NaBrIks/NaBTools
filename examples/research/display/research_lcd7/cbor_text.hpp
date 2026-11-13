// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include <span>
#include <cstdint>
#include <string>
namespace research_lcd7
{
// Bounded diagnostic text only; bytes remain the authoritative message.
	std::string cbor_text(std::span<const uint8_t>);
}

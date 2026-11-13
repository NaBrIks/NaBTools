// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
namespace NaBTools::runtime
{
// Normalized Bluetooth UUID, least significant byte first (ATT wire order).
	struct Uuid {
		std::array<uint8_t, 16> bytes{};
		static constexpr Uuid from32(uint32_t value)
		{
			Uuid out{{0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0, 0, 0, 0}};
			for(unsigned i = 0; i < 4; ++i)
			{
				out.bytes[12 + i] = static_cast<uint8_t>(value >> (8 * i));
			}
			return out;
		}

		static constexpr Uuid from16(uint16_t value)
		{
			return from32(value);
		}

		static std::optional<Uuid> from_wire(std::span<const uint8_t> wire)
		{
			if(wire.size() != 2 && wire.size() != 4 && wire.size() != 16) return {};

			if(wire.size() == 16)
			{
				Uuid out; for(size_t i = 0; i < 16; ++i)
					out.bytes[i] = wire[i];
				return out;
			}

			uint32_t n = 0; for(size_t i = 0; i < wire.size(); ++i)
				n |= uint32_t(wire[i]) << (8 * i);
			return from32(n);
		}

		std::optional<uint16_t> assigned16()const
		{
			const uint16_t n = uint16_t(bytes[12]) | (uint16_t(bytes[13]) << 8);
			return *this == from16(n) ? std::optional<uint16_t>(n) : std::nullopt;
		}

		bool operator==(const Uuid &)const = default;
		std::array<char, 37> text()const
		{
			std::array<char, 37> out{}; const auto &b = bytes;
			std::snprintf(out.data(), out.size(),
			    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			    b[15], b[14], b[13], b[12], b[11], b[10], b[9], b[8], b[7], b[6], b[5], b[4], b[3], b[2], b[1], b[0]);
			return out;
		}

	};
}

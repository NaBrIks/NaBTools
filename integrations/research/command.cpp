// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "command.hpp"
#include <string_view>
namespace NaBTools::runtime::research
{
	namespace
	{
		struct Cursor {
			Bytes input; size_t offset{};
			std::optional<uint64_t> value(uint8_t major)
			{
				if(offset == input.size()) return {};

				const auto head = input[offset++]; if((head >> 5) != major) return {};

				auto extra = head & 31; if(extra < 24) return extra;

				const unsigned n = extra == 24 ? 1 : extra == 25 ? 2 : extra == 26 ? 4 : extra == 27 ? 8 : 0;
				if(!n || n > input.size() - offset) return {};

				uint64_t v = 0; for(unsigned i = 0; i < n; ++i)
					v = (v << 8) | input[offset++];
				return v;
			}

			std::optional<std::string_view> key()
			{
				auto n = value(3); if(!n || *n > 24 || *n > input.size() - offset) return {};

				std::string_view out(reinterpret_cast<const char *>(input.data() + offset), size_t(*n));
				offset += size_t(*n); return out;
			}

		};
	}
	std::optional<Command> parse_command(Bytes input)
	{
		if(input.size() > 192) return {};

		Cursor c{input}; auto n = c.value(5); if(!n || !*n || *n > 6) return {};

		Command out;
		bool snapshot = false, scan = false, mode = false, slot = false, generation = false, handler = false,
		    address = false, type = false, sid = false, request = false, invoke = false, args = false, boot = false;
		for(size_t i = 0; i < *n; ++i)
		{
			auto k = c.key(); if(!k) return {};

			if(*k == "snapshot")
			{
				if(snapshot || c.offset == input.size() || input[c.offset++] != 0xf5) return {};

				snapshot = true;
			}
			else if(*k == "scan")
			{
				if(scan || c.offset == input.size()) return {};

				const auto b = input[c.offset++]; if(b != 0xf4 && b != 0xf5) return {};

				scan = true; out.enabled = b == 0xf5;
			}
			else if(*k == "scan_mode")
			{
				if(mode) return {};

				auto value = c.key(); if(!value || (*value != "active" && *value != "passive")) return {};

				mode = true; out.mode = *value == "active" ? ScanMode::active : ScanMode::passive;
			}
			else if(*k == "remove_slot" || *k == "invoke_slot")
			{
				auto v = c.value(0); if(slot || !v || *v >= BtEngine::max_devices) return {};

				slot = true; invoke = *k == "invoke_slot"; out.device.slot = static_cast<uint16_t>(*v);
			}
			else if(*k == "generation")
			{
				auto v = c.value(0); if(generation || !v || !*v || *v > UINT32_MAX) return {};

				generation = true; out.device.generation = static_cast<uint32_t>(*v);
			}
			else if(*k == "add_handler")
			{
				auto len = c.value(3);
				if(handler || !len || !*len || *len >= out.handler.size() || *len > input.size() - c.offset) return {};

				for(size_t j = 0; j < *len; ++j)
				{
					const uint8_t ch = input[c.offset++];
					if(!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_')) return {};

					out.handler[j] = static_cast<char>(ch);
				}
				handler = true;
			}
			else if(*k == "address")
			{
				auto len = c.value(2); if(address || !len || *len != 6 || *len > input.size() - c.offset) return {};

				for(auto &b:out.address.bytes)
				{
					b = input[c.offset++];
				}
				address = true;
			}
			else if(*k == "address_type")
			{
				auto v = c.value(0); if(type || !v || *v > 3)
					return {};

				type = true; out.address.type = static_cast<uint8_t>(*v);
			}
			else if(*k == "sid")
			{
				auto v = c.value(0); if(sid || !v || (*v > 15 && *v != 255))
					return {};

				sid = true; out.sid = static_cast<uint8_t>(*v);
			}
			else if(*k == "invoke")
			{
				auto len = c.value(3);
				if(out.operation[0] || !len || !*len || *len >= out.operation.size() || *len > input.size() - c.offset)
					return {};

				for(size_t j = 0; j < *len; ++j)
				{
					const auto ch = input[c.offset++];
					if(!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_')) return {};

					out.operation[j] = static_cast<char>(ch);
				}
			}
			else if(*k == "arguments")
			{
				auto len = c.value(2); if(args || !len || *len > out.arguments.size() || *len > input.size() - c.offset)
					return {};

				out.argument_size = static_cast<uint8_t>(*len); for(size_t j = 0; j < *len; ++j)
					out.arguments[j] = input[c.offset++];
				args = true;
			}
			else if(*k == "boot_id")
			{
				auto v = c.value(0); if(boot || !v || !*v || *v > 9007199254740991ULL)
					return {};

				boot = true; out.boot_id = *v;
			}
			else if(*k == "request_id")
			{
				auto v = c.value(0); if(request || !v || !*v || *v > 9007199254740991ULL)
					return {};

				request = true; out.request_id = *v;
			}
			else return {};
		}
		if(c.offset != input.size()) return {};

		if(mode)
		{
			if(scan || snapshot || slot || generation || handler || address || type || sid || invoke || args ||
			    out.operation[0]) return {};

			out.kind = Command::scan_mode; return out;
		}

		if(snapshot)
		{
			if(scan || slot || generation || handler || address || type || sid || invoke || args) return {};

			out.kind = Command::snapshot; return out;
		}

		if(invoke || args || out.operation[0])
		{
			if(invoke && slot && generation && out.operation[0] && args && request && !scan && !handler && !address &&
			    !type && !sid){out.kind = Command::invoke; return out;}

			return {};
		}

		if(scan && !slot && !generation && !handler && !address && !type && !sid){out.kind = Command::scan; return out;}

		if(!scan && slot && generation && !handler && !address && !type && !sid)
		{
			out.kind = Command::remove; return out;
		}

		if(!scan && !slot && !generation && handler && address && type && sid){out.kind = Command::add; return out;}

		return {};
	}

}

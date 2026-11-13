// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "cbor_text.hpp"
#include "cbor.h"
#include <cstdio>
#include <algorithm>
#include <string_view>
namespace research_lcd7
{
	namespace
	{
		struct Text {
			std::string value; unsigned budget = 96;
			void append(std::string_view part)
			{
				if(value.size() < 2048)
					value.append(part.substr(0, 2048 - value.size()));
			}

			CborError render(CborValue &item, unsigned depth)
			{
				if(!budget-- || depth > 4){append("..."); return cbor_value_advance(&item);}

				if(cbor_value_is_map(&item) || cbor_value_is_array(&item))
				{
					const bool map = cbor_value_is_map(&item); CborValue nested;
					auto error = cbor_value_enter_container(&item, &nested); if(error) return error;

					append(map ? "{\n" : "["); unsigned n = 0;
					while(!cbor_value_at_end(&nested))
					{
						if(!budget || value.size() >= 2000){append("..."); return CborErrorDataTooLarge;}

						if(n++) append(map ? "\n" : ", ");

						if((error = render(nested, depth + 1))) return error;

						if(map){append(": "); if((error = render(nested, depth + 1))) return error;}
					}
					append(map ? "\n}" : "]"); return cbor_value_leave_container(&item, &nested);
				}

				if(cbor_value_is_text_string(&item) || cbor_value_is_byte_string(&item))
				{
					const bool text = cbor_value_is_text_string(&item); size_t size = 0;
					auto error = cbor_value_get_string_length(&item, &size); if(error) return error;

					if(size > 96){append(text ? "<long text>" : "<long bytes>"); return cbor_value_advance(&item);}

					uint8_t bytes[97]{}; size_t capacity = sizeof(bytes); CborValue next;
					error = text ? cbor_value_copy_text_string(&item, reinterpret_cast<char *>(bytes), &capacity,
					        &next) : cbor_value_copy_byte_string(&item, bytes, &capacity, &next);
					if(error) return error;

					if(text)
					{
						for(size_t i = 0; i < size; ++i)
						{
							const char c = static_cast<char>(bytes[i]);
							append(bytes[i] < 32 ? " " : std::string_view(&c, 1));
						}
					}
					else
					{
						append("h'"); char hex[3]; for(size_t i = 0; i < size; ++i)
						{
							std::snprintf(hex, sizeof(hex), "%02X", bytes[i]); append(hex);
						}
						append("'");
					}

					item = next; return CborNoError;
				}

				char number[48]{};
				if(cbor_value_is_unsigned_integer(&item))
				{
					uint64_t n; auto e = cbor_value_get_uint64(&item, &n); if(e)
						return e;

					std::snprintf(number, sizeof(number), "%llu", static_cast<unsigned long long>(n)); append(number);
				}
				else if(cbor_value_is_integer(&item))
				{
					int64_t n; auto e = cbor_value_get_int64(&item, &n); if(e)
						return e;

					std::snprintf(number, sizeof(number), "%lld", static_cast<long long>(n)); append(number);
				}
				else if(cbor_value_is_boolean(&item))
				{
					bool b; auto e = cbor_value_get_boolean(&item, &b); if(e)
						return e;

					append(b ? "true" : "false");
				}
				else if(cbor_value_is_null(&item)) append("null");
				else if(cbor_value_is_double(&item))
				{
					double n; auto e = cbor_value_get_double(&item, &n); if(e)
						return e;

					std::snprintf(number, sizeof(number), "%.6g", n); append(number);
				}
				else if(cbor_value_is_float(&item) || cbor_value_is_half_float(&item))
				{
					float n;
					auto e = cbor_value_is_float(&item) ? cbor_value_get_float(&item,
					        &n) : cbor_value_get_half_float_as_float(&item, &n); if(e)
						return e;

					std::snprintf(number, sizeof(number), "%.6g", double(n)); append(number);
				}
				else append("<unsupported>");

				return cbor_value_advance(&item);
			}

		};
	}
	std::string cbor_text(std::span<const uint8_t> bytes)
	{
		CborParser parser; CborValue item; Text out;
		auto error = cbor_parser_init(bytes.data(), bytes.size(), 0, &parser, &item);
		if(!error) error = out.render(item, 0);

		if(error) out.append("\n[invalid or display limit]");
		else if(cbor_value_get_next_byte(&item) != bytes.data() + bytes.size()) out.append("\n[trailing bytes]");

		return out.value;
	}

}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "advertisement.hpp"
#include "NaBTools/core/uuid.hpp"
#include <string_view>

namespace NaBTools::runtime
{
	enum class UuidWidth:uint8_t { bits16=2, bits32=4, bits128=16 };
// All views borrow bytes; keep the owning Advertisement alive and unchanged.
	class UuidListView
	{
		Bytes bytes_; size_t width_;
		UuidListView(Bytes bytes, size_t width)
		  : bytes_(bytes), width_(width)
		{
		}

	public:
		static std::optional<UuidListView> parse(Bytes bytes, UuidWidth width)
		{
			const auto n = static_cast<size_t>(width);
			if((n != 2 && n != 4 && n != 16) || bytes.size() % n) return {};

			return UuidListView(bytes, n);
		}

		size_t size()const
		{
			return bytes_.size() / width_;
		}

		Bytes raw()const
		{
			return bytes_;
		}

		std::optional<Uuid> at(size_t index)const
		{
			if(index >= size()) return {};

			return Uuid::from_wire(bytes_.subspan(index * width_, width_));
		}

	};
	struct AdvertisingFlags {
		uint8_t raw{};
		bool limited_discoverable()const
		{
			return raw & 1;
		}

		bool general_discoverable()const
		{
			return raw & 2;
		}

		bool br_edr_not_supported()const
		{
			return raw & 4;
		}

		bool simultaneous_controller()const
		{
			return raw & 8;
		}

		bool simultaneous_host()const
		{
			return raw & 16;
		}

		bool non_discoverable()const
		{
			return !(raw & 3);
		}

	};
	struct ConnectionIntervalRange {
		uint16_t minimum{}, maximum{};
		std::optional<double> minimum_ms()const
		{
			return minimum == 0xffff ? std::nullopt : std::optional<double>(minimum * 1.25);
		}

		std::optional<double> maximum_ms()const
		{
			return maximum == 0xffff ? std::nullopt : std::optional<double>(maximum * 1.25);
		}

	};
// Lengths and byte order follow CSS Part A. Unknown fields remain available via
// raw()/AdCursor, duplicate AD types via occurrence, not a lossy map keyed by type.
// https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/CSS_v11/out/en/supplement-to-the-bluetooth-core-specification/data-types-specification.html
	class AdView
	{
		Bytes bytes_;
		static uint16_t le16(Bytes b)
		{
			return uint16_t(b[0]) | (uint16_t(b[1]) << 8);
		}

		std::optional<uint8_t> byte(uint8_t type, size_t occurrence = 0)const
		{
			auto b = raw(type, occurrence); return b && b->size() == 1 ? std::optional<uint8_t>((*b)[0]) : std::nullopt;
		}

		std::optional<uint16_t> word(uint8_t type, size_t occurrence = 0)const
		{
			auto b = raw(type, occurrence);
			return b && b->size() == 2 ? std::optional<uint16_t>(le16(*b)) : std::nullopt;
		}

	public:
		explicit AdView(Bytes bytes)
		  : bytes_(bytes)
		{
		}

		bool well_formed()const
		{
			AdCursor c(bytes_); while(c.next())
			{
			}
			return !c.malformed();
		}

		std::optional<Bytes> raw(uint8_t type, size_t occurrence = 0)const
		{
			return field(bytes_, type, occurrence);
		}

		std::optional<AdvertisingFlags> flags(size_t occurrence = 0)const
		{
			auto b = byte(1, occurrence); return b ? std::optional<AdvertisingFlags>({*b}) : std::nullopt;
		}

		std::optional<int8_t> tx_power(size_t occurrence = 0)const
		{
			auto b = byte(0x0a, occurrence); return b ? std::optional<int8_t>(static_cast<int8_t>(*b)) : std::nullopt;
		}

		std::optional<std::string_view> local_name(bool complete = true, size_t occurrence = 0)const
		{
			auto b = raw(complete ? 9 : 8, occurrence); if(!b) return {};

			return std::string_view(reinterpret_cast<const char *>(b->data()), b->size());
		}

		std::optional<UuidListView> service_uuids(UuidWidth width, bool complete = true, size_t occurrence = 0)const
		{
			const uint8_t t = width == UuidWidth::bits16 ? 2 : width == UuidWidth::bits32 ? 4 : 6;
			auto b = raw(t + uint8_t(complete), occurrence); return b ? UuidListView::parse(*b, width) : std::nullopt;
		}

		std::optional<UuidListView> solicitation_uuids(UuidWidth width, size_t occurrence = 0)const
		{
			const uint8_t t = width == UuidWidth::bits16 ? 0x14 : width == UuidWidth::bits32 ? 0x1f : 0x15;
			auto b = raw(t, occurrence); return b ? UuidListView::parse(*b, width) : std::nullopt;
		}

		std::optional<Bytes> service_data(const Uuid &uuid, UuidWidth width = UuidWidth::bits16,
		    size_t occurrence = 0)const
		{
			const size_t n = static_cast<size_t>(width); if(n != 2 && n != 4 && n != 16) return {};

			const uint8_t t = n == 2 ? 0x16 : n == 4 ? 0x20 : 0x21;
			for(size_t i = 0;; ++i)
			{
				auto b = raw(t, i); if(!b) return {};

				if(b->size() >= n && Uuid::from_wire(b->first(n)) == uuid && occurrence-- == 0) return b->subspan(n);
			}
		}

		std::optional<Bytes> manufacturer_data(size_t occurrence = 0)const
		{
			return raw(0xff, occurrence);
		}

		std::optional<uint16_t> manufacturer_id(size_t occurrence = 0)const
		{
			auto b = manufacturer_data(occurrence);
			return b && b->size() >= 2 ? std::optional<uint16_t>(le16(*b)) : std::nullopt;
		}

		std::optional<uint16_t> appearance()const
		{
			return word(0x19);
		}

		std::optional<uint16_t> advertising_interval()const
		{
			return word(0x1a);
		}

		std::optional<uint8_t> le_role()const
		{
			return byte(0x1c);
		}

		std::optional<uint32_t> class_of_device()const
		{
			auto b = raw(0x0d); if(!b || b->size() != 3) return {};

			return uint32_t(le16(*b)) | (uint32_t((*b)[2]) << 16);
		}

		std::optional<std::span<const uint8_t, 16> > sm_tk_data()const
		{
			auto b = raw(0x10); if(!b || b->size() != 16)
				return {};

			return std::span<const uint8_t, 16>(b->data(), 16);
		}

		std::optional<uint8_t> sm_oob_flags()const
		{
			return byte(0x11);
		}

		std::optional<ConnectionIntervalRange> connection_interval_range()const
		{
			auto b = raw(0x12); if(!b || b->size() != 4) return {};

			return ConnectionIntervalRange{le16(*b), le16(b->subspan(2))};
		}

	};
}

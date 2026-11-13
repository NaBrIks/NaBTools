// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/bt/gap/advertisement.hpp"
#include <algorithm>
#include <cstdio>

namespace NaBTools::runtime
{
	std::string Address::text() const
	{
		char out[18];
		std::snprintf(out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X",
		    bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
		return out;
	}

	std::optional<Advertisement> Advertisement::copy(AdvertisementView v)
	{
		if(v.payload.size() > capacity)  return std::nullopt;

		Advertisement out;
		out.address = v.address; out.rssi = v.rssi; out.time_ms = v.time_ms;
		out.sid = v.sid; out.legacy = v.legacy; out.complete = v.complete;
		out.radio = v.radio;
		out.size = static_cast<uint16_t>(v.payload.size());
		std::copy(v.payload.begin(), v.payload.end(), out.data.begin());
		return out;
	}

	AdvertisementView Advertisement::view() const
	{
		return {address, rssi, time_ms, sid, legacy, complete, Bytes(data.data(), size), radio};
	}

	std::optional<AdField> AdCursor::next()
	{
		if(offset_ >= bytes_.size() || malformed_)  return std::nullopt;

		const size_t begin = offset_, length = bytes_[begin];
		if(!length) { offset_ = bytes_.size(); return std::nullopt; }

		if(length > bytes_.size() - begin - 1) { malformed_ = true; return std::nullopt; }

		offset_ += length + 1;
		return AdField{bytes_[begin + 1], bytes_.subspan(begin + 2, length - 1), begin};
	}

	AdStatus validate(AdvertisementView v)
	{
		// Extended fragments are not a stand-alone AD document, even if the final
		// fragment carries COMPLETE. Reassembly is deliberately outside this slice.
		if(!v.legacy || !v.complete)  return AdStatus::fragment;

		AdCursor cursor(v.payload);
		while(cursor.next())
		{
		}
		return cursor.malformed() ? AdStatus::malformed : AdStatus::complete;
	}

	std::optional<Bytes> field(Bytes bytes, uint8_t type, size_t occurrence)
	{
		AdCursor cursor(bytes);
		while(auto f = cursor.next())
			if(f->type == type && occurrence-- == 0) return f->value;

		return std::nullopt;
	}

	const char * name(AdStatus s)
	{
		switch(s)
		{
		case AdStatus::complete: return "complete";
		case AdStatus::malformed: return "malformed"; case AdStatus::fragment: return "fragment";
		}
		return "unknown";
	}

	const char * phy_name(uint8_t value)
	{
		switch(value)
		{
		case 0: return "Not reported"; case 1: return "LE 1M"; case 2: return "LE 2M"; case 3: return "LE Coded";
		default: return "Unknown";
		}
	}

	const char * address_type_name(uint8_t value)
	{
		switch(value)
		{
		case 0: return "Public"; case 1: return "Random"; case 2: return "Public identity";
		case 3: return "Random identity"; case 255: return "Not reported"; default: return "Unknown";
		}
	}

	const char * data_status_name(uint8_t value)
	{
		switch(value)
		{
		case 0: return "Complete"; case 1: return "More data pending"; case 2: return "Truncated";
		default: return "Unknown";
		}
	}

}

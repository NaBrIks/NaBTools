// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/core/cbor.hpp"
namespace NaBTools::runtime
{
	void CborWriter::byte(uint8_t v)
	{
		if(used_ < output_.size())
			output_[used_++] = v;
		else
			good_ = false;
	}

	void CborWriter::head(uint8_t major, uint64_t n)
	{
		if(n < 24){byte(static_cast<uint8_t>((major << 5) | n)); return;}

		const unsigned count = n <= 0xff ? 1 : n <= 0xffff ? 2 : n <= 0xffffffff ? 4 : 8;
		byte(static_cast<uint8_t>((major << 5) | (count == 1 ? 24 : count == 2 ? 25 : count == 4 ? 26 : 27)));
		for(unsigned i = count; i > 0; --i)
			byte(static_cast<uint8_t>(n >> ((i - 1) * 8)));
	}

	void CborWriter::text(std::string_view s)
	{
		head(3, s.size()); for(unsigned char c:s)
			byte(c);
	}

	void CborWriter::bytes(Bytes s)
	{
		head(2, s.size()); for(auto c:s)
			byte(c);
	}

}

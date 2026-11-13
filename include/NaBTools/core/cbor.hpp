// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/gap/advertisement.hpp"
#include <string_view>

namespace NaBTools::runtime
{
// Small bounded writer for the demo wire maps and direct TPMS output. No DOM,
// allocation, indefinite containers, parser, or JSON dependency.
	class CborWriter
	{
		std::span<uint8_t> output_;
		size_t used_{};
		bool good_{true};
		void byte(uint8_t);
		void head(uint8_t major, uint64_t);
	public:
		explicit CborWriter(std::span<uint8_t> output)
		  : output_(output)
		{
		}

		void map(size_t n)
		{
			head(5, n);
		}

		void array(size_t n)
		{
			head(4, n);
		}

		void number(uint64_t n)
		{
			head(0, n);
		}

		void integer(int64_t n)
		{
			n >= 0 ? head(0, static_cast<uint64_t>(n)) : head(1, static_cast<uint64_t>(-(n + 1)));
		}

		void text(std::string_view);
		void bytes(Bytes);
		void boolean(bool v)
		{
			byte(v ? 0xf5 : 0xf4);
		}

		void null()
		{
			byte(0xf6);
		}

		bool good()const
		{
			return good_;
		}

		Bytes result()const
		{
			return good_ ? Bytes(output_.data(), used_) : Bytes{};
		}

	};
}

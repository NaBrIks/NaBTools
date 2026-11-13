// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/core/cbor.hpp"
#include <atomic>

namespace NaBTools::runtime::research
{
// Single engine-context producer; the sink must copy before returning. Wire
// sequence covers attempted events as well, so queue loss cannot be silent.
	class EventStream
	{
	public:
		static constexpr size_t frame_capacity = 4096;
		using Sink = bool (*)(void *, Bytes);
	private:
		std::array<uint8_t, frame_capacity> bytes_{};
		uint64_t boot_{}; std::atomic<uint64_t> sequence_{}, dropped_{};
		void *context_{}; Sink sink_{};
	public:
		void begin(uint64_t boot, void *context, Sink sink)
		{
			boot_ = boot; context_ = context; sink_ = sink;
		}

		uint64_t sequence()const
		{
			return sequence_.load();
		}

		uint64_t dropped()const
		{
			return dropped_.load();
		}

		template<class Body> bool emit(const char *type, Body body)
		{
			const auto seq = ++sequence_; CborWriter w(bytes_); w.map(5);
			w.text("v"); w.number(1); w.text("boot_id"); w.number(boot_);
			w.text("seq"); w.number(seq); w.text("reset"); w.boolean(false);
			w.text("changes"); w.array(1); w.array(2); w.text(type); body(w);
			if(!w.good() || !sink_ || !sink_(context_, w.result())){++dropped_; return false;}

			return true;
		}

	};
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/core/action.hpp"
#include "NaBTools/core/cbor.hpp"
#include <cstring>
#include <vector>

namespace NaBTools::runtime::research
{
// Application adapter, deliberately outside the BT core. A bounded copy of
// bytes emitted by any protocol module; no decoding or protocol-specific keys.
	class MessageJournal
	{
	public:
		struct Entry {MessageInfo info; uint64_t ordinal{}; std::array<char, 96> schema{}; std::vector<uint8_t> payload;
		};
	private:
		void *observer_context_{}; void (*observer_)(void *, const Entry &)
		{
		};
		static constexpr size_t capacity = 16, max_payload = 768;
		std::array<Entry, capacity> entries_;
		size_t count_{}, next_{};
		uint64_t ordinal_{}, dropped_{};
		static Delivery receive(void *context, const MessageInfo &info, Bytes bytes)
		{
			return static_cast<MessageJournal *>(context)->push(info, bytes);
		}

	public:
		void on_entry(void *context, void (*callback)(void *, const Entry &))
		{
			observer_context_ = context; observer_ = callback;
		}

		static void write_entry(CborWriter &w, const Entry &e)
		{
			w.map(7); w.text("ordinal"); w.number(e.ordinal); w.text("slot"); w.number(e.info.device.slot);
			w.text("generation"); w.number(e.info.device.generation); w.text("time_ms"); w.number(e.info.time_ms);
			w.text("sequence"); w.number(e.info.sequence); w.text("schema"); w.text(e.schema.data());
			w.text("payload"); w.bytes(e.payload);
		}

		CborAction action()
		{
			return {this, receive};
		}

		Delivery push(const MessageInfo &info, Bytes bytes)
		{
			if(bytes.empty() || bytes.size() > max_payload || !info.schema || std::strlen(info.schema) >= 96)
			{
				++dropped_; return Delivery::dropped;
			}

			auto &e = entries_[next_]; e.info = info; e.info.schema = nullptr;
			std::strcpy(e.schema.data(), info.schema); e.payload.assign(bytes.begin(), bytes.end());
			e.ordinal = ++ordinal_;
			next_ = (next_ + 1) % capacity; if(count_ < capacity) ++count_;

			// Borrowed only for this synchronous call; an asynchronous sink must copy.
			if(observer_) observer_(observer_context_, e);

			return Delivery::accepted;
		}

		uint64_t dropped()const
		{
			return dropped_;
		}

		size_t size()const
		{
			return count_;
		}

		template<class Visitor>void visit(Visitor visitor)const
		{
			const size_t begin = count_ == capacity ? next_ : 0;
			for(size_t i = 0; i < count_; ++i)
				visitor(entries_[(begin + i) % capacity]);
		}

		void write(CborWriter &w)const
		{
			w.array(count_); const size_t begin = count_ == capacity ? next_ : 0;
			for(size_t i = 0; i < count_; ++i)
				write_entry(w, entries_[(begin + i) % capacity]);
		}

	};
}

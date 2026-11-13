// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/engine.hpp"
#include "NaBTools/core/cbor.hpp"

namespace NaBTools::runtime
{
	struct ObservationRow {
		Advertisement packet;
		uint32_t packets{};
		AdStatus status{AdStatus::complete};
		std::array<char, 40> local_name{};
		int manufacturer{-1};
		AdvertisementClassification advertising_knowledge, response_knowledge;
		uint64_t advertising_time{}, response_time{};
		AdvertisementClassification classification()const;
	};
	class ResearchModule final : public IModule
	{
	public:
		static constexpr size_t capacity = 32;
	private:
		std::array<ObservationRow, capacity> rows_{};
		size_t count_{};
		uint32_t evicted_{};
		void write_summary_fields(const BtEngine &, CborWriter &, const char *, uint64_t)const;
	public:
		const char * key()const override
		{
			return "nabtools.research";
		}

		void observe(BtEngine &, AdvertisementView, AdStatus)override;
		std::span<const ObservationRow> rows()const
		{
			return {rows_.data(), count_};
		}

		const ObservationRow * find(Address, uint8_t sid)const;
		uint32_t evicted()const
		{
			return evicted_;
		}

		bool write_summary(const BtEngine &, CborWriter &, const char *, uint64_t)const;
		static void write_device(const BtEngine &, CborWriter &, const DeviceInfo &);
		static void write_observation(const BtEngine &, CborWriter &, const ObservationRow &);
		bool write_snapshot(const BtEngine &, CborWriter &, const char *source, uint64_t now_ms)const;
	};
}

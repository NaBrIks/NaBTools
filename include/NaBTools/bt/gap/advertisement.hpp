// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace NaBTools::runtime
{
	using Bytes = std::span<const uint8_t>;
	struct Address {
		std::array<uint8_t, 6> bytes{};
		uint8_t type{};
		bool operator==(const Address &) const = default;
		std::string text() const;
	};
// Controller report facts, separate from fields inside the advertising payload.
// Raw values are retained, including unknown/reserved values and sentinels.
	struct RadioInfo {
		uint16_t event_properties{};
		uint8_t primary_phy{}, secondary_phy{};
		int8_t tx_power{127}; // 127: controller has no Tx Power information
		uint16_t periodic_interval{}; // 1.25 ms units; zero: no periodic advertising
		Address directed_address{{}, 255};
		uint8_t data_status{};
		bool connectable()const
		{
			return event_properties & 1;
		}

		bool scannable()const
		{
			return event_properties & 2;
		}

		bool directed()const
		{
			return event_properties & 4;
		}

		bool scan_response()const
		{
			return event_properties & 8;
		}

		std::optional<int8_t> tx_power_dbm()const
		{
			return tx_power == 127 ? std::nullopt : std::optional<int8_t>(tx_power);
		}

		std::optional<double> periodic_interval_ms()const
		{
			return periodic_interval ? std::optional<double>(periodic_interval * 1.25) : std::nullopt;
		}

	};
	const char *phy_name(uint8_t);
	const char *address_type_name(uint8_t);
	const char *data_status_name(uint8_t);
	struct AdvertisementView {
		Address address;
		int8_t rssi{};
		uint64_t time_ms{};
		uint8_t sid{255};
		bool legacy{true}, complete{true};
		Bytes payload;
		std::optional<RadioInfo> radio;
	};
	struct Advertisement {
		static constexpr size_t capacity = 255;
		Address address;
		int8_t rssi{};
		uint64_t time_ms{};
		uint8_t sid{255};
		bool legacy{true}, complete{true};
		uint16_t size{};
		std::array<uint8_t, capacity> data{};
		std::optional<RadioInfo> radio;
		static std::optional<Advertisement> copy(AdvertisementView);
		AdvertisementView view() const;
	};
	enum class AdStatus { complete, malformed, fragment };
	struct AdField { uint8_t type{}; Bytes value; size_t offset{}; };
	class AdCursor
	{
		Bytes bytes_;
		size_t offset_{};
		bool malformed_{};
	public:
		explicit AdCursor(Bytes bytes)
		  : bytes_(bytes)
		{
		}

		std::optional<AdField> next();
		bool malformed() const
		{
			return malformed_;
		}

	};
	AdStatus validate(AdvertisementView);
	std::optional<Bytes> field(Bytes, uint8_t type, size_t occurrence = 0);
	const char *name(AdStatus);
} // namespace NaBTools::runtime

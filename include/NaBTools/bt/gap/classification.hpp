// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "advertisement.hpp"
#include "NaBTools/core/uuid.hpp"

namespace NaBTools::runtime
{
	enum class DeviceKind:uint8_t {unknown, phone, computer, watch, remote_control, keyboard, mouse,
		                           joystick, gamepad, tag, heart_rate_sensor, thermometer, blood_pressure_sensor,
		                           cycling_sensor, weight_scale, environmental_sensor, tire_pressure_sensor};
	enum class KnowledgeSource:uint8_t {none, local_name, service_uuid, service_data, solicitation, appearance,
		                                handler};
	enum class KnowledgeStrength:uint8_t {unknown, hint, advertised, decoded};
	enum class AdvertisedCapability:uint8_t {heart_rate, hid, battery, device_information, temperature,
		                                     blood_pressure, cycling_speed_cadence, cycling_power,
		                                     running_speed_cadence,
		                                     weight, environmental_sensing, immediate_alert, link_loss, tire_pressure};
	const char *name(DeviceKind);
	const char *name(KnowledgeSource);
	const char *name(KnowledgeStrength);
	const char *name(AdvertisedCapability);
	struct TypeEvidence {
		DeviceKind kind{DeviceKind::unknown}; KnowledgeSource source{KnowledgeSource::none};
		KnowledgeStrength strength{KnowledgeStrength::unknown}; uint16_t value{};
	};
	struct CapabilityEvidence {
		AdvertisedCapability capability{}; KnowledgeSource source{}; uint16_t value{};
		bool operator==(const CapabilityEvidence &)const = default;
	};
// Bounded facts, independent of SDK, SIG name catalogs, CBOR and device lifetime.
// A claim is not authenticated identity or proof of a working GATT service.
	struct AdvertisementClassification {
		static constexpr size_t capacity = 6;
		TypeEvidence type;
		std::array<CapabilityEvidence, capacity> capabilities{}; uint8_t count{};
		bool conflict{}, truncated{};
		void offer(TypeEvidence);
		void add(CapabilityEvidence);
		void merge(const AdvertisementClassification &);
	};
// Complete legacy AD documents only; malformed/fragments produce no claims.
// Service Solicitation describes wanted services, never provided capabilities.
// https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/CSS_v12/out/en/supplement-to-the-bluetooth-core-specification/data-types-specification.html
	AdvertisementClassification classify_advertisement(AdvertisementView);
}

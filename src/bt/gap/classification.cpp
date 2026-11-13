// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/bt/gap/classification.hpp"
#include "NaBTools/bt/gap/ad_view.hpp"
#include <string_view>
#include <utility>

namespace NaBTools::runtime
{
	namespace
	{
		unsigned priority(KnowledgeSource s)
		{
			switch(s)
			{
			case KnowledgeSource::handler: return 4; case KnowledgeSource::appearance: return 3;
			case KnowledgeSource::service_uuid: case KnowledgeSource::service_data: return 2;
			case KnowledgeSource::local_name: return 1; default: return 0;
			}
		}

		unsigned priority(TypeEvidence e)
		{
			return e.source == KnowledgeSource::handler &&
			       e.strength != KnowledgeStrength::decoded ? 2 : priority(e.source);
		}

		struct ServiceRule {uint16_t uuid; AdvertisedCapability capability; DeviceKind kind;};
		constexpr ServiceRule rules[]{
			{0x180d, AdvertisedCapability::heart_rate, DeviceKind::heart_rate_sensor},
			{0x1812, AdvertisedCapability::hid, DeviceKind::unknown},
			{0x180f, AdvertisedCapability::battery, DeviceKind::unknown},
			{0x180a, AdvertisedCapability::device_information, DeviceKind::unknown},
			{0x1809, AdvertisedCapability::temperature, DeviceKind::thermometer},
			{0x1810, AdvertisedCapability::blood_pressure, DeviceKind::blood_pressure_sensor},
			{0x1816, AdvertisedCapability::cycling_speed_cadence, DeviceKind::cycling_sensor},
			{0x1818, AdvertisedCapability::cycling_power, DeviceKind::cycling_sensor},
			{0x1814, AdvertisedCapability::running_speed_cadence, DeviceKind::unknown},
			{0x181d, AdvertisedCapability::weight, DeviceKind::weight_scale},
			{0x181a, AdvertisedCapability::environmental_sensing, DeviceKind::environmental_sensor},
			{0x1802, AdvertisedCapability::immediate_alert, DeviceKind::unknown},
			{0x1803, AdvertisedCapability::link_loss, DeviceKind::unknown},
		};
		DeviceKind appearance_kind(uint16_t value)
		{
			switch(value >> 6)
			{
			case 1: return DeviceKind::phone; case 2: return DeviceKind::computer;
			case 3: return DeviceKind::watch; case 6: return DeviceKind::remote_control; case 8: return DeviceKind::tag;
			case 12: return DeviceKind::thermometer; case 13: return DeviceKind::heart_rate_sensor;
			case 14: return DeviceKind::blood_pressure_sensor; case 15: switch(value & 63)
				{
				case 1: return DeviceKind::keyboard; case 2: return DeviceKind::mouse;
				case 3: return DeviceKind::joystick;
				case 4: return DeviceKind::gamepad; case 10: return DeviceKind::remote_control;
				default: return DeviceKind::unknown;
				}
			case 18: return DeviceKind::cycling_sensor; case 50: return DeviceKind::weight_scale;
			default: return DeviceKind::unknown;
			}
		}

		bool contains_ascii(std::string_view text, std::string_view word)
		{
			for(size_t i = 0; i + word.size() <= text.size(); ++i)
			{
				bool equal = true;
				for(size_t j = 0; j < word.size(); ++j)
				{
					auto ch = static_cast<unsigned char>(text[i + j]);
					if(ch >= 'A' && ch <= 'Z') ch = static_cast<unsigned char>(ch + 32);

					if(ch != static_cast<unsigned char>(word[j])){equal = false; break;}}
				if(equal) return true;
			}
			return false;
		}

		void service(AdvertisementClassification &out, const Uuid &uuid, KnowledgeSource source)
		{
			const auto value = uuid.assigned16(); if(!value) return;

			for(const auto &rule:rules)
				if(rule.uuid == *value)
				{
					out.add({rule.capability, source, *value});
					if(source != KnowledgeSource::solicitation)
						out.offer({rule.kind, source, KnowledgeStrength::advertised, *value});

					return;
				}
		}

	}
	void AdvertisementClassification::offer(TypeEvidence evidence)
	{
		if(evidence.kind == DeviceKind::unknown) return;

		if(priority(evidence) > priority(type)){type = evidence; conflict = false;}
		else if(priority(evidence) == priority(type) && evidence.kind != type.kind) conflict = true;
	}

	void AdvertisementClassification::add(CapabilityEvidence evidence)
	{
		for(size_t i = 0; i < count; ++i)
			if(capabilities[i] == evidence) return;

		if(count == capacity)
		{
			truncated = true; return;
		}

		capabilities[count++] = evidence;
	}

	void AdvertisementClassification::merge(const AdvertisementClassification &other)
	{
		const auto old_priority = priority(type); offer(other.type);
		if(priority(other.type) >= old_priority) conflict = conflict || other.conflict;

		truncated = truncated || other.truncated;
		for(size_t i = 0; i < other.count; ++i)
			add(other.capabilities[i]);
	}

	AdvertisementClassification classify_advertisement(AdvertisementView view)
	{
		AdvertisementClassification out; if(validate(view) != AdStatus::complete) return out;

		AdView ad(view.payload);
		auto local = ad.local_name(); if(!local) local = ad.local_name(false);

		if(local)
		{
			constexpr std::pair<std::string_view, DeviceKind> hints[]{
				{"remote", DeviceKind::remote_control}, {"keyboard", DeviceKind::keyboard},
				{"mouse", DeviceKind::mouse},
				{"watch", DeviceKind::watch}, {"phone", DeviceKind::phone}, {"smarttag", DeviceKind::tag}};
			for(const auto & [word, kind]:hints)
				if(contains_ascii(*local, word))
				{
					if(kind == DeviceKind::phone && (contains_ascii(*local, "headphone") || contains_ascii(*local,
					    "earphone"))) continue;

					out.offer({kind, KnowledgeSource::local_name, KnowledgeStrength::hint, 0}); break;
				}
		}

		AdCursor cursor(view.payload); while(auto f = cursor.next())
		{
			if(f->type == 0x19 && f->value.size() == 2)
			{
				const auto value = static_cast<uint16_t>(uint16_t(f->value[0]) | (uint16_t(f->value[1]) << 8));
				out.offer({appearance_kind(value), KnowledgeSource::appearance, KnowledgeStrength::advertised, value});
				continue;
			}

			size_t width = 0; KnowledgeSource source = KnowledgeSource::service_uuid; bool data = false;
			switch(f->type)
			{
			case 2: case 3: width = 2; break; case 4: case 5: width = 4; break; case 6: case 7: width = 16; break;
			case 0x14: width = 2; source = KnowledgeSource::solicitation; break;
			case 0x1f: width = 4; source = KnowledgeSource::solicitation; break;
			case 0x15: width = 16; source = KnowledgeSource::solicitation; break;
			case 0x16: width = 2; data = true; break; case 0x20: width = 4; data = true; break; case 0x21: width = 16;
				data = true; break; default: break;
			}
			if(!width) continue;

			if(data)
			{
				if(f->value.size() >= width)
					service(out, *Uuid::from_wire(f->value.first(width)), KnowledgeSource::service_data);
			}
			else if(f->value.size() % width == 0)
				for(size_t i = 0; i < f->value.size(); i += width)
					service(out, *Uuid::from_wire(f->value.subspan(i, width)), source);
		}
		return out;
	}

	const char * name(DeviceKind v)
	{
		switch(v)
		{
#define KIND(x) case DeviceKind::x: return #x;
		KIND(unknown) KIND(phone) KIND(computer) KIND(watch) KIND(remote_control) KIND(keyboard) KIND(mouse)
		KIND(joystick) KIND(gamepad) KIND(tag) KIND(heart_rate_sensor) KIND(thermometer) KIND(blood_pressure_sensor)
		KIND(cycling_sensor) KIND(weight_scale) KIND(environmental_sensor) KIND(tire_pressure_sensor)
#undef KIND
		}
		return "unknown";
	}

	const char * name(KnowledgeSource v)
	{
		switch(v)
		{
#define SOURCE(x) case KnowledgeSource::x: return #x;
		SOURCE(none) SOURCE(local_name) SOURCE(service_uuid) SOURCE(service_data) SOURCE(solicitation) SOURCE(
			appearance) SOURCE(handler)
#undef SOURCE
		}
		return "none";
	}

	const char * name(KnowledgeStrength v)
	{
		switch(v)
		{
		case KnowledgeStrength::unknown: return "unknown"; case KnowledgeStrength::hint: return "hint";
		case KnowledgeStrength::advertised: return "advertised"; case KnowledgeStrength::decoded: return "decoded";
		}
		return "unknown";
	}

	const char * name(AdvertisedCapability v)
	{
		switch(v)
		{
#define CAP(x) case AdvertisedCapability::x: return #x;
		CAP(heart_rate) CAP(hid) CAP(battery) CAP(device_information) CAP(temperature) CAP(blood_pressure)
		CAP(cycling_speed_cadence) CAP(cycling_power) CAP(running_speed_cadence) CAP(weight)
		CAP(environmental_sensing) CAP(immediate_alert) CAP(link_loss) CAP(tire_pressure)
#undef CAP
		}
		return "unknown";
	}

}

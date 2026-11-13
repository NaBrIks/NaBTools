// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/bt/gap/classification.hpp"
#include <vector>
#include <stdexcept>
#include <iostream>
using namespace NaBTools::runtime;
void check(bool v, const char *message)
{
	if(!v)
		throw std::runtime_error(message);
}

AdvertisementClassification classify(std::initializer_list<uint8_t> bytes)
{
	AdvertisementView v; v.payload = Bytes(bytes.begin(), bytes.size()); return classify_advertisement(v);
}

int main()
{
	try{
		auto c = classify({12, 9, 'S', 'm', 'a', 'r', 't', 'R', 'e', 'm', 'o', 't', 'e'});
		check(c.type.kind == DeviceKind::remote_control && c.type.strength == KnowledgeStrength::hint && c.count == 0,
		    "name is only a hint");
		c = classify({3, 0x19, 0x80, 1, 5, 3, 0x12, 0x18, 0x0f, 0x18});
		check(c.type.kind == DeviceKind::remote_control && c.type.source == KnowledgeSource::appearance && c.count == 2,
		    "appearance plus advertised HID/battery");
		c = classify({3, 0x14, 0x0d, 0x18});
		check(c.type.kind == DeviceKind::unknown && c.count == 1 &&
		    c.capabilities[0].source == KnowledgeSource::solicitation, "wanted HRS is not provided HRS");
		c = classify({3, 3, 0x12, 0x18});
		check(c.type.kind == DeviceKind::unknown && c.count == 1, "HID alone does not imply remote");
		c = classify({3, 0x19, 0xc0, 0, 3, 3, 0x0d, 0x18});
		check(c.type.kind == DeviceKind::watch && c.count == 1 && !c.conflict, "watch may offer heart rate");
		c = classify({5, 5, 0x0d, 0x18, 0, 0}); check(c.type.kind == DeviceKind::heart_rate_sensor, "32-bit HRS");
		auto uuid = Uuid::from16(0x180d); std::vector<uint8_t> bytes{17, 7};
		bytes.insert(bytes.end(), uuid.bytes.begin(), uuid.bytes.end());
		AdvertisementView v; v.payload = bytes;
		check(classify_advertisement(v).count == 1, "128-bit Bluetooth base service");
		bytes[2] ^= 1; check(classify_advertisement(v).count == 0, "vendor UUID never guessed by suffix");
		c = classify({3, 0xff, 0x75, 0});
		check(c.type.kind == DeviceKind::unknown && c.count == 0, "company does not imply device kind");
		c = classify({3, 0x19, 0x80, 1, 3, 0x19, 0x40, 0}); check(c.conflict, "contradictory appearances exposed");
		c = classify({3, 0x16, 0x0d, 0x18});
		check(c.count == 1 && c.capabilities[0].source == KnowledgeSource::service_data,
		    "service-data source retained");
		check(classify({8, 3, 0x0d, 0x18}).count == 0, "malformed AD no claims");
		check(classify({4, 3, 0x0d, 0x18, 0}).count == 0, "partial UUID list no claim");
		v.legacy = false; check(classify_advertisement(v).count == 0, "extended fragments not decoded");
		c = classify({17, 3, 0x0d, 0x18, 0x12, 0x18, 0x0f, 0x18, 0x0a, 0x18, 0x09, 0x18, 0x10, 0x18, 0x16, 0x18, 0x18,
		              0x18});
		check(c.count == AdvertisementClassification::capacity && c.truncated, "bounded facts report truncation");
		AdvertisementClassification merged; merged.merge(classify({3, 3, 0x0d, 0x18}));
		merged.merge(classify({3, 0x19, 0xc0, 0}));
		check(merged.type.kind == DeviceKind::watch && merged.count == 1, "separate reports complement");
		std::cout << "PASS advertising classification, bounds and uncertainty\n"; return 0;
	}catch(const std::exception &e){std::cerr << e.what() << '\n'; return 1;}
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/bt/gap/ad_view.hpp"
#include "NaBTools/core/units.hpp"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cmath>
using namespace NaBTools::runtime;
void check(bool v, const char *text)
{
	if(!v)
		throw std::runtime_error(text);
}

int main()
{
	try{
		std::vector<uint8_t> bytes;
		const auto add = [&](uint8_t t, std::initializer_list<uint8_t> v){
				bytes.push_back(uint8_t(v.size() + 1)); bytes.push_back(t); bytes.insert(bytes.end(), v);
			};
		add(1, {0x1e}); add(0x0a, {0xf6}); add(9, {'t', 'e', 's', 't'});
		add(3, {0x0d, 0x18, 0x0f, 0x18}); add(5, {0x78, 0x56, 0x34, 0x12});
		add(0x14, {0x0d, 0x18}); add(0x1f, {0x78, 0x56, 0x34, 0x12});
		add(0xff, {0x75, 0, 0x42}); add(0xff, {0x87, 0});
		add(0x16, {0x0d, 0x18, 1, 2}); add(0x16, {0x0d, 0x18, 3}); add(0x20, {0x78, 0x56, 0x34, 0x12, 4});
		add(0x19, {0x40, 3}); add(0x1a, {32, 0}); add(0x1c, {2}); add(0x0d, {1, 2, 3}); add(0x11, {5});
		add(0x12, {6, 0, 0xff, 0xff});
		AdView ad(bytes); check(ad.well_formed(), "complete AD document");
		check(ad.flags()->general_discoverable() && !ad.flags()->limited_discoverable(), "flags bits");
		check(ad.flags()->simultaneous_host() && ad.flags()->simultaneous_controller(), "simultaneous flags");
		check(*ad.tx_power() == -10 && *ad.local_name() == "test", "signed power and borrowed name");
		check(ad.service_uuids(UuidWidth::bits16)->size() == 2, "UUID list size");
		check(ad.service_uuids(UuidWidth::bits16)->at(1) == Uuid::from16(0x180f), "16-bit UUID LE order");
		check(ad.service_uuids(UuidWidth::bits32)->at(0) == Uuid::from32(0x12345678), "32-bit UUID LE order");
		check(ad.solicitation_uuids(UuidWidth::bits32)->at(0) == Uuid::from32(0x12345678), "32-bit solicitation");
		check(ad.solicitation_uuids(UuidWidth::bits16)->at(0) == Uuid::from16(0x180d), "16-bit solicitation");
		check(*ad.manufacturer_id() == 0x75 && *ad.manufacturer_id(1) == 0x87, "duplicate manufacturers");
		auto data = ad.service_data(Uuid::from16(0x180d));
		check(data && data->data() >= bytes.data() && data->data() + data->size() <= bytes.data() + bytes.size(),
		    "borrowed service bytes");
		check((*data)[0] == 1 && (*ad.service_data(Uuid::from16(0x180d), UuidWidth::bits16, 1))[0] == 3,
		    "duplicate service data");
		check((*ad.service_data(Uuid::from32(0x12345678), UuidWidth::bits32))[0] == 4, "32-bit service data");
		check(*ad.appearance() == 0x340 && *ad.advertising_interval() == 32 && *ad.le_role() == 2, "scalar fields");
		check(*ad.class_of_device() == 0x030201 && *ad.sm_oob_flags() == 5, "class and OOB bits");
		check(ad.connection_interval_range()->minimum == 6 && *ad.connection_interval_range()->minimum_ms() == 7.5 &&
		    !ad.connection_interval_range()->maximum_ms(), "16-bit connection interval bounds");
		auto uuid128 = Uuid::from16(0x180d).bytes;
		std::vector<uint8_t> long_fields{17, 7}; long_fields.insert(long_fields.end(), uuid128.begin(), uuid128.end());
		long_fields.insert(long_fields.end(), {17, 0x15});
		long_fields.insert(long_fields.end(), uuid128.begin(), uuid128.end());
		long_fields.insert(long_fields.end(), {18, 0x21});
		long_fields.insert(long_fields.end(), uuid128.begin(), uuid128.end()); long_fields.push_back(42);
		AdView ad128(long_fields);
		check(ad128.service_uuids(UuidWidth::bits128)->at(0) == Uuid::from16(0x180d), "128-bit list");
		check(ad128.solicitation_uuids(UuidWidth::bits128)->at(0) == Uuid::from16(0x180d), "128-bit solicitation");
		check((*ad128.service_data(Uuid::from16(0x180d), UuidWidth::bits128))[0] == 42, "128-bit service data");
		std::vector<uint8_t> tk{17, 0x10}; tk.insert(tk.end(), uuid128.begin(), uuid128.end());
		auto tk_view = AdView(tk).sm_tk_data();
		check(tk_view && tk_view->data() == tk.data() + 2, "borrowed 16-byte TK");
		std::array<uint8_t, 3> short_manufacturer{2, 0xff, 0x75};
		check(!AdView(short_manufacturer).manufacturer_id(), "one-byte manufacturer ID rejected");
		for(size_t n = 1; n < 16; ++n)
		{
			std::vector<uint8_t> bad{uint8_t(n + 1), 7}; bad.resize(n + 2);
			check(!AdView(bad).service_uuids(UuidWidth::bits128), "partial UUID rejected");
		}
		for(uint8_t t:{1, 0x0a, 0x19, 0x1a, 0x1c, 0xff, 0x12})
		{
			std::array<uint8_t, 2> empty{1, t}; AdView v(empty);
			check(!v.flags() && !v.tx_power() && !v.appearance() && !v.advertising_interval() && !v.le_role() &&
			    !v.manufacturer_id() && !v.connection_interval_range(), "short fields rejected");
		}
		std::array<uint8_t, 3> broken{9, 0xff, 0}; check(!AdView(broken).well_formed(), "malformed length");
		RadioInfo r; r.event_properties = 15; r.primary_phy = 3; r.secondary_phy = 2; r.tx_power = -18;
		r.periodic_interval = 80; r.directed_address = {{1, 2, 3, 4, 5, 6}, 1}; r.data_status = 2;
		AdvertisementView view; view.payload = bytes; view.radio = r; auto owned = Advertisement::copy(view);
		check(owned && owned->view().radio->directed_address == r.directed_address, "owned metadata"); bytes[2] = 0;
		check(owned->data[2] == 0x1e, "owned bytes independent");
		std::vector<uint8_t> oversized(Advertisement::capacity + 1); view.payload = oversized;
		check(!Advertisement::copy(view), "oversized owned packet rejected");
		check(r.connectable() && r.scannable() && r.directed() && r.scan_response(), "event properties");
		check(*r.tx_power_dbm() == -18 && *r.periodic_interval_ms() == 100, "radio units"); r.tx_power = 127;
		r.periodic_interval = 0; check(!r.tx_power_dbm() && !r.periodic_interval_ms(), "radio sentinels");
		check(std::string(phy_name(77)) == "Unknown" && std::string(data_status_name(77)) == "Unknown",
		    "unknown codes");
		check(Uuid::from16(0x180d).assigned16() == 0x180d, "assigned UUID"); uuid128[0] ^= 1;
		check(!Uuid{uuid128}.assigned16(), "vendor UUID");
		check(std::abs(Pressure{101325}.atmospheres() - 1) < 1e-12 && Pressure{100000}.bars() == 1, "pressure units");
		check(Temperature::from_centi_c(-4000).fahrenheit() == -40, "signed temperature");
		std::cout << "PASS AD views, UUIDs, duplicates, ownership and radio metadata\n"; return 0;
	}catch(const std::exception &e){std::cerr << e.what() << '\n'; return 1;}
}

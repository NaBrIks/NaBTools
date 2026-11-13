// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/catalogs/sig/sig.hpp"
#include <cstddef>
namespace NaBTools::runtime::sig
{
#if NABTOOLS_SIG_PROFILE > 0
	struct Entry {uint16_t code; const char *label;};
#include <sig.generated.inc>
	template<size_t N>const char * find(const Entry (&items)[N], uint16_t code)
	{
		size_t first = 0, last = N; while(first < last)
		{
			const auto mid = first + (last - first) / 2;
			if(items[mid].code < code) first = mid + 1; else last = mid;}
		return first < N && items[first].code == code ? items[first].label : nullptr;
	}

#endif
	const char * lookup(Table table, uint16_t code)
	{
#if NABTOOLS_SIG_PROFILE > 0
		switch(table)
		{
		case Table::company: return find(company_names, code);
		case Table::ad_type: return find(ad_type_names, code);
		case Table::service: return find(service_names, code);
		case Table::characteristic: return find(characteristic_names, code);
		case Table::descriptor: return find(descriptor_names, code);
		case Table::declaration: return find(declaration_names, code);
		case Table::unit: return find(unit_names, code);
		case Table::service_class: return find(service_class_names, code);
		case Table::protocol: return find(protocol_names, code);
#if NABTOOLS_SIG_PROFILE >= 2
		case Table::member: return find(member_names, code);
#else
		case Table::member: return nullptr;
#endif
#if NABTOOLS_SIG_PROFILE >= 2
		case Table::sdo: return find(sdo_names, code);
#else
		case Table::sdo: return nullptr;
#endif
		case Table::browse_group: return find(browse_group_names, code);
#if NABTOOLS_SIG_PROFILE >= 2
		case Table::mesh_profile: return find(mesh_profile_names, code);
#else
		case Table::mesh_profile: return nullptr;
#endif
		case Table::object_type: return find(object_type_names, code);
		case Table::appearance: return find(appearance_names, code);
		}
#else
		(void)table; (void)code;
#endif
		return nullptr;
	}

	const char * company(uint16_t code)
	{
		return lookup(Table::company, code);
	}

	const char * ad_type(uint8_t code)
	{
		return lookup(Table::ad_type, code);
	}

	const char * service(uint16_t code)
	{
		return lookup(Table::service, code);
	}

	const char * characteristic(uint16_t code)
	{
		return lookup(Table::characteristic, code);
	}

	const char * descriptor(uint16_t code)
	{
		return lookup(Table::descriptor, code);
	}

	const char * declaration(uint16_t code)
	{
		return lookup(Table::declaration, code);
	}

	const char * unit(uint16_t code)
	{
		return lookup(Table::unit, code);
	}

	const char * service_class(uint16_t code)
	{
		return lookup(Table::service_class, code);
	}

	const char * protocol(uint16_t code)
	{
		return lookup(Table::protocol, code);
	}

	const char * member(uint16_t code)
	{
		return lookup(Table::member, code);
	}

	const char * sdo(uint16_t code)
	{
		return lookup(Table::sdo, code);
	}

	const char * browse_group(uint16_t code)
	{
		return lookup(Table::browse_group, code);
	}

	const char * mesh_profile(uint16_t code)
	{
		return lookup(Table::mesh_profile, code);
	}

	const char * object_type(uint16_t code)
	{
		return lookup(Table::object_type, code);
	}

	const char * appearance(uint16_t code)
	{
		return lookup(Table::appearance, code);
	}

	const char * uuid(const Uuid &value)
	{
		auto code = value.assigned16(); if(!code) return nullptr;

		for(const auto table:{Table::service, Table::characteristic, Table::descriptor, Table::declaration, Table::unit,
		                      Table::service_class, Table::protocol, Table::member, Table::sdo, Table::browse_group,
		                      Table::mesh_profile, Table::object_type})
			if(auto label = lookup(table, *code)) return label;

		return nullptr;
	}

	const char * profile()
	{
		return NABTOOLS_SIG_PROFILE == 0 ? "OFF" : NABTOOLS_SIG_PROFILE == 1 ? "COMMON" : NABTOOLS_SIG_PROFILE ==
		       2 ? "EXTENDED" : "FULL";
	}

	Coverage coverage()
	{
		return NABTOOLS_SIG_PROFILE == 0 ? Coverage::disabled : NABTOOLS_SIG_PROFILE <
		       3 ? Coverage::curated : Coverage::complete_supported_tables;
	}

}

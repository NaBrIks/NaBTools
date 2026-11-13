// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/core/config.hpp"
#include <cstdint>
#include "NaBTools/core/uuid.hpp"
namespace NaBTools::runtime::sig
{
	enum class Coverage { disabled, curated, complete_supported_tables };
	const char * profile();
	Coverage coverage();
	enum class Table { company, ad_type, service, characteristic, descriptor, declaration, unit,
		               service_class, protocol, member, sdo, browse_group, mesh_profile, object_type, appearance };
	const char *lookup(Table, uint16_t);
// Bluetooth-base UUIDs only; vendor 128-bit UUIDs are never guessed by suffix.
	const char * uuid(const Uuid &);
	const char *company(uint16_t);
	const char *ad_type(uint8_t);
	const char *service(uint16_t);
	const char *characteristic(uint16_t);
	const char *descriptor(uint16_t);
	const char *declaration(uint16_t);
	const char *unit(uint16_t);
	const char *service_class(uint16_t);
	const char *protocol(uint16_t);
	const char *member(uint16_t);
	const char *sdo(uint16_t);
	const char *browse_group(uint16_t);
	const char *mesh_profile(uint16_t);
	const char *object_type(uint16_t);
	const char *appearance(uint16_t);
}

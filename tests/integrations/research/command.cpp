// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "command.hpp"
#include "NaBTools/core/cbor.hpp"
#include <array>
#include <iostream>
using namespace NaBTools::runtime;
int main()
{
	std::array<uint8_t, 96> storage; CborWriter w(storage); w.map(1); w.text("scan"); w.boolean(true);
	auto good = w.result(); auto cmd = research::parse_command(good); if(!cmd || !cmd->enabled) return 1;

	for(size_t n = 0; n < good.size(); ++n)
		if(research::parse_command(good.first(n))) return 2;

	CborWriter r(storage); r.map(2); r.text("remove_slot"); r.number(31); r.text("generation"); r.number(UINT32_MAX);
	cmd = research::parse_command(r.result());
	if(!cmd || cmd->device.slot != 31 || cmd->device.generation != UINT32_MAX) return 3;

	CborWriter bad(storage); bad.map(2); bad.text("scan"); bad.boolean(true); bad.text("scan"); bad.boolean(false);
	if(research::parse_command(bad.result())) return 4;

	CborWriter out(storage); out.map(2); out.text("remove_slot"); out.number(32); out.text("generation"); out.number(1);
	if(research::parse_command(out.result())) return 5;

	std::array<uint8_t, 160> addbuf; CborWriter add(addbuf); add.map(5); add.text("add_handler");
	add.text("nabtools.samsung.smarttag");
	add.text("address"); std::array<uint8_t, 6> address{1, 2, 3, 4, 5, 6}; add.bytes(address);
	add.text("address_type"); add.number(1); add.text("sid"); add.number(255); add.text("request_id");
	add.number(1234567890123ULL);
	cmd = research::parse_command(add.result());
	if(!cmd || cmd->kind != research::Command::add || cmd->address.bytes != address ||
	    cmd->request_id != 1234567890123ULL) return 6;

	for(size_t n = 0; n < add.result().size(); ++n)
		if(research::parse_command(add.result().first(n))) return 7;

	CborWriter invoke(addbuf); invoke.map(5); invoke.text("invoke_slot"); invoke.number(2); invoke.text("generation");
	invoke.number(3);
	invoke.text("invoke"); invoke.text("alert.start"); invoke.text("arguments"); const uint8_t args[]{0xa0};
	invoke.bytes(args);
	invoke.text("request_id"); invoke.number(444);
	cmd = research::parse_command(invoke.result());
	if(!cmd || cmd->kind != research::Command::invoke || cmd->argument_size != 1 || cmd->arguments[0] != 0xa0 ||
	    cmd->request_id != 444) return 8;

	for(size_t n = 0; n < invoke.result().size(); ++n)
		if(research::parse_command(invoke.result().first(n))) return 9;

	std::array<uint8_t, 192> bootbuf; CborWriter boot(bootbuf); boot.map(6);
	boot.text("invoke_slot"); boot.number(2); boot.text("generation"); boot.number(3); boot.text("invoke");
	boot.text("connection.connect");
	boot.text("arguments"); boot.bytes(args); boot.text("request_id"); boot.number(444); boot.text("boot_id");
	boot.number(9007199254740991ULL);
	cmd = research::parse_command(boot.result());
	if(!cmd || cmd->boot_id != 9007199254740991ULL || cmd->kind != research::Command::invoke) return 10;

	for(size_t n = 0; n < boot.result().size(); ++n)
		if(research::parse_command(boot.result().first(n))) return 11;

	CborWriter snapshot(storage); snapshot.map(1); snapshot.text("snapshot"); snapshot.boolean(true);
	cmd = research::parse_command(snapshot.result()); if(!cmd || cmd->kind != research::Command::snapshot) return 12;

	for(size_t n = 0; n < snapshot.result().size(); ++n)
		if(research::parse_command(snapshot.result().first(n))) return 13;

	CborWriter mixed(storage); mixed.map(2); mixed.text("snapshot"); mixed.boolean(true); mixed.text("scan");
	mixed.boolean(true);
	if(research::parse_command(mixed.result())) return 14;

	for(const auto *mode:{"active", "passive"})
	{
		CborWriter m(storage); m.map(1); m.text("scan_mode"); m.text(mode);
		cmd = research::parse_command(m.result()); if(!cmd || cmd->kind != research::Command::scan_mode) return 15;

		if(cmd->mode != (mode[0] == 'a' ? ScanMode::active : ScanMode::passive)) return 16;

		for(size_t n = 0; n < m.result().size(); ++n)
			if(research::parse_command(m.result().first(n)))
				return 17;
	}
	for(const auto *mode:{"ACTIVE", "", "connect"})
	{
		CborWriter m(storage); m.map(1); m.text("scan_mode"); m.text(mode); if(research::parse_command(m.result()))
			return 18;
	}
	CborWriter mode_mix(storage); mode_mix.map(2); mode_mix.text("scan_mode"); mode_mix.text("active");
	mode_mix.text("scan"); mode_mix.boolean(true);
	if(research::parse_command(mode_mix.result())) return 19;

	CborWriter mode_duplicate(storage); mode_duplicate.map(2); mode_duplicate.text("scan_mode");
	mode_duplicate.text("active"); mode_duplicate.text("scan_mode"); mode_duplicate.text("passive");
	if(research::parse_command(mode_duplicate.result())) return 20;

	std::cout << "PASS command boundaries\n";
}

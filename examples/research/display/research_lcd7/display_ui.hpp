// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/research/research.hpp"
#include "command.hpp"
#include "journal.hpp"
namespace research_lcd7
{
	using Submit = bool (*)(void *, const NaBTools::runtime::research::Command &);
// All calls, widget events and engine reads belong to the application task.
	void create(NaBTools::runtime::BtEngine &, NaBTools::runtime::ResearchModule &,
	    NaBTools::runtime::research::MessageJournal &, void *, Submit);
	void update(uint64_t now_ms, const char *network);
	void control_result(uint64_t request, NaBTools::runtime::Status);
}

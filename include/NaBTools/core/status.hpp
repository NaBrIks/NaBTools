// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
namespace NaBTools::runtime
{
	enum class Status { ok, busy, invalid, full, expired, unsupported, backend_error };
	const char *name(Status);
}

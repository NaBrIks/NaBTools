// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "NaBTools/bt/engine.hpp"

namespace NaBTools::runtime
{
	enum class Delivery { accepted, dropped, error };
	struct MessageInfo { DeviceId device; uint64_t time_ms{}, sequence{}; const char *schema{}; };
// Borrowed, synchronous delivery. The application owns context and copies bytes
// when retaining a message. Declaring this hook does not link a CBOR codec.
	struct CborAction {void *context{}; Delivery (*invoke)(void *, const MessageInfo &, Bytes)
					   {
					   };};
}

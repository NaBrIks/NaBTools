// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools/bt/gatt/client.hpp"
namespace NaBTools::runtime
{
	const char * name(GattEventKind kind)
	{
		switch(kind)
		{
		case GattEventKind::connected: return "connected"; case GattEventKind::disconnected: return "disconnected";
		case GattEventKind::service: return "service"; case GattEventKind::characteristic: return "characteristic";
		case GattEventKind::descriptor: return "descriptor";
		case GattEventKind::discovery_complete: return "discovery_complete";
		case GattEventKind::subscribed: return "subscribed"; case GattEventKind::read: return "read";
		case GattEventKind::write: return "write"; case GattEventKind::notification: return "notification";
		case GattEventKind::error: return "error";
		}
		return "unknown";
	}

}

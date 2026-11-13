// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools_espidf.hpp"
#include "NaBTools/bt/gap/ad_view.hpp"
#include "NaBTools/catalogs/sig/sig.hpp"
#include "NaBTools/devices/tomtom/tpms/tpms.hpp"
#include "NaBTools/research/research.hpp"
#include "esp_log.h"

// Link-only composition example. start() is deliberately not called: flashing
// this smoke fixture does not initialize a radio or change device settings.
extern "C" void app_main()
{
	using namespace NaBTools::runtime;
	BtEngine engine(std::make_unique<EspIdfBackend>());
	engine.add_module(std::make_unique<ResearchModule>());
	engine.add_module(std::make_unique<TomTomModule>(false, TpmsActions{}));
	const uint8_t bytes[]{2, 1, 6, 3, 3, 0x0d, 0x18}; AdView ad(bytes);
	const auto uuid = ad.service_uuids(UuidWidth::bits16)->at(0);
	const char *label = sig::uuid(*uuid);
	ESP_LOGI("NaBTools_smoke", "AD valid=%d service=%s", ad.well_formed(), label ? label : "unknown");
}

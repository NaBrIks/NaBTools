// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "display_ui.hpp"
#include "cbor_text.hpp"
#include "lvgl.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace NaBTools::runtime;
static void require(bool condition, const char *why)
{
	if(!condition)
		throw std::runtime_error(why);
}

class Backend final : public IBackend
{
public:
	ScanState state{ScanState::idle}; unsigned next{};
	Status start()override
	{
		state = ScanState::scanning; return Status::ok;
	}

	void pump()override
	{
	}

	Status scan(bool enable)override
	{
		state = enable ? ScanState::scanning : ScanState::stopped; return Status::ok;
	}

	BackendSnapshot snapshot()const override
	{
		return {state, 0, next, 0};
	}

	bool take(Advertisement &out)override
	{
		if(next == 3)return false;

		static constexpr uint8_t payload[]{2, 1, 6, 3, 0xff, 0x75, 0, 7, 9, 'S', 'e', 'n', 's', 'o', 'r'};
		out = {}; out.address.bytes = {2, 0, 0, 0, 0, static_cast<uint8_t>(++next)};
		out.rssi = static_cast<int8_t>(-40 - int(next) * 8); out.time_ms = 1000; out.size = sizeof(payload);
		std::copy(std::begin(payload), std::end(payload), out.data.begin()); return true;
	}

};
class Recognizer final : public IModule
{
public:
	const char * key()const override
	{
		return "fixture";
	}

	void observe(BtEngine &, AdvertisementView, AdStatus)override
	{
	}

	std::optional<Detection> recognize(AdvertisementView)const override
	{
		return Detection{"Fixture sensor", "Synthetic fixture; not a physical device", true};
	}

};
class Device final : public IDevice
{
public:
	DeviceInfo info()const override
	{
		return {{}, { {2, 0, 0, 0, 0, 1}, 0}, "Fixture sensor", 1000, 7, 0, 0};
	}

	void on_advertisement(BtEngine &, DeviceId, AdvertisementView)override
	{
	}

};
static lv_obj_t * find(lv_obj_t *parent, const lv_obj_class_t *type, const char *title = nullptr)
{
	if(lv_obj_check_type(parent, type) && (!title || !std::strcmp(lv_label_get_text(parent), title)))return parent;

	for(uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
		if(auto *found = find(lv_obj_get_child(parent, i), type, title)) return found;

	return nullptr;
}

static void click(const char *title)
{
	auto *item = find(lv_screen_active(), &lv_label_class, title); require(item, "button label missing");
	lv_obj_send_event(lv_obj_get_parent(item), LV_EVENT_CLICKED, nullptr);
}

static void select(unsigned row)
{
	auto *table = find(lv_screen_active(), &lv_table_class); require(table, "table missing");
	lv_table_set_selected_cell(table, row, 0); lv_obj_send_event(table, LV_EVENT_VALUE_CHANGED, nullptr);
}

int main(int argc, char **argv)
{
	try{
		const std::string directory = argc > 1 ? argv[1] : ".";
		auto backend = std::make_unique<Backend>(); auto *hardware = backend.get(); BtEngine engine(std::move(backend));
		auto observer = std::make_unique<ResearchModule>(); auto *research = observer.get();
		engine.add_module(std::move(observer)); engine.add_module(std::make_unique<Recognizer>());
		require(engine.start() == Status::ok, "engine start"); engine.poll();
		auto id = engine.add_device(std::make_unique<Device>()); research::MessageJournal journal;
		std::array<uint8_t, 128> bytes; CborWriter writer(bytes); writer.map(3);
		writer.text("measurement"); writer.integer(-42); writer.text("active"); writer.boolean(true);
		writer.text("samples"); writer.array(2); writer.number(800); writer.number(810);
		journal.push({id, 1000, 9, "fixture.measurement.v1"}, writer.result());
		auto rendered = research_lcd7::cbor_text(writer.result());
		require(rendered.find("measurement: -42") != std::string::npos &&
		    rendered.find("active: true") != std::string::npos, "generic CBOR fields");
		for(size_t i = 0; i < writer.result().size(); ++i)
			require(research_lcd7::cbor_text(writer.result().first(i)).size() <= 2048, "bounded truncated CBOR");
		lv_init(); auto *display = lv_display_create(800, 480); std::vector<uint8_t> pixels(800 * 480 * 3);
		lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
		lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size(), LV_DISPLAY_RENDER_MODE_FULL);
		lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *){
			lv_display_flush_ready(d);
		});
		std::vector<research::Command> commands;
		research_lcd7::create(engine, *research, journal, &commands,
		    [](void *context, const research::Command &command){
			static_cast<std::vector<research::Command> *>(context)->push_back(command); return true;
		});
		research_lcd7::update(2500, "Headless LVGL | synthetic data");
		auto capture = [&](const char *file){
				lv_refr_now(display); std::ofstream out(directory + "/" + file, std::ios::binary);
				out << "P6\n800 480\n255\n"; for(size_t i = 0; i < pixels.size(); i += 3)
				{
					out.put(pixels[i + 2]); out.put(pixels[i + 1]); out.put(pixels[i]);
				}
			};
		require(lv_table_get_row_count(find(lv_screen_active(), &lv_table_class)) == 4, "three air rows");
		select(1); click("Add");
		require(commands.back().kind == research::Command::add && commands.back().address.bytes[5] == 1,
		    "add selected observation");
		research_lcd7::control_result(commands.back().request_id, Status::ok); capture("air.ppm");
		click("Pause scan");
		require(commands.back().kind == research::Command::scan && !commands.back().enabled,
		    "scan command through queue");
		hardware->scan(false); research_lcd7::update(3000, "Headless LVGL | synthetic data");
		require(find(lv_screen_active(), &lv_label_class, "Start scan"), "confirmed scan label");
		click("Devices"); select(1); capture("devices.ppm"); click("Remove");
		require(commands.back().kind == research::Command::remove && commands.back().device == id,
		    "remove generation-aware instance");
		click("CBOR"); select(1); capture("cbor.ppm");
		for(unsigned i = 0; i < 17; ++i)
			journal.push({id, 3000, i, "fixture.measurement.v1"}, writer.result());
		research_lcd7::update(4000, "");
		require(find(lv_screen_active(), &lv_label_class, "Message evicted from bounded journal."),
		    "selected message not silently rebound after eviction");
		click("Devices"); engine.remove_device(id); engine.add_device(std::make_unique<Device>());
		research_lcd7::update(4500, "");
		require(find(lv_screen_active(), &lv_label_class, "Instance removed. Select another row."),
		    "stale instance generation stays invalid");
		const auto count = commands.size(); click("Run");
		require(commands.size() == count, "stale selection cannot issue another instance's command");
		std::cout <<
		    "Research screen: air selection, queued scan/add/remove, generic CBOR, bounded truncations and three LVGL renders passed\n";
		lv_display_delete(display); lv_deinit(); return 0;
	}catch(const std::exception &e){std::cerr << e.what() << '\n'; return 1;}
}

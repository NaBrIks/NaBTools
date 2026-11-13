// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "NaBTools_espidf.hpp"
#include "NaBTools/research/research.hpp"
#include "NaBTools/devices/tomtom/tpms/tpms.hpp"
#include "NaBTools/devices/samsung/smarttag/smarttag.hpp"
#include "samsung/smarttag_crypto.hpp"
#if __has_include("smarttag_owner_secret.hpp")
#include "smarttag_owner_secret.hpp"
#endif
#include "NaBTools/profiles/heart_rate/hrm.hpp"
#include "command.hpp"
#include "journal.hpp"
#include "live_views.hpp"
#include "live_transport.hpp"
#include "assets.generated.hpp"
#include <NaWServer/server.h>
#include <NaWServer/route.h>
#include <NaWServer/request.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "freertos/task.h"
#include <mutex>
#include <vector>
#include <cstring>
#include <atomic>
#include <cstdio>
#if NABTOOLS_LCD7
#include "board.hpp"
#include "display_ui.hpp"
#include "lvgl.h"
#endif
#if __has_include("wifi_secret.hpp")
#include "wifi_secret.hpp"
#else
#define NABTOOLS_WIFI_SSID ""
#define NABTOOLS_WIFI_PASSWORD ""
#endif

using namespace NaBTools::runtime;
using namespace NaWServer;
namespace
{
	constexpr uint16_t http_port = 80;
	uint64_t boot_id;
	std::atomic<uint32_t> network_address{};
	std::mutex snapshot_mutex;
// One engine writer and synchronous HTTP readers. Pinning a published buffer
// lets BLE keep running during slow HTTP sends without allocating snapshots.
	struct Snapshot {std::array<uint8_t, 32768> bytes; size_t size{}, readers{}; uint64_t sequence{};};
	std::array<Snapshot, 2> snapshots;
	int published_snapshot = -1;
	StaticQueue_t command_control;
	alignas(4) uint8_t command_storage[8 * sizeof(research::Command)];
	QueueHandle_t commands;
	research::MessageJournal journal;
	LiveTransport live;
	research::LiveViews live_views;
	struct ControlResult {uint64_t request_id{}; Status status{}; DeviceId device;};
	std::array<ControlResult, 8> control_results;
	size_t control_count{}, control_next{};
	void write_control(CborWriter &w, const ControlResult &c)
	{
		w.map(4); w.text("request_id"); w.number(c.request_id); w.text("result"); w.text(name(c.status));
		w.text("slot"); w.number(c.device.slot); w.text("generation"); w.number(c.device.generation);
	}

	class Assets final : public HandlerBase
	{
		bool canHandle(Request *r)override
		{
			if(r->method() != NaWServer::HttpMethod::Get) return false;

			for(const auto &a:research_assets::files)
				if(r->path() == a.path) return true;

			return false;
		}

		void handleRequest(Request *r, Response &response)override
		{
			for(const auto &a:research_assets::files)
				if(r->path() == a.path)
				{
					if(response.header("Content-Encoding", "gzip") != SendResult::Ok) return;

					(void)response.send(200, a.mime, {a.data, a.size}); return;
				}
		}

	} assets;
	bool state(void *context, bool (*send)(void *, Bytes))
	{
		Snapshot *snapshot = nullptr;
		const auto fence = live.stream().sequence();
		// A cached snapshot older than the request can omit events emitted before
		// this peer joined. Wait for the engine's next complete publication at or
		// beyond the request cursor. Never hold the snapshot mutex while waiting
		// or sending: the BT producer must continue to advance independently.
		for(unsigned n = 0; n < 100 && !snapshot; ++n)
		{
			{std::lock_guard lock(snapshot_mutex);
			 if(published_snapshot >= 0 && snapshots[published_snapshot].sequence >= fence)
			 {
				 snapshot = &snapshots[published_snapshot]; ++snapshot->readers;
			 }
			}
			if(!snapshot) vTaskDelay(pdMS_TO_TICKS(10));
		}
		if(!snapshot) return false;

		const auto result = send(context, {snapshot->bytes.data(), snapshot->size});
		{std::lock_guard lock(snapshot_mutex); --snapshot->readers;}
		return result;
	}

	Status submit(Bytes body, uint64_t &request)
	{
		auto parsed = research::parse_command(body); if(!parsed) return Status::invalid;

		request = parsed->request_id;
		if(parsed->kind == research::Command::snapshot) return Status::invalid;

		if(parsed->boot_id != boot_id) return Status::expired;

		return xQueueSend(commands, &*parsed, 0) == pdTRUE ? Status::ok : Status::full;
	}

	HandlerBase * const routes[]{live.route(), &assets};
// Release browser asset pools: seven MCU slots must also serve other clients.
	Server server(routes, http_port, {}, ResponseOptions{ContentType::Default,
	                                                     CachePolicy::NoStore, HeaderFlags::NoSniff,
	                                                     ConnectionPolicy::Close});
	void network(void *, esp_event_base_t base, int32_t id, void *data)
	{
		if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
		else if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
		{
			network_address.store(0);
			ESP_LOGW("NaBTools", "Wi-Fi disconnected reason=%u",
			    static_cast<wifi_event_sta_disconnected_t *>(data)->reason);
			esp_wifi_connect();
		}
		else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
		{
			auto *ip = static_cast<ip_event_got_ip_t *>(data);
			network_address.store(ip->ip_info.ip.addr);
			ESP_LOGI("NaBTools", "Research URL http://" IPSTR "/", IP2STR(&ip->ip_info.ip));
		}
	}

	void wifi()
	{
		if(!*NABTOOLS_WIFI_SSID)
		{
			ESP_LOGW("NaBTools", "Wi-Fi unconfigured; passive BLE/serial remain available"); return;
		}

		ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
		esp_netif_create_default_wifi_sta(); wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
#if NABTOOLS_LCD7
		// The RGB bounce buffers and both radios need internal DMA-capable DRAM.
		// The unmodified display port exhausted it during esp_wifi_init on LCD7.
		// Bound the research UI's Wi-Fi buffering; PSRAM cannot replace static RX.
		// https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/wifi.html#wi-fi-buffer-usage
		cfg.static_rx_buf_num = 4; cfg.static_tx_buf_num = 4; cfg.cache_tx_buf_num = 8;
		cfg.dynamic_rx_buf_num = 16; cfg.rx_ba_win = 4;
#endif
		ESP_ERROR_CHECK(esp_wifi_init(&cfg)); ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
		ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network, nullptr));
		ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network, nullptr));
		wifi_config_t sta{};
		strlcpy(reinterpret_cast<char *>(sta.sta.ssid), NABTOOLS_WIFI_SSID, sizeof(sta.sta.ssid));
		strlcpy(reinterpret_cast<char *>(sta.sta.password), NABTOOLS_WIFI_PASSWORD, sizeof(sta.sta.password));
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
		ESP_ERROR_CHECK(esp_wifi_start()); ESP_ERROR_CHECK(server.begin());
	}

}
extern "C" void app_main()
{
	boot_id = ((uint64_t(esp_random()) << 21) ^ esp_random()) & 9007199254740991ULL;
	if(!boot_id)boot_id = 1;

	// Never erase NVS automatically: calibration and application settings may be present.
	ESP_ERROR_CHECK(nvs_flash_init());
	commands = xQueueCreateStatic(8, sizeof(research::Command), command_storage, &command_control);
	if(!live.begin(boot_id, submit, state))std::abort();

#if NABTOOLS_LCD7
	ESP_ERROR_CHECK(initialize_display());
	lv_tick_set_cb([]()->uint32_t {
		return static_cast<uint32_t>(esp_timer_get_time() / 1000);
	});
#endif
	journal.on_entry(&live.stream(), [](void *context, const research::MessageJournal::Entry &entry){
		static_cast<research::EventStream *>(context)->emit("message", [&](CborWriter &w){
			research::MessageJournal::write_entry(w, entry);
		});
	});
	wifi();
	static BtEngine engine(std::make_unique<EspIdfBackend>());
	auto research_module = std::make_unique<ResearchModule>(); auto *research_view = research_module.get();
	engine.add_module(std::move(research_module)); TpmsActions output;
#if NABTOOLS_TPMS_CBOR
	output.cbor = journal.action();
#endif
	// All recognizers are passive; this application admits devices only on Add.
	engine.add_module(std::make_unique<TomTomModule>(false, output));
	SmartTagActions tag_output;
#if NABTOOLS_SMARTTAG_CBOR
	tag_output.cbor = journal.action();
#endif
	SmartTagOwnerAccess tag_owner;
#if __has_include("smarttag_owner_secret.hpp")
	// One development credential, tried only on an explicitly selected instance
	// via owner.authenticate. Mutual proof must succeed before any control write.
	tag_owner = {espidf_smarttag_crypto(), nullptr, [](void *, DeviceId, Address, std::span<uint8_t, 16> out){
					 std::copy(research_smarttag_master_secret.begin(), research_smarttag_master_secret.end(),
					     out.begin()); return true;
				 }};
#endif
	engine.add_module(std::make_unique<SmartTagModule>(tag_output, tag_owner));
	HeartRateActions hrm_output;
#if NABTOOLS_HRM_CBOR
	hrm_output.cbor = journal.action();
#endif
	engine.add_module(std::make_unique<HeartRateModule>(hrm_output));
#if NABTOOLS_LCD7
	research_lcd7::create(engine, *research_view, journal, nullptr, [](void *, const research::Command &command){
		return xQueueSend(commands, &command, 0) == pdTRUE;
	});
	uint64_t next_display = 0;
#endif
	auto status = engine.start(); ESP_LOGI("NaBTools", "engine start accepted=%d", status == Status::ok);
	uint64_t next_snapshot = 0, next_log = 0;
	for(;;)
	{
		bool controls_changed = false;
		research::Command cmd;
		for(size_t i = 0; i < 8 && xQueueReceive(commands, &cmd, 0) == pdTRUE; ++i)
		{
			Status result = Status::invalid; DeviceId device;
			if(cmd.kind == research::Command::scan)result = engine.scan(cmd.enabled);
			else if(cmd.kind == research::Command::scan_mode)result = engine.set_scan_mode(cmd.mode);
			else if(cmd.kind == research::Command::remove)result = engine.remove_device(cmd.device);
			else if(cmd.kind == research::Command::invoke)
			{
				device = cmd.device;
				result = engine.invoke(device,
				        {cmd.operation.data(), "application/cbor", {cmd.arguments.data(), cmd.argument_size},
				         cmd.request_id, static_cast<uint64_t>(esp_timer_get_time() / 1000)});
			}
			else if(const auto *row = research_view->find(cmd.address, cmd.sid))
			{
				result = engine.attach(cmd.handler.data(), row->packet.view(), device);
			}
			else result = Status::expired;

			control_results[control_next] = {cmd.request_id, result, device};
			control_next = (control_next + 1) % control_results.size();
			control_count = std::min(control_count + 1, control_results.size());
			live.stream().emit("control", [&](CborWriter &w){
				write_control(w, {cmd.request_id, result, device});
			}); controls_changed = true;
#if NABTOOLS_LCD7
			research_lcd7::control_result(cmd.request_id, result);
#endif
			ESP_LOGI("NaBTools", "command request=%llu result=%s", static_cast<unsigned long long>(cmd.request_id),
			    name(result));
		}
		if(status == Status::ok)engine.poll();

		const uint64_t now = static_cast<uint64_t>(esp_timer_get_time() / 1000);
#if NABTOOLS_LCD7
		if(now >= next_display)
		{
			next_display = now + 250; esp_ip4_addr_t address{}; address.addr = network_address.load(); char network[48];
			if(address.addr)std::snprintf(network, sizeof(network), "IP " IPSTR, IP2STR(&address));
			else std::snprintf(network, sizeof(network), "IP offline");

			research_lcd7::update(now, network);
		}

		(void)lv_timer_handler();
#endif
		if(!live.syncing())
			live_views.update(live.stream(), engine, *research_view, now, journal.dropped(), live.dropped(),
			    controls_changed);

		if(now >= next_snapshot)
		{
			next_snapshot = now + 500;
			int target = -1;
			{std::lock_guard lock(snapshot_mutex); for(size_t i = 0; i < snapshots.size(); ++i)
				 if(static_cast<int>(i) != published_snapshot && !snapshots[i].readers)
				 {
					 target = static_cast<int>(i); break;
				 }
			}
			// Both buffers may be pinned by slow readers; keep the last complete
			// snapshot and service BLE instead of allocating an unbounded copy.
			if(target >= 0)
			{
				auto &pending = snapshots[target];
				pending.sequence = live.stream().sequence();
				CborWriter w(pending.bytes); w.map(5); w.text("v"); w.number(1); w.text("boot_id"); w.number(boot_id);
				w.text("seq"); w.number(pending.sequence); w.text("reset"); w.boolean(true);
				const auto devices = engine.devices(); w.text("changes");
				w.array(1 + devices.size() + research_view->rows().size() + journal.size() + control_count);
				w.array(2); w.text("runtime");
				research::LiveViews::write_runtime(w, engine, *research_view, now, journal.dropped(), live.dropped());
				for(const auto &d:devices)
				{
					w.array(2); w.text("device"); ResearchModule::write_device(engine, w, d);
				}
				for(const auto &row:research_view->rows())
				{
					w.array(2); w.text("observation"); ResearchModule::write_observation(engine, w, row);
				}
				journal.visit([&](const research::MessageJournal::Entry &e){
					w.array(2); w.text("message"); research::MessageJournal::write_entry(w, e);
				});
				const size_t begin = control_count == control_results.size() ? control_next : 0;
				for(size_t i = 0; i < control_count; ++i)
				{
					w.array(2); w.text("control");
					write_control(w, control_results[(begin + i) % control_results.size()]);
				}
				if(w.good())
				{
					pending.size = w.result().size();
					std::lock_guard lock(snapshot_mutex); published_snapshot = target;
				}
				else ESP_LOGW("NaBTools", "Snapshot capacity exceeded");
			}
		}

		if(now >= next_log)
		{
			auto b = engine.backend(); auto c = engine.counters();
			ESP_LOGI("NaBTools", "scan=%s observed=%llu dropped=%lu devices=%u error=%d heap=%lu largest=%u",
			    name(b.state),
			    static_cast<unsigned long long>(c.observed), static_cast<unsigned long>(b.dropped),
			    static_cast<unsigned>(engine.devices().size()), b.error,
			    static_cast<unsigned long>(esp_get_free_heap_size()),
			    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
#if NABTOOLS_LCD7
			ESP_LOGI("NaBTools", "LCD frames=%lu pixel=%04x touch=%s touch_errors=%lu internal=%u largest=%u",
			    static_cast<unsigned long>(display_frame_count()), display_pixel(4, 4),
			    display_touch_available() ? "ready" : "unavailable", static_cast<unsigned long>(display_touch_errors()),
			    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
			    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
#endif
			next_log = now + 5000;
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

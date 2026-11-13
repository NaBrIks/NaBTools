// SPDX-License-Identifier: GPL-3.0-only
#include "display_ui.hpp"
#include "cbor_text.hpp"
#include "NaBTools/catalogs/sig/sig.hpp"
#include "lvgl.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace research_lcd7
{
	namespace
	{
		using namespace NaBTools::runtime;
		using research::Command;
		enum class Page {air, devices, messages};
		struct State {
			BtEngine *engine{}; ResearchModule *research{}; research::MessageJournal *journal{};
			void *context{}; Submit submit{};
			lv_obj_t *header{}, *table{}, *details{}, *options{}, *run{}, *remove{}, *scan{}, *sort{}, *footer{},
			    *result{};
			Page page{Page::air}; bool rssi_sort{}, chosen{}; Address address{}; uint8_t sid{};
			DeviceId device{}; uint64_t ordinal{}, now{}, pending{}, sequence{};
			std::vector<const ObservationRow *> rows; std::vector<DeviceInfo> devices;
			std::vector<bool> recognized; std::array<lv_obj_t *, 3> tabs{}; std::string network;
			std::vector<const research::MessageJournal::Entry *> entries;
			std::vector<HandlerMatch> handlers; std::vector<OperationInfo> operations;
		} s;
		constexpr uint32_t ink = 0x28363e, cream = 0xf5f2e9, muted = 0x6f756e, line = 0xd9d1bd;
		void text(lv_obj_t *object, const std::string &value)
		{
			if(std::strcmp(lv_label_get_text(object), value.c_str()))
				lv_label_set_text(object, value.c_str());
		}

		void enabled(lv_obj_t *object, bool value)
		{
			if(value)
				lv_obj_remove_state(object, LV_STATE_DISABLED);
			else
				lv_obj_add_state(object, LV_STATE_DISABLED);
		}

		lv_obj_t * label(lv_obj_t *parent, int x, int y, int width, const char *value)
		{
			auto *object = lv_label_create(parent); lv_obj_set_pos(object, x, y); lv_obj_set_width(object, width);
			lv_label_set_text(object, value); lv_obj_set_style_text_color(object, lv_color_hex(ink), 0); return object;
		}

		lv_obj_t * button(lv_obj_t *parent, int x, int y, int width, const char *value, lv_event_cb_t cb,
		    void *context = nullptr)
		{
			auto *object = lv_button_create(parent); lv_obj_remove_style_all(object);
			lv_obj_set_pos(object, x, y); lv_obj_set_size(object, width, 28);
			lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
			lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);
			lv_obj_set_style_radius(object, 2, 0); lv_obj_set_style_shadow_width(object, 0, 0);
			lv_obj_set_style_pad_all(object, 2, 0); lv_obj_set_style_bg_color(object, lv_color_hex(0xe8e2d4), 0);
			auto *title = label(object, 0, 0, width - 6, value); lv_obj_center(title);
			lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
			lv_obj_add_event_cb(object, cb, LV_EVENT_CLICKED, context); return object;
		}

		void issue(Command command)
		{
			// Stay away from ordinary browser Date.now() tickets while keeping the
			// exported control ID within JavaScript's exact integer range.
			command.request_id = (uint64_t{1} << 52) | ++s.sequence;
			if(s.submit && s.submit(s.context, command))
			{
				s.pending = command.request_id; text(s.result, "Queued; waiting for engine");
			}
			else text(s.result, "Command queue full");
		}

		void cell(unsigned row, unsigned column, const std::string &value)
		{
			if(std::strcmp(lv_table_get_cell_value(s.table, row, column), value.c_str()))
				lv_table_set_cell_value(s.table, row, column, value.c_str());

			lv_table_set_cell_ctrl(s.table, row, column, LV_TABLE_CELL_CTRL_TEXT_CROP);
		}

		void headers(std::initializer_list<const char *> names, std::initializer_list<int> widths, size_t count)
		{
			lv_table_set_column_count(s.table, names.size()); lv_table_set_row_count(s.table, count + 1);
			unsigned i = 0; for(auto *name:names)
				cell(0, i++, name);
			i = 0; for(auto width:widths)
				lv_table_set_column_width(s.table, i++, width);
		}

		std::string age(uint64_t when)
		{
			char value[32];
			std::snprintf(value, sizeof(value), "%.1f", double(s.now >= when ? s.now - when : 0) / 1000); return value;
		}

		std::string device_id(DeviceId id)
		{
			return std::to_string(id.slot) + "." + std::to_string(id.generation);
		}

		void options(const std::string &value)
		{
			const auto list = value.empty() ? std::string("No actions") : value;
			if(std::strcmp(lv_dropdown_get_options(s.options), list.c_str()))
				lv_dropdown_set_options(s.options, list.c_str());

			enabled(s.options, !value.empty());
		}

		void refresh_details()
		{
			enabled(s.run, false); enabled(s.remove, false); s.handlers.clear(); s.operations.clear();
			text(lv_obj_get_child(s.run, 0), s.page == Page::air ? "Add" : "Run");
			std::string detail;
			if(s.page == Page::air && s.chosen)
			{
				const auto *row = s.research->find(s.address, s.sid);
				if(!row){options(""); text(s.details, "Observation evicted. Select another row."); return;}

				detail = row->packet.address.text() + "\n" + std::string(row->local_name.data()) + "\n";
				if(row->manufacturer >= 0)
				{
					if(const auto *name = sig::company(row->manufacturer))
						detail += std::string(name) + "\n";
				}

				detail += row->packet.rssi ==
				    127 ? "RSSI unavailable (raw 127)" : "RSSI " + std::to_string(row->packet.rssi) + " dBm";
				detail += "; age " + age(row->packet.time_ms) + " s\nPackets " + std::to_string(row->packets) + "\n";
				detail += "AD: " + std::string(name(row->status)) + "; type " +
				    std::to_string(row->packet.address.type) + "; SID " + std::to_string(row->packet.sid) + "\n";
				if(row->packet.radio)
				{
					const auto &r = *row->packet.radio;
					detail += "PHY " + std::string(phy_name(r.primary_phy)) + " / " + phy_name(r.secondary_phy) + "\n";
					detail += "Tx power: " +
					    (r.tx_power_dbm() ? std::to_string(*r.tx_power_dbm()) + " dBm" : std::string("not reported")) +
					    "\n";
					detail += "Periodic: " +
					    (r.periodic_interval_ms() ? std::to_string(*r.periodic_interval_ms()) +
					    " ms" : std::string("none")) + "\n";
					detail += "Report: " + std::string(data_status_name(r.data_status)) + "; properties " +
					    std::to_string(r.event_properties) + "\n";
					if(r.directed())
						detail += "Directed to " + r.directed_address.text() + " (" +
						    address_type_name(r.directed_address.type) + ")\n";
				}

				s.handlers = s.engine->recognize(row->packet.view()); std::string list;
				for(const auto &h:s.handlers)
				{
					if(!list.empty())
						list += '\n';

					list += h.detection.label;
					detail += "\n" + std::string(h.detection.label) + ": " + h.detection.evidence + "\n";
				}
				options(list); enabled(s.run, !s.handlers.empty());
				if(row->status == AdStatus::complete)
				{
					AdCursor cursor(row->packet.view().payload);
					while(auto field = cursor.next())
					{
						const char *kind = sig::ad_type(field->type); char type[12];
						std::snprintf(type, sizeof(type), "0x%02X", field->type);
						detail += "\n" + std::string(kind ? kind : type) + " (" + std::to_string(field->value.size()) +
						    ")\n";
						if(field->type == 2 || field->type == 3 || field->type == 0x14 || field->type == 0x16)
						{
							const size_t end = field->type == 0x16 ? std::min<size_t>(field->value.size(),
							        2) : field->value.size();
							for(size_t i = 0; i + 1 < end; i += 2)
							{
								const auto uuid = uint16_t(field->value[i]) | (uint16_t(field->value[i + 1]) << 8);
								char code[12]; std::snprintf(code, sizeof(code), "0x%04X", uuid);
								detail += std::string(code);
								if(const auto *service = sig::service(uuid)){detail += " " + std::string(service);}

								detail += '\n';
							}
						}

						for(auto b:field->value)
						{
							char hex[4]; std::snprintf(hex, sizeof(hex), "%02X ", b); detail += hex;
						}
					}
				}

				detail += "\n\nRAW\n"; for(auto b:row->packet.view().payload)
				{
					char hex[4]; std::snprintf(hex, sizeof(hex), "%02X ", b); detail += hex;
				}
			}
			else if(s.page == Page::devices && s.device.valid())
			{
				const auto found = std::find_if(s.devices.begin(), s.devices.end(), [](const auto &d){
					return d.id == s.device;
				});
				if(found == s.devices.end())
				{
					options(""); text(s.details, "Instance removed. Select another row."); return;
				}

				detail = std::string(found->protocol) + "\n" + found->address.text() + "\nInstance " +
				    device_id(found->id) + "\n";
				auto interaction = s.engine->interaction(found->id);
				s.operations.assign(interaction.commands.begin(), interaction.commands.end()); std::string list;
				for(const auto &operation:s.operations)
				{
					if(!list.empty())
						list += '\n';

					list += operation.label;
				}
				options(list); const auto index = lv_dropdown_get_selected(s.options);
				enabled(s.run, index < s.operations.size() && s.operations[index].status == Status::ok);
				enabled(s.remove, true);
				if(index < s.operations.size())
				{
					const auto &op = s.operations[index];
					detail += "\n" + std::string(op.key) + ": " + name(op.status) + "\n" +
					    (op.reason ? op.reason : "") + "\n";
				}

				for(const auto &event:interaction.events)
					detail += "\n" + std::string(event.key) + ": " + name(event.status);
				const research::MessageJournal::Entry *latest = nullptr;
				s.journal->visit([&](const auto &entry){
					if(entry.info.device == found->id)
						latest = &entry;
				});
				if(latest) detail += "\n\n" + std::string(latest->schema.data()) + "\n" + cbor_text(latest->payload);
			}
			else if(s.page == Page::messages && s.ordinal)
			{
				options(""); const research::MessageJournal::Entry *found = nullptr;
				s.journal->visit([&](const auto &entry){
					if(entry.ordinal == s.ordinal)
						found = &entry;
				});
				if(found)
					detail = std::string(found->schema.data()) + "\nDevice " + device_id(found->info.device) +
					    "\nSeq " + std::to_string(found->info.sequence) + "; age " + age(found->info.time_ms) +
					    " s\n\n" + cbor_text(found->payload);
				else detail = "Message evicted from bounded journal.";
			}
			else
			{
				options("");
				detail =
				    "Select a row on the left.\n\nAir: named AD fields and raw bytes.\nDevices: module commands and latest CBOR.\nCBOR: received measurements and events.";
			}

			text(s.details, detail);
		}

		void table_selected(lv_event_t *)
		{
			uint32_t row, column; lv_table_get_selected_cell(s.table, &row, &column); if(!row)
				return;

			--row;
			if(s.page == Page::air && row < s.rows.size())
			{
				s.address = s.rows[row]->packet.address; s.sid = s.rows[row]->packet.sid; s.chosen = true;
			}
			else if(s.page == Page::devices && row < s.devices.size()) s.device = s.devices[row].id;
			else if(s.page == Page::messages && row < s.entries.size()) s.ordinal = s.entries[row]->ordinal;

			refresh_details();
		}

		void execute(lv_event_t *)
		{
			Command command; const auto index = lv_dropdown_get_selected(s.options);
			if(s.page == Page::air && s.chosen && index < s.handlers.size())
			{
				command.kind = Command::add; command.address = s.address; command.sid = s.sid;
				std::snprintf(command.handler.data(), command.handler.size(), "%s", s.handlers[index].key);
			}
			else if(s.page == Page::devices && index < s.operations.size() && s.operations[index].status == Status::ok)
			{
				command.kind = Command::invoke; command.device = s.device; command.arguments[0] = 0xa0;
				command.argument_size = 1;
				std::snprintf(command.operation.data(), command.operation.size(), "%s", s.operations[index].key);
			}
			else return;

			issue(command);
		}

	}
	void create(BtEngine &engine, ResearchModule &research, research::MessageJournal &journal, void *context,
	    Submit submit)
	{
		s = {}; s.engine = &engine; s.research = &research; s.journal = &journal; s.context = context;
		s.submit = submit;
		auto *root = lv_screen_active(); lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_bg_color(root, lv_color_hex(cream), 0);
		lv_obj_set_style_text_font(root, &lv_font_montserrat_14, 0);
		s.header = label(root, 8, 6, 784, "NaBTools | BLE starting");
		s.tabs[0] = button(root, 6, 28, 82, "Air", [](lv_event_t *){
			s.page = Page::air; update(s.now, "");
		});
		s.tabs[1] = button(root, 94, 28, 88, "Devices", [](lv_event_t *){
			s.page = Page::devices; update(s.now, "");
		});
		s.tabs[2] = button(root, 188, 28, 82, "CBOR", [](lv_event_t *){
			s.page = Page::messages; update(s.now, "");
		});
		for(auto *tab:s.tabs)
			lv_obj_set_style_bg_color(tab, lv_color_hex(0xf6dfb4), LV_STATE_CHECKED);
		s.scan = button(root, 282, 28, 102, "Pause scan", [](lv_event_t *){
			Command c; c.kind = Command::scan; c.enabled = s.engine->backend().state != ScanState::scanning; issue(c);
		});
		s.sort = button(root, 390, 28, 114, "Sort RSSI", [](lv_event_t *){
			s.rssi_sort = !s.rssi_sort;
		});
		s.table = lv_table_create(root); lv_obj_set_pos(s.table, 6, 64); lv_obj_set_size(s.table, 504, 383);
		lv_obj_set_style_pad_all(s.table, 0, 0); lv_obj_set_style_radius(s.table, 0, 0);
		lv_obj_set_style_border_color(s.table, lv_color_hex(line), 0);
		lv_obj_set_style_pad_ver(s.table, 3, LV_PART_ITEMS); lv_obj_set_style_pad_hor(s.table, 4, LV_PART_ITEMS);
		lv_obj_set_style_border_width(s.table, 1, LV_PART_ITEMS);
		lv_obj_set_style_border_color(s.table, lv_color_hex(line), LV_PART_ITEMS);
		lv_obj_set_style_bg_color(s.table, lv_color_hex(0xe3d4b0),
		    uint32_t(LV_PART_ITEMS) | uint32_t(LV_STATE_PRESSED));
		lv_obj_add_event_cb(s.table, table_selected, LV_EVENT_VALUE_CHANGED, nullptr);
		lv_obj_add_flag(s.table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
		lv_obj_add_event_cb(s.table, [](lv_event_t *event){
			auto *descriptor = lv_draw_task_get_label_dsc(lv_event_get_draw_task(event)); if(!descriptor) return;

			const auto row = descriptor->base.id1, column = descriptor->base.id2;
			const auto first_numeric = s.page == Page::air ? 3u : 2u;
			if(column >= first_numeric) descriptor->align = LV_TEXT_ALIGN_RIGHT;

			if(s.page == Page::air && row && row <= s.recognized.size() && column == 1 && s.recognized[row - 1])
				descriptor->color = lv_color_hex(0x16705b);
		}, LV_EVENT_DRAW_TASK_ADDED, nullptr);
		label(root, 520, 34, 270, "Selection / module actions");
		s.options = lv_dropdown_create(root); lv_obj_set_pos(s.options, 520, 64); lv_obj_set_size(s.options, 180, 28);
		lv_obj_set_style_pad_ver(s.options, 4, 0);
		lv_obj_add_event_cb(s.options, [](lv_event_t *){
			refresh_details();
		}, LV_EVENT_VALUE_CHANGED, nullptr);
		s.run = button(root, 706, 64, 84, "Run / Add", execute);
		s.remove = button(root, 520, 96, 110, "Remove", [](lv_event_t *){
			if(s.device.valid())
			{
				Command c; c.kind = Command::remove; c.device = s.device; issue(c);
			}
		});
		s.result = label(root, 636, 99, 152, "");
		auto *panel = lv_obj_create(root); lv_obj_set_pos(panel, 516, 132); lv_obj_set_size(panel, 278, 315);
		lv_obj_set_style_pad_all(panel, 5, 0); lv_obj_set_style_radius(panel, 0, 0);
		lv_obj_set_style_border_color(panel, lv_color_hex(line), 0);
		s.details = label(panel, 0, 0, 260, ""); lv_label_set_long_mode(s.details, LV_LABEL_LONG_WRAP);
		s.footer = label(root, 8, 455, 784, "No radio observations yet");
		lv_obj_set_style_text_color(s.footer, lv_color_hex(muted), 0);
		update(0, "");
	}

	void update(uint64_t now, const char *network)
	{
		if(network && *network) s.network = network;

		enabled(s.sort, s.page == Page::air);
		for(size_t i = 0; i < s.tabs.size(); ++i)
		{
			if(i == static_cast<size_t>(s.page))
				lv_obj_add_state(s.tabs[i], LV_STATE_CHECKED);
			else
				lv_obj_remove_state(s.tabs[i], LV_STATE_CHECKED);
		}
		s.now = now; const auto backend = s.engine->backend(); const auto counters = s.engine->counters();
		char status[180];
		std::snprintf(status, sizeof(status), "NaBTools | BLE %s | RX %llu | drop %lu | devices %u",
		    name(backend.state), static_cast<unsigned long long>(counters.observed),
		    static_cast<unsigned long>(backend.dropped), unsigned(s.engine->devices().size()));
		text(s.header, status);
		text(lv_obj_get_child(s.scan, 0), backend.state == ScanState::scanning ? "Pause scan" : "Start scan");
		if(s.page == Page::air)
		{
			s.rows.clear(); s.recognized.clear(); for(const auto &row:s.research->rows())
				s.rows.push_back(&row);
			if(s.rssi_sort)
				std::stable_sort(s.rows.begin(), s.rows.end(), [](auto *a, auto *b){
					return (a->packet.rssi == 127 ? -129 : int(a->packet.rssi)) >
					       (b->packet.rssi == 127 ? -129 : int(b->packet.rssi));
				});

			headers({"Company", "Device", "Address", "dBm", "Packets", "Age s"}, {88, 96, 145, 40, 65, 66},
			    s.rows.size());
			unsigned r = 1; for(const auto *row:s.rows)
			{
				std::string company = "--"; if(row->manufacturer >= 0)
				{
					if(const auto *name = sig::company(row->manufacturer))
						company = name;
					else
					{
						char id[12]; std::snprintf(id, sizeof(id), "0x%04X", row->manufacturer); company = id;
					}
				}

				auto matches = s.engine->recognize(row->packet.view()); const char *title = row->local_name.data();
				if(!*title) title = matches.empty() ? "--" : matches.front().detection.label;

				s.recognized.push_back(!matches.empty());
				cell(r, 0, company); cell(r, 1, title); cell(r, 2, row->packet.address.text());
				cell(r, 3, row->packet.rssi == 127 ? "--" : std::to_string(row->packet.rssi));
				cell(r, 4, std::to_string(row->packets)); cell(r, 5, age(row->packet.time_ms)); ++r;
			}
		}
		else if(s.page == Page::devices)
		{
			s.devices = s.engine->devices();
			headers({"Device", "Address", "ID", "Packets", "Age s"}, {164, 145, 64, 65, 62}, s.devices.size());
			unsigned r = 1; for(const auto &d:s.devices)
			{
				cell(r, 0, d.protocol); cell(r, 1, d.address.text()); cell(r, 2, device_id(d.id));
				cell(r, 3, std::to_string(d.packets)); cell(r, 4, age(d.last_seen_ms)); ++r;
			}
		}
		else
		{
			s.entries.clear(); s.journal->visit([](const auto &e){
				s.entries.push_back(&e);
			});
			headers({"Schema", "Device", "Seq", "Age s"}, {260, 70, 80, 90}, s.entries.size());
			unsigned r = 1; for(const auto *entry:s.entries)
			{
				cell(r, 0, entry->schema.data()); cell(r, 1, device_id(entry->info.device));
				cell(r, 2, std::to_string(entry->info.sequence)); cell(r, 3, age(entry->info.time_ms)); ++r;
			}
		}

		refresh_details();
		std::snprintf(status, sizeof(status), "%s | SIG %s | AD malformed %llu / fragments %llu | journal drop %llu",
		    s.network.empty() ? "IP offline" : s.network.c_str(), sig::profile(),
		    static_cast<unsigned long long>(counters.malformed), static_cast<unsigned long long>(counters.fragments),
		    static_cast<unsigned long long>(s.journal->dropped()));
		text(s.footer, status);
	}

	void control_result(uint64_t request, Status result)
	{
		if(request == s.pending)
		{
			text(s.result, std::string("Engine: ") + name(result)); s.pending = 0;
		}
	}

}

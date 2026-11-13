// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "gatt_client.hpp"
#include "esp_gattc_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <cstring>

namespace NaBTools::runtime
{
	namespace
	{
		uint64_t now_ms()
		{
			return static_cast<uint64_t>(esp_timer_get_time() / 1000);
		}

		Uuid uuid(const esp_bt_uuid_t &source)
		{
			if(source.len == ESP_UUID_LEN_16) return Uuid::from16(source.uuid.uuid16);

			if(source.len == ESP_UUID_LEN_32) return Uuid::from32(source.uuid.uuid32);

			Uuid out; if(source.len == ESP_UUID_LEN_128)
				std::copy_n(source.uuid.uuid128, 16, out.bytes.begin());

			return out;
		}

		esp_bt_uuid_t sdk_uuid(Uuid value)
		{
			esp_bt_uuid_t out{}; uint32_t short_value = 0; for(unsigned i = 0; i < 4; ++i)
				short_value |= uint32_t(value.bytes[12 + i]) << (8 * i);
			if(value == Uuid::from32(short_value))
			{
				if(short_value <= 65535)
				{
					out.len = ESP_UUID_LEN_16; out.uuid.uuid16 = static_cast<uint16_t>(short_value);
				}
				else {out.len = ESP_UUID_LEN_32; out.uuid.uuid32 = short_value;}
			}
			else
			{
				out.len = ESP_UUID_LEN_128; std::copy(value.bytes.begin(), value.bytes.end(), out.uuid.uuid128);
			}

			return out;
		}

	}
	struct EspIdfGattClient::Impl {
		static constexpr size_t max_links = 4, raw_capacity = 32, out_capacity = 32;
		enum class Op {none, waiting, opening, search, enumerating, registering, subscribing, reading, writing,
			           closing};
		struct Link {
			bool used{}, cancelled{}; uint32_t generation{}, epoch{}; DeviceId owner; Address address;
			uint16_t conn{65535}; Op op{Op::none}; uint64_t request{}, deadline{};
			uint16_t value_handle{}, cccd{}; bool indications{};
			struct Service {uint16_t start{}, end{};}; std::array<Service, 16> services{};
			size_t service_count{}, service_index{};
			uint16_t char_count{}, char_index{}, desc_count{}, desc_index{}, current_char{};
			bool counts_ready{}, descriptors{};
			std::array<uint16_t, 8> subscriptions{}; size_t subscription_count{};
		};
		struct Raw {
			esp_gattc_cb_event_t kind{}; esp_gatt_if_t interface{ESP_GATT_IF_NONE};
			uint16_t conn{65535}, handle{}, end{}, size{}, properties{}; uint32_t epoch{};
			int status{}; Address address; Uuid uuid; uint64_t time{};
			std::array<uint8_t, GattEvent::capacity> data{};
		};
		static Impl *active; static portMUX_TYPE lock;
		std::array<Link, max_links> links{};
		std::array<uint32_t, 16> rx_epochs{}; uint32_t rx_serial{};
		StaticQueue_t raw_control{}; alignas(4) uint8_t raw_storage[raw_capacity * sizeof(Raw)]{};
		QueueHandle_t raw_queue{};
		std::deque<GattEvent> output;
		std::atomic<uint32_t> dropped{}; std::atomic<bool> lost{};
		esp_gatt_if_t interface{ESP_GATT_IF_NONE}; bool registered{}; int registration_error{};
		GattSessionId notify_wait;
		GattSessionId id(const Link &link)const
		{
			return {static_cast<uint8_t>(&link - links.data()), link.generation};
		}

		Link * get(GattSessionId session)
		{
			if(session.slot >= links.size())
				return nullptr;

			auto &l = links[session.slot]; return l.used && l.generation == session.generation ? &l : nullptr;
		}

		Link * connection(const Raw &raw)
		{
			for(auto &l:links)
				if(l.used && l.conn == raw.conn && l.epoch == raw.epoch)
					return &l;

			return nullptr;
		}

		GattEvent event(const Link &l, GattEventKind kind, int error = 0, uint64_t time = 0)const
		{
			GattEvent e; e.kind = kind; e.owner = l.owner; e.session = id(l); e.request_id = l.request;
			e.time_ms = time ? time : now_ms();
			e.error = error; e.status = error ? Status::backend_error : Status::ok; return e;
		}

		void push(GattEvent e)
		{
			if(output.size() >= out_capacity)
			{
				++dropped; lost = true;
			}
			else
				output.push_back(std::move(e));
		}

		void unregister(Link &l)
		{
			for(size_t i = 0; i < l.subscription_count; ++i)
			{
				(void)esp_ble_gattc_unregister_for_notify(interface, l.address.bytes.data(), l.subscriptions[i]);
			}
			l.subscription_count = 0;
		}

		void free(Link &l)
		{
			unregister(l); l.used = false; l.op = Op::none; l.conn = 65535;
		}

		void close(Link &l)
		{
			if(l.op == Op::waiting){free(l); return;}

			l.op = Op::closing; l.deadline = 0;
			if(l.conn != 65535) (void)esp_ble_gattc_close(interface, l.conn);

			// An in-flight open has no public cancellation API in this IDF version.
			// Keep its slot quarantined and close immediately if OPEN arrives later.
		}

		void fail(Link &l, int error, Status status = Status::backend_error)
		{
			auto e = event(l, GattEventKind::error, error); e.status = status; push(e); close(l);
		}

		void open(Link &l)
		{
			l.op = Op::opening; l.deadline = now_ms() + 30000;
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
			const auto result = esp_ble_gattc_aux_open(interface, l.address.bytes.data(),
			        static_cast<esp_ble_addr_type_t>(l.address.type), true);
#else
			const auto result = esp_ble_gattc_open(interface, l.address.bytes.data(),
			        static_cast<esp_ble_addr_type_t>(l.address.type), true);
#endif
			if(result != ESP_OK){push(event(l, GattEventKind::error, result)); free(l);}
		}

		static void callback(esp_gattc_cb_event_t kind, esp_gatt_if_t gatt_if, esp_ble_gattc_cb_param_t *p)
		{
			portENTER_CRITICAL(&lock); auto *self = active;
			if(!self || !p){portEXIT_CRITICAL(&lock); return;}

			Raw r; r.kind = kind; r.interface = gatt_if; r.time = now_ms(); bool keep = true;
			switch(kind)
			{
			case ESP_GATTC_REG_EVT: r.status = p->reg.status; break;
			case ESP_GATTC_OPEN_EVT:
				r.conn = p->open.conn_id; r.status = p->open.status;
				std::copy_n(p->open.remote_bda, 6, r.address.bytes.begin());
				if(!r.status && r.conn < self->rx_epochs.size())
				{
					if(++self->rx_serial == 0){++self->rx_serial;}

					self->rx_epochs[r.conn] = self->rx_serial;
				}

				break;
			case ESP_GATTC_SEARCH_RES_EVT:
				r.conn = p->search_res.conn_id; r.handle = p->search_res.start_handle; r.end = p->search_res.end_handle;
				r.uuid = uuid(p->search_res.srvc_id.uuid); break;
			case ESP_GATTC_SEARCH_CMPL_EVT: r.conn = p->search_cmpl.conn_id; r.status = p->search_cmpl.status; break;
			case ESP_GATTC_REG_FOR_NOTIFY_EVT: r.handle = p->reg_for_notify.handle; r.status = p->reg_for_notify.status;
				break;
			case ESP_GATTC_WRITE_CHAR_EVT: case ESP_GATTC_WRITE_DESCR_EVT:
				r.conn = p->write.conn_id; r.handle = p->write.handle; r.status = p->write.status; break;
			case ESP_GATTC_READ_CHAR_EVT:
				r.conn = p->read.conn_id; r.handle = p->read.handle; r.status = p->read.status;
				r.size = p->read.value_len;
				if(r.size <= r.data.size() && (!r.size || p->read.value))
					std::copy_n(p->read.value, r.size, r.data.begin());
				else r.status = ESP_ERR_INVALID_SIZE;

				break;
			case ESP_GATTC_NOTIFY_EVT:
				r.conn = p->notify.conn_id; r.handle = p->notify.handle; r.size = p->notify.value_len;
				if(r.size <= r.data.size() && (!r.size || p->notify.value))
					std::copy_n(p->notify.value, r.size, r.data.begin());
				else r.status = ESP_ERR_INVALID_SIZE;

				break;
			case ESP_GATTC_DISCONNECT_EVT: r.conn = p->disconnect.conn_id; r.status = p->disconnect.reason; break;
			case ESP_GATTC_CLOSE_EVT: r.conn = p->close.conn_id; r.status = p->close.reason; break;
			default: keep = false; break;
			}
			if(r.conn < self->rx_epochs.size()) r.epoch = self->rx_epochs[r.conn];

			if(keep && xQueueSend(self->raw_queue, &r, 0) != pdTRUE){++self->dropped; self->lost = true;}

			portEXIT_CRITICAL(&lock);
		}

		void consume(const Raw &r)
		{
			if(r.kind == ESP_GATTC_REG_EVT)
			{
				if(!r.status)
				{
					interface = r.interface; registered = true;
				}
				else
					registration_error = r.status;

				return;
			}

			if(interface != ESP_GATT_IF_NONE && r.interface != interface && r.interface != ESP_GATT_IF_NONE) return;

			if(r.kind == ESP_GATTC_OPEN_EVT)
			{
				Link *pending = nullptr; for(auto &l:links)
					if(l.used && l.conn == 65535 && l.address.bytes == r.address.bytes){pending = &l; break;}

				if(!pending)
				{
					if(!r.status)
						(void)esp_ble_gattc_close(interface, r.conn);

					return;
				}

				auto &l = *pending;
				if(r.status)
				{
					if(!l.cancelled)
						push(event(l, GattEventKind::error, r.status, r.time));

					free(l); return;
				}

				if(r.conn >= rx_epochs.size())
				{
					fail(l, ESP_ERR_INVALID_SIZE); (void)esp_ble_gattc_close(interface, r.conn); return;
				}

				l.conn = r.conn; l.epoch = r.epoch;
				if(l.cancelled || l.op == Op::closing){close(l); return;}

				l.op = Op::none; l.deadline = 0; push(event(l, GattEventKind::connected, 0, r.time)); return;
			}

			if(r.kind == ESP_GATTC_REG_FOR_NOTIFY_EVT)
			{
				auto *l = get(notify_wait); notify_wait = {}; if(!l || l->cancelled || l->op != Op::registering) return;

				if(r.handle != l->value_handle){fail(*l, ESP_ERR_INVALID_STATE); return;}

				if(r.status){fail(*l, r.status); return;}

				if(l->subscription_count == l->subscriptions.size()){fail(*l, ESP_ERR_NO_MEM, Status::full); return;}

				l->subscriptions[l->subscription_count++] = l->value_handle;
				uint8_t cccd[]{static_cast<uint8_t>(l->indications ? 2 : 1), 0};
				l->op = Op::subscribing;
				const auto result = esp_ble_gattc_write_char_descr(interface, l->conn, l->cccd, 2, cccd,
				        ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
				if(result != ESP_OK){fail(*l, result);}

				return;
			}

			auto *link = connection(r); if(!link)
				return;

			auto &l = *link;
			if(r.kind == ESP_GATTC_DISCONNECT_EVT || r.kind == ESP_GATTC_CLOSE_EVT)
			{
				if(!l.cancelled)
				{
					auto e = event(l, GattEventKind::disconnected, r.status, r.time); if(l.op == Op::closing)
						e.status = Status::ok;

					push(e);
				}

				free(l); return;
			}

			if(l.cancelled || l.op == Op::closing) return;

			switch(r.kind)
			{
			case ESP_GATTC_SEARCH_RES_EVT:
				if(l.op != Op::search) return;

				if(l.service_count == l.services.size()){fail(l, ESP_ERR_NO_MEM, Status::full); return;}

				l.services[l.service_count++] = {r.handle, r.end}; {
					auto e = event(l, GattEventKind::service, 0, r.time); e.handle = r.handle; e.end_handle = r.end;
					e.uuid = r.uuid; push(e);
				} break;
			case ESP_GATTC_SEARCH_CMPL_EVT:
				if(l.op != Op::search) return;

				if(r.status)
				{
					fail(l, r.status); return;
				}

				l.op = Op::enumerating; l.service_index = 0; l.counts_ready = false; break;
			case ESP_GATTC_NOTIFY_EVT: {
				if(r.status){fail(l, r.status); return;}

				auto e = event(l, GattEventKind::notification, 0, r.time); e.request_id = 0; e.handle = r.handle;
				e.size = r.size; e.data = r.data; push(e); break;
			}
			case ESP_GATTC_READ_CHAR_EVT:
				if(l.op != Op::reading || r.handle != l.value_handle)
				{
					return;
				}

				{
					auto e = event(l, GattEventKind::read, r.status, r.time); e.handle = r.handle;
					if(!r.status)
					{
						e.size = r.size; e.data = r.data;
					}

					l.op = Op::none; l.deadline = 0; push(e);
				} break;
			case ESP_GATTC_WRITE_DESCR_EVT:
				if(l.op != Op::subscribing || r.handle != l.cccd)
				{
					return;
				}

				{
					auto e = event(l, GattEventKind::subscribed, r.status, r.time); e.handle = l.value_handle;
					l.op = Op::none; l.deadline = 0; push(e);
				} break;
			case ESP_GATTC_WRITE_CHAR_EVT:
				if(l.op != Op::writing || r.handle != l.value_handle)
				{
					return;
				}

				{
					auto e = event(l, GattEventKind::write, r.status, r.time); e.handle = r.handle; l.op = Op::none;
					l.deadline = 0; push(e);
				} break;
			default: break;
			}
		}

		void enumerate(Link &l)
		{
			if(l.service_index == l.service_count)
			{
				l.op = Op::none; l.deadline = 0; push(event(l, GattEventKind::discovery_complete)); return;
			}

			const auto service = l.services[l.service_index];
			if(!l.counts_ready)
			{
				l.char_count = 0;
				const auto result = esp_ble_gattc_get_attr_count(interface, l.conn, ESP_GATT_DB_CHARACTERISTIC,
				        service.start, service.end, 0, &l.char_count);
				if(result != ESP_GATT_OK)
				{
					fail(l, result); return;
				}

				l.char_index = 0; l.descriptors = false; l.counts_ready = true;
			}

			if(l.descriptors)
			{
				if(l.desc_index == l.desc_count){l.descriptors = false; ++l.char_index; return;}

				esp_gattc_descr_elem_t descriptor{}; uint16_t count = 1;
				const auto result = esp_ble_gattc_get_all_descr(interface, l.conn, l.current_char, &descriptor, &count,
				        l.desc_index++);
				// IDF 5.4 returns the total cache count, not the number copied into
				// this capacity-one page. A successful nonempty page contains one.
				if(result != ESP_GATT_OK || count < 1){fail(l, result ? result : ESP_ERR_INVALID_SIZE); return;}

				auto e = event(l, GattEventKind::descriptor); e.handle = descriptor.handle;
				e.parent_handle = l.current_char; e.uuid = uuid(descriptor.uuid); push(e); return;
			}

			if(l.char_index == l.char_count){++l.service_index; l.counts_ready = false; return;}

			esp_gattc_char_elem_t characteristic{}; uint16_t count = 1;
			const auto result = esp_ble_gattc_get_all_char(interface, l.conn, service.start, service.end,
			        &characteristic, &count, l.char_index);
			if(result != ESP_GATT_OK || count < 1){fail(l, result ? result : ESP_ERR_INVALID_SIZE); return;}

			l.current_char = characteristic.char_handle; l.desc_count = 0; l.desc_index = 0; l.descriptors = true;
			const auto descriptors = esp_ble_gattc_get_attr_count(interface, l.conn, ESP_GATT_DB_DESCRIPTOR, 0, 0,
			        l.current_char, &l.desc_count);
			if(descriptors != ESP_GATT_OK){fail(l, descriptors); return;}

			auto e = event(l, GattEventKind::characteristic); e.handle = l.current_char;
			e.parent_handle = service.start;
			e.properties = characteristic.properties; e.uuid = uuid(characteristic.uuid); push(e);
		}

	};
	EspIdfGattClient::Impl *EspIdfGattClient::Impl::active{};
	portMUX_TYPE EspIdfGattClient::Impl::lock = portMUX_INITIALIZER_UNLOCKED;
	EspIdfGattClient::EspIdfGattClient()
	  : impl_(std::make_unique<Impl>())
	{
	}

	EspIdfGattClient::~EspIdfGattClient()
	{
		auto &s = *impl_; for(auto &l:s.links)
			if(l.used && l.conn != 65535) (void)esp_ble_gattc_close(s.interface, l.conn);

		portENTER_CRITICAL(&Impl::lock); if(Impl::active == &s)
			Impl::active = nullptr;

		portEXIT_CRITICAL(&Impl::lock);
		if(s.registered) (void)esp_ble_gattc_app_unregister(s.interface);
	}

	Status EspIdfGattClient::start()
	{
		auto &s = *impl_;
		s.raw_queue = xQueueCreateStatic(Impl::raw_capacity, sizeof(Impl::Raw), s.raw_storage, &s.raw_control);
		if(!s.raw_queue) return Status::full;

		portENTER_CRITICAL(&Impl::lock); const bool busy = Impl::active && Impl::active != &s; if(!busy)
			Impl::active = &s;

		portEXIT_CRITICAL(&Impl::lock);
		if(busy) return Status::busy;

		if(esp_ble_gattc_register_callback(Impl::callback) != ESP_OK || esp_ble_gattc_app_register(0x79) != ESP_OK)
			return Status::backend_error;

		return Status::ok;
	}

	GattStats EspIdfGattClient::stats()const
	{
		const auto &s = *impl_; uint32_t count = 0; for(const auto &l:s.links)
			if(l.used && l.conn != 65535 && !l.cancelled) ++count;

		return {s.registered, count, s.dropped.load()};
	}

	bool EspIdfGattClient::opening()const
	{
		for(const auto &l:impl_->links)
			if(l.used && (l.op == Impl::Op::waiting || l.op == Impl::Op::opening)) return true;

		return false;
	}

	void EspIdfGattClient::pump(bool may_open)
	{
		auto &s = *impl_;
		if(s.lost.exchange(false))
		{
			s.output.clear(); for(auto &l:s.links)
				if(l.used && !l.cancelled) s.fail(l, ESP_ERR_NO_MEM, Status::full);
		}

		Impl::Raw raw; for(unsigned n = 0; n < 32 && xQueueReceive(s.raw_queue, &raw, 0) == pdTRUE; ++n)
			s.consume(raw);
		bool pending = false; for(const auto &l:s.links)
			if(l.used && l.op == Impl::Op::opening) pending = true;

		if(may_open && !pending) for(auto &l:s.links) if(l.used && l.op == Impl::Op::waiting){s.open(l); break;}

		for(auto &l:s.links)
			if(l.used && !l.cancelled)
			{
				if(l.deadline && now_ms() >= l.deadline){s.fail(l, ESP_ERR_TIMEOUT); continue;}

				for(unsigned n = 0; n < 4 && l.op == Impl::Op::enumerating && s.output.size() < 16; ++n)
					s.enumerate(l);
			}
	}

	Status EspIdfGattClient::connect(DeviceId owner, Address address, uint64_t request, GattSessionId &out)
	{
		out = {}; auto &s = *impl_; if(!s.registered)
			return Status::busy;

		if(!owner.valid() || !request || address.type > 3) return Status::invalid;

		for(const auto &l:s.links)
			if(l.used && l.address.bytes == address.bytes) return Status::busy;

		for(auto &l:s.links)
			if(!l.used)
			{
				const auto generation = l.generation + 1 ? l.generation + 1 : 1; l = Impl::Link{};
				l.generation = generation; l.used = true; l.owner = owner; l.address = address;
				l.request = request; l.op = Impl::Op::waiting; l.deadline = now_ms() + 30000;
				out = s.id(l); return Status::ok;
			}

		return Status::full;
	}

	Status EspIdfGattClient::disconnect(GattSessionId session, uint64_t request)
	{
		auto &s = *impl_; auto *l = s.get(session); if(!l) return Status::expired;

		if(l->op == Impl::Op::closing){return Status::busy;}

		if(l->op == Impl::Op::waiting)
		{
			l->request = request; s.push(s.event(*l, GattEventKind::disconnected)); s.free(*l); return Status::ok;
		}

		l->request = request; s.close(*l); return Status::ok;
	}

	Status EspIdfGattClient::discover(GattSessionId session, std::optional<Uuid> filter, uint64_t request)
	{
		auto &s = *impl_; auto *l = s.get(session); if(!l || l->cancelled)
			return Status::expired;

		if(l->conn == 65535 || l->op != Impl::Op::none) return Status::busy;

		l->request = request; l->service_count = 0; l->op = Impl::Op::search; l->deadline = now_ms() + 15000;
		esp_bt_uuid_t value{}; if(filter) value = sdk_uuid(*filter);

		const auto result = esp_ble_gattc_search_service(s.interface, l->conn, filter ? &value : nullptr);
		if(result != ESP_OK)
		{
			l->op = Impl::Op::none; l->deadline = 0; return Status::backend_error;
		}

		return Status::ok;
	}

	Status EspIdfGattClient::subscribe(GattSessionId session, uint16_t value, uint16_t cccd, bool indications,
	    uint64_t request)
	{
		auto &s = *impl_; auto *l = s.get(session); if(!l || l->cancelled) return Status::expired;

		if(l->conn == 65535 || l->op != Impl::Op::none || s.notify_wait.valid()) return Status::busy;

		if(!value || !cccd) return Status::invalid;

		if(l->subscription_count == l->subscriptions.size()) return Status::full;

		l->request = request; l->value_handle = value; l->cccd = cccd; l->indications = indications;
		l->op = Impl::Op::registering; l->deadline = now_ms() + 15000;
		s.notify_wait = session;
		const auto result = esp_ble_gattc_register_for_notify(s.interface, l->address.bytes.data(), value);
		if(result != ESP_OK)
		{
			s.notify_wait = {}; l->op = Impl::Op::none; l->deadline = 0; return Status::backend_error;
		}

		return Status::ok;
	}

	Status EspIdfGattClient::read(GattSessionId session, uint16_t handle, uint64_t request)
	{
		auto &s = *impl_; auto *l = s.get(session); if(!l || l->cancelled) return Status::expired;

		if(l->conn == 65535 || l->op != Impl::Op::none){return Status::busy;}

		if(!handle) return Status::invalid;

		l->request = request; l->value_handle = handle; l->op = Impl::Op::reading; l->deadline = now_ms() + 15000;
		if(esp_ble_gattc_read_char(s.interface, l->conn, handle, ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
		{
			l->op = Impl::Op::none; l->deadline = 0; return Status::backend_error;
		}

		return Status::ok;
	}

	Status EspIdfGattClient::write(GattSessionId session, uint16_t handle, Bytes bytes, bool response, uint64_t request)
	{
		auto &s = *impl_; auto *l = s.get(session); if(!l || l->cancelled) return Status::expired;

		if(l->conn == 65535 || l->op != Impl::Op::none){return Status::busy;}

		if(!handle || bytes.size() > GattEvent::capacity) return Status::invalid;

		l->request = request; l->value_handle = handle; l->op = Impl::Op::writing; l->deadline = now_ms() + 15000;
		const auto result = esp_ble_gattc_write_char(s.interface, l->conn, handle, bytes.size(),
		        const_cast<uint8_t *>(bytes.data()), response ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
		        ESP_GATT_AUTH_REQ_NONE);
		if(result != ESP_OK)
		{
			l->op = Impl::Op::none; l->deadline = 0; return Status::backend_error;
		}

		return Status::ok;
	}

	void EspIdfGattClient::cancel(DeviceId owner)
	{
		auto &s = *impl_; for(auto &l:s.links)
			if(l.used && l.owner == owner)
			{
				l.cancelled = true; s.close(l);
			}
	}

	bool EspIdfGattClient::take(GattEvent &event)
	{
		auto &q = impl_->output; if(q.empty())
			return false;

		event = std::move(q.front()); q.pop_front(); return true;
	}

}

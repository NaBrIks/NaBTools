// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "live_transport.hpp"
#include "command.hpp"
#include <NaWServer/response.h>
#include <cstring>
#include "freertos/task.h"
using namespace NaBTools::runtime;
using namespace NaWServer;

void LiveTransport::Endpoint::handleRequest(Request *request, Response &response)
{
	// HTTPD has already negotiated the upgrade before this callback. Close an
	// excess upgraded session instead of sending an ordinary HTTP response.
	// https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-reference/protocols/esp_http_server.html#websocket-server
	// Native access belongs to the explicit ESP-IDF backend adapter. The
	// pointer is borrowed only while this handler and its WS callbacks run.
	auto *native = NaWServer::espIdfRequest(*request);
	if(!native){(void)response.empty(503); return;}

	if(request->method() == HttpMethod::Get && count() >= 2)
	{
		httpd_sess_trigger_close(native->handle, httpd_req_to_sockfd(native)); return;
	}

	request_ = native; WebSocketEndpoint::handleRequest(request, response); request_ = nullptr;
}

bool LiveTransport::enqueue(void *context, Bytes bytes)
{
	auto &self = *static_cast<LiveTransport *>(context);
	if(!self.clients())return true;

	// Zero wait on the BT task. The worker owns a bounded copy and performs
	// potentially slow HTTPD work; no WebSocketClient pointer escapes a callback.
	auto &frame = self.staging_; frame.size = static_cast<uint16_t>(bytes.size());
	std::copy(bytes.begin(), bytes.end(), frame.bytes.begin());
	return xQueueSend(self.queue_, &frame, 0) == pdTRUE;
}

void LiveTransport::worker(void *context)
{
	auto &self = *static_cast<LiveTransport *>(context); Frame frame;
	for(;;)
		if(xQueueReceive(self.queue_, &frame, portMAX_DELAY) == pdTRUE)
		{
			const auto sent = self.endpoint_.send_binary(frame.bytes.data(), frame.size);
			self.rejected_ += sent.rejected;
		}
}

bool LiveTransport::send_snapshot(void *context, Bytes snapshot)
{
	auto &self = *static_cast<LiveTransport *>(context);
	// A snapshot is pinned by the application for this synchronous HTTPD
	// callback. Sending here avoids retaining a borrowed client pointer
	// or enqueuing a complete snapshot into the four-job per-peer send queue.
	// https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-reference/protocols/esp_http_server.html#websocket-server
	// A reset packet is the same change envelope as a small live update, up to
	// 32 KiB. WebSocket/TCP handle transport segmentation; no chunk protocol.
	httpd_ws_frame_t frame{}; frame.type = HTTPD_WS_TYPE_BINARY; frame.final = true;
	frame.payload = const_cast<uint8_t *>(snapshot.data()); frame.len = snapshot.size();
	return httpd_ws_send_frame(self.endpoint_.callback_request(), &frame) == ESP_OK;
}

bool LiveTransport::begin(uint64_t boot, Submit submit, SnapshotSource snapshot)
{
	boot_ = boot; submit_ = submit; snapshot_ = snapshot;
	queue_ = xQueueCreateStatic(queue_capacity, sizeof(Frame), queue_storage_, &queue_control_);
	if(!queue_)return false;

	stream_.begin(boot, this, enqueue);
	endpoint_.onConnect([this](WebSocketClient *client){
		std::array<uint8_t, 128> bytes; CborWriter w(bytes); w.map(4);
		w.text("v"); w.number(1); w.text("type"); w.text("hello");
		w.text("boot_id"); w.number(boot_); w.text("seq"); w.number(stream_.sequence());
		if(client->send_binary(w.result().data(), w.result().size()) != WebSocketSendResult::Queued)++rejected_;
	});
	endpoint_.onMessage([this](WebSocketClient *client, const uint8_t *data, size_t size, bool binary){
		const auto parsed = binary ? research::parse_command({data, size}) : std::nullopt;
		if(parsed && parsed->kind == research::Command::snapshot)
		{
			syncing_ = true; const auto sent = snapshot_(this, send_snapshot); syncing_ = false;
			if(!sent)
			{
				auto *r = endpoint_.callback_request(); httpd_sess_trigger_close(r->handle, httpd_req_to_sockfd(r));
			}

			return;
		}

		uint64_t request = 0; const auto status = binary ? submit_({data, size}, request) : Status::invalid;
		std::array<uint8_t, 160> bytes; CborWriter w(bytes); w.map(5);
		w.text("v"); w.number(1); w.text("type"); w.text("ack"); w.text("boot_id"); w.number(boot_);
		w.text("request_id"); w.number(request); w.text("status");
		w.text(status == Status::ok ? "queued" : name(status));
		if(client->send_binary(w.result().data(), w.result().size()) != WebSocketSendResult::Queued)++rejected_;
	});
	return xTaskCreate(worker, "nabtools_ws", 6144, this, 2, nullptr) == pdPASS;
}

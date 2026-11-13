// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "stream.hpp"
#include "NaBTools/core/status.hpp"
#include <NaWServer/websocket.h>
#include <NaWServer/backends/esp_idf_request.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class LiveTransport
{
	class Endpoint final : public NaWServer::WebSocketEndpoint
	{
		httpd_req_t *request_{};
	public:
		Endpoint()
		  : WebSocketEndpoint("/ws")
		{
		}

		void handleRequest(NaWServer::Request *, NaWServer::Response &)override;
		httpd_req_t * callback_request()const
		{
			return request_;
		}

	} endpoint_;
	struct Frame {uint16_t size{}; std::array<uint8_t, NaBTools::runtime::research::EventStream::frame_capacity> bytes;
	};
	static constexpr size_t queue_capacity = 4;
	StaticQueue_t queue_control_{};
	alignas(4) uint8_t queue_storage_[queue_capacity * sizeof(Frame)]{};
	QueueHandle_t queue_{}; uint64_t boot_{};
	Frame staging_;
	using Submit = NaBTools::runtime::Status (*)(NaBTools::runtime::Bytes, uint64_t &);
	Submit submit_{};
	using SnapshotSource = bool (*)(void *, bool (*)(void *, NaBTools::runtime::Bytes));
	SnapshotSource snapshot_{}; std::atomic<bool> syncing_{};
	std::atomic<uint64_t> rejected_{};
	NaBTools::runtime::research::EventStream stream_;
	static bool enqueue(void *, NaBTools::runtime::Bytes);
	static void worker(void *);
	static bool send_snapshot(void *, NaBTools::runtime::Bytes);
public:
	bool begin(uint64_t boot, Submit, SnapshotSource);
	NaWServer::HandlerBase * route()
	{
		return &endpoint_;
	}

	auto& stream()
	{
		return stream_;
	}

	uint64_t dropped()const
	{
		return rejected_.load() + stream_.dropped();
	}

	size_t clients()const
	{
		return endpoint_.count();
	}

	bool syncing()const
	{
		return syncing_.load();
	}

};

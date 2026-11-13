// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include "FreeRTOS.h"
inline QueueHandle_t xQueueCreateStatic(size_t, size_t, uint8_t *, StaticQueue_t *q)
{
	return q;
}

inline int xQueueSend(QueueHandle_t, const void *, int)
{
	return pdTRUE;
}

inline int xQueueReceive(QueueHandle_t, void *, int)
{
	return 0;
}

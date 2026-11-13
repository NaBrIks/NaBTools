// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include <cstddef>
#include <cstdint>
using portMUX_TYPE = int;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0, pdTRUE = 1;
inline void portENTER_CRITICAL(portMUX_TYPE *)
{
}

inline void portEXIT_CRITICAL(portMUX_TYPE *)
{
}

struct StaticQueue_t {};
using QueueHandle_t = void *;

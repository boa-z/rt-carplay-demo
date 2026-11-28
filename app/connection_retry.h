/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t deadline;
    unsigned failures;
    bool waiting;
} carplay_connection_retry_t;

void carplay_retry_schedule(carplay_connection_retry_t *retry, uint32_t now_ms, int error);
uint32_t carplay_retry_remaining(const carplay_connection_retry_t *retry, uint32_t now_ms);

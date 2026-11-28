/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "connection_retry.h"

void carplay_retry_schedule(carplay_connection_retry_t *retry, uint32_t now_ms, int error)
{
    uint32_t delay = 1000;
    if (error)
    {
        if (retry->failures < 5)
            ++retry->failures;
        delay = 5000U << (retry->failures - 1U);
        if (delay > 60000)
            delay = 60000;
    }
    else
        retry->failures = 0;
    retry->deadline = now_ms + delay;
    retry->waiting = true;
}

uint32_t carplay_retry_remaining(const carplay_connection_retry_t *retry, uint32_t now_ms)
{
    uint32_t remaining = retry->deadline - now_ms;
    return retry->waiting && remaining <= INT32_MAX ? remaining : 0;
}

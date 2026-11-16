/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_INTERNET_SHARE_LUBANLITE_H
#define CARPLAY_INTERNET_SHARE_LUBANLITE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Copied observations only; callers never access lwIP objects or wait for I/O.
 * Reachability means the configured WAN-bound HTTP 204 check passed, not a
 * guarantee that every Internet destination or an attached iPhone works. */
typedef struct
{
    bool available, device, link, addressed, route_ready, checking, checked, reachable, sharing;
    uint32_t age_ms;
    char address[16], mask[16], gateway[16], dns[16];
    char probe_url[160];
} carplay_internet_status_t;
#if defined(RTCARPLAY_INTERNET_SHARE)
void carplay_internet_status(carplay_internet_status_t *out);
bool carplay_internet_check(void);
#else
static inline void carplay_internet_status(carplay_internet_status_t *out)
{
    memset(out, 0, sizeof(*out));
}
static inline bool carplay_internet_check(void)
{
    return false;
}
#endif
#endif

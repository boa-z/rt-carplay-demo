/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
/* Ordinary-thread blocking operations; do not call from LVGL or callbacks.
 * Commands and shell calls share a nonblocking gate: contention returns
 * RTCP_ESTATE. Session close waits up to three seconds for the iAP2 owner;
 * timeout/self-join returns RTCP_ETIMEDOUT and preserves its stream. Pair
 * and iAP2 start are separate operations; use one application owner to
 * compose them. Detached/running/trust queries are observational snapshots. */
int carplay_usb_open(void);
bool carplay_usb_attached(void);
int carplay_usb_pair(void);
int carplay_usb_start_iap2(void);
bool carplay_usb_iap2_running(void);
/* Read-only USB network diagnostics in an ordinary worker thread. */
void carplay_usb_network_status(void);
int carplay_usb_close_session(void);

int carplay_usb_trust_cache_status(void);

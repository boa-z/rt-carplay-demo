/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Wireless iAP2 session on top of rtcp_bt_lubanlite.
 *
 * carplay_iap2 starts one Bluetooth RFCOMM server for the Apple iAP2 service
 * UUID and drives core/iap2/iap2_session over it: received bytes are fed to
 * the link layer, its writes go back over RFCOMM, and a 100 ms tick runs the
 * retransmit/invite timers.  The receiver must already be running
 * (carplay_start): the session retains its own copied SSID/passphrase,
 * AirPlay port, device id and Ed25519 public key from the application.
 *
 * RTCARPLAY_MFI supplies the board's shared authentication provider.
 */
#pragma once

#include <stdbool.h>

/* Starts the Bluetooth host (if needed), registers SDP/RFCOMM and serves one
 * phone at a time.  Safe to call from the MSH thread.  Returns RTCP_OK,
 * RTCP_ESTATE when the receiver is not running or the Bluetooth link is
 * owned by another service or a lifecycle call is already in progress,
 * or another negative RTCP error. Success schedules startup; readiness
 * and asynchronous failure are reported through copied status. */
int carplay_iap2_start(void);

/* Asks the link thread to stop and waits for the owner to destroy its
 * session. Returns RTCP_OK when it is down, RTCP_ESTATE
 * on lifecycle contention/self-join, or RTCP_ETIMEDOUT after 3 seconds.
 * A timeout leaves the worker and all its resources owned, not reusable. */
int carplay_iap2_stop(void);

/* Atomic, non-blocking stop request; no transport operation runs here.
 * The owner observes it after its bounded 500/100 ms accept/receive wait
 * or a pending platform startup call, then tears down the session. */
void carplay_iap2_request_stop(void);

bool carplay_iap2_is_running(void);

/* Session progress, mirrored for the shell status command.  All reset when
 * the RFCOMM link drops. */
bool carplay_iap2_identification_accepted(void);
bool carplay_iap2_authenticated(void);

typedef struct
{
    bool running, listening, connected, identified, authenticated;
    unsigned wifi_configs, availability, invites, reinvites, invite_acks;
    bool mfi_completed; /* retained through normal RFCOMM Wi-Fi handover */
    bool tunnel_connected, tunnel_identified, tunnel_authenticated, handoff_complete;
    int error; /* asynchronous startup/session failure; reset on start */
} carplay_iap2_status_t;
/* Copies atomic diagnostic fields without accessing live session pointers.
 * Fields can reflect adjacent owner steps; this is not a transactionally
 * coherent session-state snapshot. Ordinary-thread API, not an ISR API. */
void carplay_iap2_get_status(carplay_iap2_status_t *status);

#include <stddef.h>
#include <stdint.h>
#include "rtcp/media.h"
#include "rtcp/device_time.h"
#include "rtcp/navigation.h"

typedef struct
{
    rtcp_now_playing_cb_t on_now_playing;
    rtcp_route_guidance_cb_t on_route_guidance;
    rtcp_device_time_cb_t on_device_time;
    void *context;
} carplay_iap2_tunnel_callbacks_t;
/* Invoked exclusively by the RTSP owner; independent from the RFCOMM pump. */
bool carplay_iap2_tunnel_bytes(const uint8_t *, size_t, void *);
bool carplay_iap2_tunnel_tick(void *);
void carplay_iap2_tunnel_stop(void *);
bool carplay_iap2_request_handoff(const char *, void *);

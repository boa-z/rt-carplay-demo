/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_CONNECTION_MANAGER_H
#define CARPLAY_CONNECTION_MANAGER_H
#include <stdbool.h>
#include <stdint.h>
#include "rtcp_lubanlite.h"
#include "pairing_manager.h"
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "fusion_display.h"
#endif
#include "rtcp/events.h"

#if defined(RTCARPLAY_IAP2) && defined(LPKG_RT_CARPLAY_LUBANLITE_WIFI)
#define DEMO_HAS_WIRELESS 1
#else
#define DEMO_HAS_WIRELESS 0
#endif

typedef enum
{
    DEMO_BOOT,
    DEMO_WAIT,
    DEMO_PAIR,
    DEMO_NETWORK,
    DEMO_CONNECT,
    DEMO_LIVE,
    DEMO_CLOSING,
    DEMO_FAILED
} demo_stage_t;

typedef struct
{
    demo_stage_t stage;
    int error;
    char pin[16];
    bool usb;
    bool automatic, wireless_enabled, usb_ready;
    unsigned retry_seconds, reconnects;
    int usb_error, wireless_error;
    rtcp_receiver_state_t receiver;
    bool wireless, bt_ready, bt_connected, bt_identified, bt_authenticated;
    unsigned wifi_configs, availability, invites, reinvites, invite_acks;
    char ssid[33];
    rtcp_bt_pairing_status_t pairing;
    char ipv6[64];
    unsigned received, frames, drops, errors, hid_sent, hid_failed;
    int trust, storage_error;
    demo_stage_t previous;
    int last_error;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_display_mode_t display_requested;
    uint16_t display_width, display_height;
    bool display_reconfiguring;
#endif
} demo_status_t;

/* Application-owned connection worker. No LVGL dependency. Initialize from
 * the application owner before posting requests. Snapshots are caller-owned. */
int carplay_connection_manager_start(void);
bool carplay_connection_maintenance_acquire(void);
void carplay_connection_maintenance_release(void);
bool carplay_connection_status(demo_status_t *out);
/* Legacy manual diagnostic override; Launcher V2 uses automatic discovery. */
void carplay_connection_select_mode(bool wireless);
/* Idle-only setting. USB remains armed; false pauses wireless discovery.
 * Accepted changes persist in the application preferences when DFS is enabled. */
bool carplay_connection_set_wireless_enabled(bool enabled);
void carplay_connection_pairing_reply(uint32_t request, bool accept);
void carplay_connection_retry(void);
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
/* Asynchronous: geometry changes close/recreate the session on its owner.
 * LOCAL keeps the selected phone geometry until reboot. The selected mode is
 * persisted; LOCAL boots with the default fusion phone geometry. */
bool carplay_connection_set_display_mode(carplay_display_mode_t mode);
#endif
bool carplay_connection_files_open(void);
void carplay_connection_files_close(void);
void carplay_connection_files_refresh(void);
void carplay_connection_files_forget(const char *name);
bool carplay_connection_files_snapshot(unsigned *revision,
                                       carplay_pair_file_t files[CARPLAY_PAIR_FILES_MAX], unsigned *count,
                                       bool *more, int *result);
int carplay_connection_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y);
#endif

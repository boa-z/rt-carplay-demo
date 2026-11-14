/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_CONNECTION_MANAGER_H
#define CARPLAY_CONNECTION_MANAGER_H
#include <stdbool.h>
#include <stdint.h>
#include "rtcp_lubanlite.h"
#include "pairing_manager.h"
#include "rtcp/events.h"
#if defined(RTCARPLAY_AUDIO)
#include "audio_test_lubanlite.h"
#endif

#if defined(RTCARPLAY_IAP2) && defined(LPKG_RT_CARPLAY_LUBANLITE_WIFI)
#define DEMO_HAS_WIRELESS 1
#else
#define DEMO_HAS_WIRELESS 0
#endif

typedef enum {
    DEMO_BOOT, DEMO_WAIT, DEMO_PAIR, DEMO_NETWORK, DEMO_CONNECT,
    DEMO_LIVE, DEMO_CLOSING, DEMO_FAILED
} demo_stage_t;

typedef struct {
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
} demo_status_t;


/* Application-owned connection worker. No LVGL dependency. Initialize from
 * the application owner before posting requests. Snapshots are caller-owned. */
int carplay_connection_manager_start(void);
bool carplay_connection_status(demo_status_t *out);
/* Legacy manual diagnostic override; Launcher V2 uses automatic discovery. */
void carplay_connection_select_mode(bool wireless);
/* Idle-only setting. USB remains armed; false pauses wireless discovery.
 * Settings are in RAM and reset to automatic/both at reboot. */
bool carplay_connection_set_wireless_enabled(bool enabled);
void carplay_connection_pairing_reply(uint32_t request, bool accept);
void carplay_connection_retry(void);
bool carplay_connection_files_open(void);
void carplay_connection_files_close(void);
void carplay_connection_files_refresh(void);
void carplay_connection_files_forget(const char *name);
bool carplay_connection_files_snapshot(unsigned *revision,
    carplay_pair_file_t files[CARPLAY_PAIR_FILES_MAX], unsigned *count,
    bool *more, int *result);
int carplay_connection_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y);
#if defined(RTCARPLAY_AUDIO)
bool carplay_connection_audio_open(void);
void carplay_connection_audio_select(unsigned action);
void carplay_connection_audio_snapshot(bool *open, bool *ready, bool *exiting,
    audio_test_config_t *config, int *result, audio_test_status_t *status);
#endif
#endif

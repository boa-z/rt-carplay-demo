/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * CarPlay / AirPlay receiver application for RT-Thread + Luban-Lite.
 *
 * It composes the protocol library into a running receiver: Wi-Fi SoftAP
 * with the Apple vendor IE, DHCP, mDNS advertisement, the AirPlay RTSP
 * server (Pair-Setup/Pair-Verify/Screen/Event endpoints) and the touch ->
 * HID input bridge.  Board specifics stay behind the rtcp_lubanlite
 * helpers; the encoded Screen stream is handed to on_screen, which defaults
 * to the on-board MPP/DE video sink when RTCARPLAY_VIDEO is enabled.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rtcp/device_time.h"
#include "rtcp/events.h"
#include "rtcp/display.h"

#ifdef RT_USING_RTC
void carplay_app_on_device_time(const rtcp_device_time_t *update, void *context);
int carplay_app_clock_offset_minutes(void);
bool carplay_app_clock_is_automatic(void);
/** Report whether this session supplied a UTC sample, independently of RTC write success. */
bool carplay_app_phone_clock_received(void);
int carplay_app_set_clock_automatic(bool automatic);
int carplay_app_set_local_clock(int64_t local_seconds, int offset_minutes);
#endif

/* Screen sink.  configuration=true announces the H.264 SPS/PPS; otherwise
 * annex_b is one access unit prefixed with a start code.  Called from the
 * poll thread and must return quickly (copy the data, do not block). */
typedef void (*carplay_app_screen_cb_t)(bool configuration,
                                        const uint8_t *annex_b,
                                        size_t annex_b_len,
                                        uint64_t sender_time_ns,
                                        void *context);

typedef struct
{
    const char *name;                  /* Bonjour/MFi name, default "rt-carplay" */
    const char *model;                 /* default "ArtInChip" */
    const char *ssid;                  /* default "ArtInChip CarPlay" */
    const char *passphrase;            /* default "88888888"; "" = open network */
    uint8_t channel;                   /* default 36 (5 GHz, as the vendor image) */
    const char *pair_code;             /* NULL = "3939" (CarPlay's fixed AirPlay PIN) */
    uint16_t width_pixels;             /* required display width */
    uint16_t height_pixels;            /* required display height */
    uint16_t rotation_degrees;         /* 0, 90, 180 or 270 */
    const char *seed_path;             /* default "/data/rt-carplay.seed" */
    const char *store_path;            /* default "/data/rt-carplay-pairings.bin" */
    carplay_app_screen_cb_t on_screen; /* NULL = on-board sink if built in */
    void *screen_context;
    /* Wired CarPlay: skip the SoftAP/mDNS bring-up and serve AirPlay over
     * IPv6 on the phone's CDC-NCM link.  The address advertised in
     * CarPlayStartSession is read from the "u1" netif on demand. */
    bool wired;
    /* Optional OEM tile assets. Keep the label and PNGs valid until stop. */
    const rtcp_oem_icon_t *oem_icons;
    size_t oem_icon_count;
    const char *oem_icon_label;
} carplay_app_config_t;

/* Fills config with the defaults above; width/height are left for the
 * caller (the board knows its panel). */
void carplay_app_config_defaults(carplay_app_config_t *config);

/* Brings the receiver up and starts its poll thread.  Returns RTCP_OK or a
 * negative RTCP error. Call from ordinary thread context (main or MSH).
 * Concurrent/reentrant lifecycle calls return RTCP_ESTATE. Failed thread
 * creation/startup rolls back all initialized resources and permits retry. */
int carplay_app_start(const carplay_app_config_t *config);

/* Start with the built-in display geometry: SoftAP + mDNS (wireless) and the
 * IPv6 NCM link without them (wired).  The SDK application and the
 * carplay_start command use these. */
int carplay_app_autostart(void);
int carplay_app_autostart_wired(void);
/* Connection-owner-only session creation with an explicit Main Display size.
 * Stop iAP2 borrowers and the old Receiver before calling this. */
int carplay_app_autostart_display(bool wired, uint16_t width, uint16_t height);

/* Asks the poll thread to tear everything down and waits for its last state
 * access. Calling from that worker (including its callbacks) returns
 * RTCP_ESTATE: post a host stop request instead of joining oneself. */
int carplay_app_stop(void);

bool carplay_app_is_running(void);
int carplay_app_set_projection_enabled(bool enabled);
/* Owner-published protocol state, copied under the snapshot/queue guards.
 * May lag one diagnostic tick (100 ms); zeroed on failure/retirement. */
int carplay_app_session_state(rtcp_receiver_state_t *state);

/* Copied display identity. Safe across stop/restart; returns ESTATE during
 * lifecycle changes and clears the output on failure. */
typedef struct
{
    uint16_t width, height;
    char pairing_code[5];
} carplay_app_display_info_t;
int carplay_app_display_info(carplay_app_display_info_t *info);
/* Compatibility geometry query returns zero during lifecycle contention. */
void carplay_app_display_size(uint16_t *width, uint16_t *height);
/* Legacy lifecycle-owner-only borrowed PIN; use display_info across threads. */
const char *carplay_app_pairing_code(void);

/*
 * Snapshot of the live wireless configuration for the iAP2 session.  The
 * strings and scalar values are copied into caller-owned storage and stay
 * valid after stop/restart. Returns RTCP_ESTATE when not running, stopping
 * or a lifecycle change is retiring existing snapshots. Other queries and
 * input operations do not exclude snapshots.
 * ipv4 is four bytes in network order; public_key_hex is the Ed25519 public
 * key exactly as announced in the mDNS TXT record.  airplay_connected is a
 * snapshot; poll it to pace CarPlay re-invites.
 */
typedef struct
{
    char name[33];
    char model[33];
    char ssid[33];
    char passphrase[65];
    uint8_t channel;
    uint8_t security_type; /* 0 = open, 2 = WPA/WPA2 PSK */
    uint16_t airplay_port;
    uint8_t ipv4[4];
    uint8_t bssid[6];
    bool bssid_valid;
    char device_id[18];
    char public_key_hex[65];
    const char *source_version;
    bool mdns_ready;
    bool airplay_connected;
} carplay_app_iap2_info_t;

int carplay_app_iap2_info(carplay_app_iap2_info_t *info);

/*
 * Snapshot of the wired AirPlay endpoint for the iAP2 session.  Unlike the
 * wireless snapshot, no SoftAP exists: ipv6 is the NCM netif's link-local in
 * presentation form (e.g. "fe80::..." without a zone), freshly read at call
 * time which is why it must be called when CarPlayAvailability arrives, after
 * the phone has enumerated its NCM function and DAD completed.
 *
 * Returns RTCP_ESTATE when a wireless receiver is running (or none is),
 * RTCP_ENOENT when the wired receiver is up but the netif has no usable
 * address yet. Strings are copied; source_version is a static SDK constant.
 */
typedef struct
{
    char device_id[18];
    char public_key_hex[65];
    const char *source_version;
    uint16_t airplay_port;
    char ipv6[64];
    bool airplay_connected;
} carplay_app_wired_info_t;

int carplay_app_wired_info(carplay_app_wired_info_t *info);

/*
 * One owner for the single Bluetooth link: the echo bring-up (carplay_bt)
 * and the iAP2 session (carplay_iap2) register the same RFCOMM server, so
 * running both would split accepts and corrupt the stream.  Claim before
 * enabling the Bluetooth host; release on teardown.
 */
typedef enum
{
    CARPLAY_BT_OWNER_NONE = 0,
    CARPLAY_BT_OWNER_ECHO,
    CARPLAY_BT_OWNER_IAP2,
} carplay_bt_owner_t;

bool carplay_bt_claim(carplay_bt_owner_t owner);
void carplay_bt_release(carplay_bt_owner_t owner);
carplay_bt_owner_t carplay_bt_owner(void);

/*
 * Input injection in display pixels (phase: RTCP_TOUCH_DOWN/MOVE/UP).  Safe
 * from any thread: events are queued and delivered by the poll thread, which
 * is the only thread allowed to write the event channel. Returns
 * RTCP_ESTATE when start/stop or another public input request owns the gate.
 */
int carplay_app_touch(uint8_t phase, uint16_t pointer_id,
                      uint16_t x, uint16_t y);

/* Last AirPlay pairing-table write result, diagnostic snapshot. */
int carplay_app_pairing_storage_error(void);

void carplay_app_debug_counters(unsigned *received, unsigned *hid_sent, unsigned *hid_failed);

/* Low-priority lifecycle owner only, while not starting/stopping the app.
 * Periodic output defaults off; the shell controls it with carplay_perf. */
void carplay_app_performance_report(void);

/* RTSP owner only: tunneled iAP2 writes use encrypted event /command. */
bool carplay_app_send_iap(const uint8_t *, size_t);

/* Thread-safe enqueue; event-channel write remains on the RTSP owner.
 * Returns RTCP_ESTATE during lifecycle/input entry contention. */
int carplay_app_request_siri(void);

typedef enum
{
    CARPLAY_PHONE_BACK = 1,
    CARPLAY_PHONE_HOME,
    CARPLAY_PHONE_PREVIOUS,
    CARPLAY_PHONE_PLAY_PAUSE,
    CARPLAY_PHONE_NEXT,
} carplay_phone_action_t;

typedef struct
{
    char title[129], artist[129], album[129];
    bool has_playing, playing;
    bool has_duration, has_elapsed;
    uint32_t duration_ms, elapsed_ms;
    uint32_t artwork_revision;
} carplay_phone_track_t;

#define CARPLAY_PHONE_MANEUVER_ROWS 3U
#define CARPLAY_PHONE_ARTWORK_MAX   (192U * 1024U)

typedef struct
{
    uint16_t index;
    bool present;
    char instruction[129], distance[65];
} carplay_phone_maneuver_t;

typedef struct
{
    carplay_phone_track_t track;
    char route_road[129], route_destination[129], route_remaining[129], route_turn_distance[129];
    carplay_phone_maneuver_t maneuvers[CARPLAY_PHONE_MANEUVER_ROWS];
    size_t lane_count;
    struct
    {
        int16_t angle;
        bool highlighted;
    } lanes[8];
    bool has_battery, has_charging, has_signal, has_airplane, airplane, has_call;
    bool has_external_power, external_power, has_cellular, cellular;
    uint16_t battery;
    uint8_t charging, signal, call;
    char carrier[129];
} carplay_phone_media_t;

void carplay_app_set_phone_appearance(rtcp_appearance_setting_t);
rtcp_appearance_setting_t carplay_app_phone_appearance(void);
int carplay_app_phone_action(carplay_phone_action_t);
/**
 * Copy the latest metadata into caller-owned storage from a task context.
 * @param[out] media Destination snapshot. An inactive receiver yields an empty snapshot.
 * @return True on success, false for a null destination or mutex contention.
 */
bool carplay_app_phone_media(carplay_phone_media_t *media);
/**
 * Copy the encoded artwork from a task context without waiting for the receiver.
 * @param revision Nonzero artwork revision from the snapshot.
 * @param[out] buffer Caller-owned byte storage.
 * @param capacity Destination size in bytes.
 * @return Bytes copied, or zero if the revision is stale, unavailable, or cannot fit.
 */
size_t carplay_app_phone_artwork(uint32_t revision, uint8_t *buffer, size_t capacity);

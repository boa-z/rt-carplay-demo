/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * RT-Thread + Luban-Lite receiver composition.
 *
 * Boot order (start): entropy + Ed25519 identity, pairing store, SoftAP and
 * vendor IE, DHCP, /info plist, AirPlay RTSP server, mDNS, input bridge,
 * on-board video sink (RTCARPLAY_VIDEO), poll thread.  Everything is torn
 * down by that thread when it observes the stop flag, so the RTSP server and
 * the event cipher stay single-threaded.
 */
#include "carplay_app.h"
#include "rtcp/receiver.h"

#include <rtthread.h>

#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef RT_USING_RTC
#include <sys/time.h>
#endif

#include "airplay_ed25519.h"
#include "airplay_info_plist.h"
#include "airplay_bplist.h"
#include "airplay_pairing_store.h"
#include "pairing_files.h"
#include "airplay_txt.h"
#include "airplay_vendor_ie.h"
#include "carplay_ctrl.h"
#include "input_bridge.h"
#include "launcher_preferences.h"
#include "mdns_browser.h"
#include "mdns_responder.h"
#include "rtcp/input.h"
#include "rtcp/rtcp.h"
#include "rtcp_lubanlite.h"
#include "rtcp_rtthread.h"
#if defined(RTCARPLAY_DEMO_LAUNCHER)
#include "../assets/rt-carplay-oem-png.h"
#endif
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
#include "vehicle_protocol_demo.h"
#endif

#if defined(RTCARPLAY_VIDEO)
#include "video_sink_lubanlite.h"
#endif

#if defined(RTCARPLAY_AUDIO)
#include "audio_sink_lubanlite.h"
#if defined(RTCARPLAY_MICROPHONE)
#include "audio_input_lubanlite.h"
#endif
#endif

#if defined(RTCARPLAY_TOUCH_PANEL)
#include "touch_panel_lubanlite.h"
#endif

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "fusion_display.h"
#endif

#if defined(RTCARPLAY_IAP2) || defined(RTCARPLAY_USB_HOST)
#include "iap2_link_lubanlite.h"
#endif

#define APP_LOG(fmt, ...) rt_kprintf("[carplay] " fmt "\n", ##__VA_ARGS__)

static atomic_bool s_classic_diagnostics;

static void on_classic_playback(const rtcp_now_playing_details_t *update, void *context)
{
    (void)context;
    if (!atomic_load(&s_classic_diagnostics))
        return;
    APP_LOG("classic playback clear=%u duration=%u/%u elapsed=%u/%u artwork=%u/%u transfer=%u",
            (unsigned)update->invalidated, (unsigned)update->has_duration, (unsigned)update->duration_ms,
            (unsigned)update->has_elapsed, (unsigned)update->elapsed_ms,
            (unsigned)update->has_artwork, (unsigned)update->artwork_length, (unsigned)update->has_artwork_transfer_id);
}

static int cmd_carplay_classic_diag(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")))
        atomic_store(&s_classic_diagnostics, !strcmp(argv[1], "on"));
    else if (argc != 1)
        return RTCP_EINVAL;
    APP_LOG("classic diagnostics %s",
            atomic_load(&s_classic_diagnostics) ? "on" : "off");
    return RTCP_OK;
}
MSH_CMD_EXPORT_ALIAS(cmd_carplay_classic_diag, carplay_classic_diag, Toggle Classic metadata diagnostics);

#if defined(RTCARPLAY_DEMO_LAUNCHER)
static const rtcp_oem_icon_t demo_oem_icon = {
    .png = carplay_demo_oem_icon_png,
    .png_len = sizeof(carplay_demo_oem_icon_png),
    .width_pixels = CARPLAY_DEMO_OEM_ICON_SIZE,
    .height_pixels = CARPLAY_DEMO_OEM_ICON_SIZE,
};
#endif

#ifdef RT_USING_RTC
static rtcp_device_time_t s_pending_phone_time;
static atomic_int s_phone_clock_offset = ATOMIC_VAR_INIT(INT_MIN);
static atomic_bool s_clock_automatic = ATOMIC_VAR_INIT(true);
static atomic_bool s_phone_clock_received;
static struct rt_mutex s_clock_lock;

static int init_clock_lock(void)
{
    return rt_mutex_init(&s_clock_lock, "cpclock", RT_IPC_FLAG_PRIO);
}
INIT_PREV_EXPORT(init_clock_lock);

void carplay_app_on_device_time(const rtcp_device_time_t *update, void *context)
{
    (void)context;
    rt_base_t irq = rt_hw_interrupt_disable();
    if (!update)
    {
        s_pending_phone_time = (rtcp_device_time_t){ 0 };
        atomic_store(&s_phone_clock_received, false);
        rt_hw_interrupt_enable(irq);
        return;
    }
    if (update->has_unix_seconds)
        atomic_store(&s_phone_clock_received, true);
    if (!atomic_load(&s_clock_automatic))
    {
        rt_hw_interrupt_enable(irq);
        return;
    }
    if (update->has_unix_seconds)
    {
        s_pending_phone_time.has_unix_seconds = true;
        s_pending_phone_time.unix_seconds = update->unix_seconds;
    }
    if (update->has_time_zone_offset)
    {
        s_pending_phone_time.has_time_zone_offset = true;
        s_pending_phone_time.time_zone_offset_minutes = update->time_zone_offset_minutes;
    }
    if (update->has_daylight_saving_offset)
    {
        s_pending_phone_time.has_daylight_saving_offset = true;
        s_pending_phone_time.daylight_saving_offset_minutes = update->daylight_saving_offset_minutes;
    }
    rt_hw_interrupt_enable(irq);
}

bool carplay_app_phone_clock_received(void)
{
    return atomic_load(&s_phone_clock_received);
}

int carplay_app_clock_offset_minutes(void)
{
    int offset = atomic_load(&s_phone_clock_offset);
    if (offset != INT_MIN)
        return offset;
#ifdef RT_LIBC_DEFAULT_TIMEZONE
    return RT_LIBC_DEFAULT_TIMEZONE * 60;
#else
    return 0;
#endif
}

bool carplay_app_clock_is_automatic(void)
{
    return atomic_load(&s_clock_automatic);
}

int carplay_app_set_clock_automatic(bool automatic)
{
    if (rt_mutex_take(&s_clock_lock, RT_WAITING_FOREVER) != RT_EOK)
        return RTCP_EIO;
    rt_base_t irq = rt_hw_interrupt_disable();
    atomic_store(&s_clock_automatic, automatic);
    if (!automatic)
        s_pending_phone_time = (rtcp_device_time_t){ 0 };
    rt_hw_interrupt_enable(irq);
    rt_mutex_release(&s_clock_lock);
    return RTCP_OK;
}

static int set_clock_utc(int64_t seconds)
{
    struct timeval value = { .tv_sec = (time_t)seconds };
    if ((int64_t)value.tv_sec != seconds || seconds < 0)
        return RTCP_EINVAL;
    return settimeofday(&value, NULL) == 0 ? RTCP_OK : RTCP_EIO;
}

#if defined(RTCARPLAY_NETWORK_TIME)
int carplay_app_set_network_clock(int64_t utc_seconds)
{
    if (rt_mutex_take(&s_clock_lock, RT_WAITING_FOREVER) != RT_EOK)
        return RTCP_EIO;
    int result = set_clock_utc(utc_seconds);
    rt_mutex_release(&s_clock_lock);
    return result;
}
#endif

int carplay_app_set_local_clock(int64_t local_seconds, int offset_minutes)
{
    if (offset_minutes < -14 * 60 || offset_minutes > 14 * 60 || local_seconds < 0)
        return RTCP_EINVAL;
    if (rt_mutex_take(&s_clock_lock, RT_WAITING_FOREVER) != RT_EOK)
        return RTCP_EIO;
    int result = RTCP_ESTATE;
    if (!atomic_load(&s_clock_automatic))
    {
        result = set_clock_utc(local_seconds - offset_minutes * 60L);
        if (result == RTCP_OK)
            atomic_store(&s_phone_clock_offset, offset_minutes);
    }
    rt_mutex_release(&s_clock_lock);
    return result;
}

static void apply_phone_clock(void)
{
    static int time_zone_minutes = INT_MIN;
    static int daylight_saving_minutes;
    rt_base_t irq = rt_hw_interrupt_disable();
    bool pending = s_pending_phone_time.has_unix_seconds || s_pending_phone_time.has_time_zone_offset ||
                   s_pending_phone_time.has_daylight_saving_offset;
    rt_hw_interrupt_enable(irq);
    if (!pending || rt_mutex_take(&s_clock_lock, RT_WAITING_NO) != RT_EOK)
        return;

    irq = rt_hw_interrupt_disable();
    rtcp_device_time_t update = s_pending_phone_time;
    s_pending_phone_time = (rtcp_device_time_t){ 0 };
    rt_hw_interrupt_enable(irq);

    if (update.has_unix_seconds)
    {
        if (set_clock_utc(update.unix_seconds) != RTCP_OK)
            APP_LOG("iPhone clock: RTC rejected UTC seconds");
        else
            APP_LOG("iPhone clock: RTC synchronized (UTC)");
    }
    if (update.has_time_zone_offset &&
        update.time_zone_offset_minutes >= -14 * 60 && update.time_zone_offset_minutes <= 14 * 60)
        time_zone_minutes = update.time_zone_offset_minutes;
    if (update.has_daylight_saving_offset)
        daylight_saving_minutes = update.daylight_saving_offset_minutes;
    if (time_zone_minutes != INT_MIN && (update.has_time_zone_offset || update.has_daylight_saving_offset))
    {
        int offset = time_zone_minutes + daylight_saving_minutes;
        int previous = atomic_exchange(&s_phone_clock_offset, offset);
        if (previous != offset)
            APP_LOG("iPhone clock: dashboard UTC offset %d minutes", offset);
    }
    rt_mutex_release(&s_clock_lock);
}
#endif

/* Shell and receiver/demo threads share this flag; independent of sessions. */
static atomic_bool s_perf_logging = ATOMIC_VAR_INIT(false);

static int cmd_carplay_perf(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")))
    {
        atomic_store(&s_perf_logging, !strcmp(argv[1], "on"));
    }
    else if (argc != 1)
    {
        rt_kprintf("Usage: carplay_perf [on|off]\n");
        return RTCP_EINVAL;
    }
    rt_kprintf("carplay performance logging: %s\n",
               atomic_load(&s_perf_logging) ? "on" : "off");
    return RTCP_OK;
}
MSH_CMD_EXPORT_ALIAS(cmd_carplay_perf, carplay_perf,
                     Periodic performance logging : on | off);

/* carplay_start uses the same geometry as the SDK autostart path.  In app
 * mode the board's Kconfig values (which must match the panel) win; a caller
 * that links the library directly gets the 1280x480 widescreen fallback. */
#ifndef CARPLAY_APP_DEFAULT_WIDTH
#if defined(RTCARPLAY_SCREEN_WIDTH)
#define CARPLAY_APP_DEFAULT_WIDTH RTCARPLAY_SCREEN_WIDTH
#else
#define CARPLAY_APP_DEFAULT_WIDTH 1280U
#endif
#endif
#ifndef CARPLAY_APP_DEFAULT_HEIGHT
#if defined(RTCARPLAY_SCREEN_HEIGHT)
#define CARPLAY_APP_DEFAULT_HEIGHT RTCARPLAY_SCREEN_HEIGHT
#else
#define CARPLAY_APP_DEFAULT_HEIGHT 480U
#endif
#endif
#ifndef CARPLAY_APP_DEFAULT_ROTATION
#if defined(RTCARPLAY_ROTATION)
#define CARPLAY_APP_DEFAULT_ROTATION RTCARPLAY_ROTATION
#else
#define CARPLAY_APP_DEFAULT_ROTATION 0U
#endif
#endif

/* Advertised PCM sample rate.  The sink configures the codec from the format
 * the SETUP actually negotiates; this only selects which capability mask the
 * /info plist publishes (0 = both 44.1 and 48 kHz). */
#ifndef CARPLAY_APP_AUDIO_SAMPLE_RATE
#define CARPLAY_APP_AUDIO_SAMPLE_RATE 0U
#endif

/* Compatibility escape hatch for an otherwise identical legacy-mask build. */
#ifndef CARPLAY_APP_LEGACY_FEATURES
#define CARPLAY_APP_LEGACY_FEATURES 0
#endif

#define DEFAULT_NAME       "rt-carplay"
#define DEFAULT_MODEL      "ArtInChip"
#define DEFAULT_SSID       "ArtInChip CarPlay"
#define DEFAULT_PASSPHRASE "88888888"
/* The vendor firmware runs the CarPlay SoftAP on 5 GHz channel 36, which the
 * RTL8733BS supports on the reference board. */
#define DEFAULT_CHANNEL    36U
#define DEFAULT_SEED_PATH  "/data/rt-carplay.seed"
#define DEFAULT_STORE_PATH "/data/rt-carplay-pairings.bin"
/* iOS CarPlay runs AirPlay Pair-Setup with this fixed setup code; there is
 * no PIN entry UI.  DiPlay and LIVI hardcode the same value. */
#define DEFAULT_PAIR_CODE "3939"

#define POLL_THREAD_STACK_BYTES (32U * 1024U)
#define POLL_THREAD_PRIORITY    15U
#define POLL_THREAD_TICK        20U

/* CarPlay Control reverse connection.
 *
 * The iPhone advertises _carplay-ctrl._tcp once it commits to CarPlay; the
 * receiver browses it and sends GET /ctrl-int/1/connect.  The vendor reference
 * stack on this board does exactly this (mdnsd_start_carplay_client_connect),
 * as do DiPlay, LIVI, catplay and carplayd's rx_connect.  carplayd's session
 * notes call the connect-out what makes the phone open the RTSP session.
 *
 * Kept switchable (carplay_ctrl on|off) so an A/B run can isolate its effect
 * from the rest of the session establishment. */
#define CARPLAY_CTRL_SERVICE    "_carplay-ctrl._tcp.local"
#define CARPLAY_CTRL_USER_AGENT "AirPlay/" AIRPLAY_INFO_DEFAULT_SOURCE_VERSION
#define CARPLAY_CTRL_RETRY_MS   2000U

#define PAIR_CODE_DIGITS   4U
#define TOUCH_QUEUE_LENGTH 32U
#define TOUCH_QUEUE_MASK   (TOUCH_QUEUE_LENGTH - 1U)
#define TOUCH_BATCH        8U

#define IP_WAIT_STEP_MS 100U
#define IP_WAIT_TRIES   100U

typedef struct
{
    uint8_t phase;
    uint16_t pointer_id;
    uint16_t x;
    uint16_t y;
} touch_event_t;

/* Published by the poll owner; no Browser-owned pointers cross threads. */
typedef struct
{
    bool available, enabled, has_endpoint, peer_valid;
    uint64_t device_id;
    unsigned endpoints, attempts, responses;
    int last_status;
    unsigned queries, tx_errors, packets, updates;
    uint8_t peer_ipv4[4];
    mdns_browser_endpoint_t endpoint;
} control_status_t;

typedef struct
{
    struct rtcp_os os;
    airplay_ed25519_keypair_t keypair;

    char name[33];
    char model[33];
    char ssid[33];
    char passphrase[65];
    uint8_t channel;
    char pairing_code[PAIR_CODE_DIGITS + 1U];

    char device_id[18];
    char bluetooth_id[18];
    char host_name[32];
    char seed_path[64];
    char store_path[80];
    char public_key_hex[AIRPLAY_ED25519_PUBLIC_KEY_BYTES * 2U + 1U];

    uint8_t wifi_mac[6];
    bool wifi_mac_valid;

    uint8_t accessory_identifier[64];
    size_t accessory_identifier_len;

    uint16_t width;
    uint16_t height;
    uint8_t max_fps;
    uint16_t rotation;
    bool wired;

    uint8_t ipv4[4];

    uint8_t info_plist[AIRPLAY_INFO_PLIST_MAX_BYTES];
    const rtcp_oem_icon_t *oem_icons;
    size_t oem_icon_count;
    const char *oem_icon_label;
    size_t info_plist_len;
    uint8_t txt[512];
    size_t txt_len;

    mdns_responder_t *mdns;
    mdns_browser_t *browser;
    rtcp_receiver_t *receiver;
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
    carplay_vehicle_protocol_demo_t vehicle_demo;
    carplay_vehicle_protocol_snapshot_t vehicle_snapshot;
#endif
    input_bridge_t *bridge;
    rt_mutex_t lock;
    rt_thread_t thread;

    uint16_t airplay_port;
    atomic_bool airplay_connected;

    /* CarPlay Control browse/connect state (owned by the poll thread). */
    bool ctrl_enabled;
    uint64_t ctrl_device_id;
    uint32_t ctrl_last_attempt_ms;
    unsigned ctrl_attempts;
    unsigned ctrl_responses;
    int ctrl_last_status;
    /* Discovery candidate from an AirPlay query on the AP, superseded by
     * the actual RTSP peer. Used only for a unicast mDNS query. */
    uint8_t ctrl_peer_ipv4[4];
    bool ctrl_peer_valid;
    unsigned browse_queries;
    unsigned browse_tx_errors;
    unsigned browse_packets;
    unsigned browse_updates;
    char ctrl_last_endpoint[MDNS_MAX_NAME];
    /* Requests and copied status are protected by lock. */
    bool ctrl_enable_pending, ctrl_requested_enabled, ctrl_connect_pending;
    control_status_t ctrl_status;

    carplay_app_screen_cb_t on_screen;
#if defined(RTCARPLAY_VIDEO)
    uint64_t screen_keyframe_retry_ms;
#endif
    void *screen_context;

    rtcp_receiver_state_t receiver_state;
    rtcp_screen_stats_t screen_stats;
    rtcp_audio_stats_t audio_stats;
    rtcp_timing_stats_t timing_stats;
    rtcp_audio_modes_t audio_modes;
#if defined(RTCARPLAY_VIDEO)
    uint32_t perf_ms;
    rtcp_screen_stats_t perf_screen;
    carplay_video_perf_t perf_video;
#endif
    uint32_t screen_sample_ms;
    uint32_t screen_report_ms;
    uint32_t screen_report_frames;
    bool siri_pending;
    unsigned projection_pending;
    bool projection_pause_waiting;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    uint32_t projection_retained_stream;
    bool projection_resume_waiting;
    bool projection_resume_confirmed;
#endif
    rtcp_projection_owner_t projection_owner;
    carplay_phone_action_t phone_action_pending;
    int phone_appearance_sent;
    carplay_phone_media_t phone_media;
    carplay_phone_track_t iap2_track;
    size_t phone_artwork_size;
    uint32_t phone_artwork_revision;
#if defined(RTCARPLAY_IAP2) || defined(RTCARPLAY_USB_HOST)
    carplay_phone_maneuver_t maneuver_cache[8];
    unsigned maneuver_slot;
    bool has_lane_index, has_lane_payload, lanes_hidden;
    uint16_t lane_index, lane_payload_index;
#endif
#if defined(RTCARPLAY_IAP2)
    carplay_iap2_tunnel_callbacks_t tunnel_callbacks;
#endif
    bool cipher_pending;
    bool cipher_requested;
    bool cipher_applied;
    unsigned touch_queued;
    unsigned touch_queue_full;
    atomic_uint hid_reports_sent;
    atomic_uint hid_reports_failed;
    touch_event_t touch[TOUCH_QUEUE_LENGTH];
    unsigned touch_head;
    unsigned touch_tail;

    unsigned screen_frames;
    unsigned mdns_errors;

    bool ap_started;
    bool store_initialized;
#if defined(RTCARPLAY_VIDEO)
    bool video_started;
#endif
#if defined(RTCARPLAY_AUDIO)
    bool audio_started;
    bool touch_panel_started;
    unsigned audio_packets;
#endif
} app_state_t;

static app_state_t s_app;
#if defined(RTCARPLAY_DEMO_LAUNCHER)
static uint8_t s_phone_artwork[CARPLAY_PHONE_ARTWORK_MAX];
static uint32_t s_artwork_revision;
#endif
/* Publication flags must survive reset of the per-session data. Lifecycle
 * entry is nonblocking: concurrent/reentrant start/stop returns ESTATE. */
static atomic_flag s_lifecycle_call = ATOMIC_FLAG_INIT;
/* Readers may coexist with input and each other. A lifecycle writer blocks
 * new readers and sleeps until existing copies/diagnostics have returned;
 * no reader can retain the session mutex while it is reset or destroyed. */
#define SNAPSHOT_WRITER (UINT_MAX / 2U + 1U)
static atomic_uint s_snapshot_readers = ATOMIC_VAR_INIT(0);
static atomic_int s_pairing_storage_result = ATOMIC_VAR_INIT(RTCP_OK);
static atomic_int s_phone_appearance = ATOMIC_VAR_INIT(RTCP_APPEARANCE_AUTOMATIC);

static bool snapshot_enter(void)
{
    unsigned readers = atomic_load_explicit(&s_snapshot_readers, memory_order_acquire);
    do
    {
        if (readers >= SNAPSHOT_WRITER - 1U)
            return false;
    } while (!atomic_compare_exchange_weak_explicit(&s_snapshot_readers, &readers,
                                                    readers + 1U, memory_order_acq_rel,
                                                    memory_order_acquire));
    return true;
}

static void snapshot_leave(void)
{
    (void)atomic_fetch_sub_explicit(&s_snapshot_readers, 1U, memory_order_release);
}

static void snapshots_retire(void)
{
    (void)atomic_fetch_or_explicit(&s_snapshot_readers, SNAPSHOT_WRITER,
                                   memory_order_acq_rel);
    while (atomic_load_explicit(&s_snapshot_readers, memory_order_acquire) != SNAPSHOT_WRITER)
        rt_thread_mdelay(1);
}

static void snapshots_enable(void)
{
    atomic_store_explicit(&s_snapshot_readers, 0, memory_order_release);
}
static atomic_bool s_receiver_stop = ATOMIC_VAR_INIT(false);
static atomic_bool s_receiver_worker_live = ATOMIC_VAR_INIT(false);
static atomic_bool s_receiver_running = ATOMIC_VAR_INIT(false);
static atomic_bool s_receiver_shutting_down = ATOMIC_VAR_INIT(false);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static bool copy_string(char *destination, size_t capacity, const char *source)
{
    if (source == NULL)
        return false;
    size_t length = strlen(source);
    if (length == 0U || length >= capacity)
        return false;
    memcpy(destination, source, length + 1U);
    return true;
}

static void hex_encode(const uint8_t *bytes, size_t len, char *output)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < len; ++i)
    {
        output[i * 2U] = digits[bytes[i] >> 4U];
        output[i * 2U + 1U] = digits[bytes[i] & 0x0fU];
    }
    output[len * 2U] = '\0';
}

static bool load_seed(const char *path,
                      uint8_t seed[AIRPLAY_ED25519_SEED_BYTES])
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    uint8_t bytes[AIRPLAY_ED25519_SEED_BYTES];
    size_t got = fread(bytes, 1U, sizeof(bytes), file);
    bool extra = fgetc(file) != EOF;
    fclose(file);
    bool ok = got == sizeof(bytes) && !extra;
    if (ok)
        memcpy(seed, bytes, sizeof(bytes));
    memset(bytes, 0, sizeof(bytes));
    return ok;
}

static bool write_seed(const char *path,
                       const uint8_t seed[AIRPLAY_ED25519_SEED_BYTES])
{
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return false;
    bool ok = fwrite(seed, 1U, AIRPLAY_ED25519_SEED_BYTES, file) ==
              AIRPLAY_ED25519_SEED_BYTES;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

/* ------------------------------------------------------------------ */
/* Pairing persistence (files on the data partition)                   */
/* ------------------------------------------------------------------ */

static int store_load(uint8_t *blob, size_t capacity, size_t *len,
                      void *context)
{
    app_state_t *app = context;
    return carplay_file_load(app->store_path, blob, capacity, len);
}

static int store_save(const uint8_t *blob, size_t len, void *context)
{
    app_state_t *app = context;
    int ret = carplay_file_save(app->store_path, blob, len);
    s_pairing_storage_result = ret;
    return ret;
}

/* ------------------------------------------------------------------ */
/* Pairing providers                                                   */
/* ------------------------------------------------------------------ */

static bool provider_random(uint8_t *output, size_t len, void *context)
{
    app_state_t *app = context;
    if (app->os.random == NULL)
        return false;
    return app->os.random(output, len);
}

static bool provider_save_controller(const uint8_t *id, size_t length,
                                     const uint8_t public_key[32], void *context)
{
    bool ok = airplay_pairing_store_save_pairing(id, length, public_key, context);
    if (!ok && s_pairing_storage_result == RTCP_OK)
        s_pairing_storage_result = RTCP_EIO;
    APP_LOG("AirPlay pairing persistence=%s error=%d", ok ? "saved" : "failed", s_pairing_storage_result);
    return ok;
}

static bool provider_sign(const uint8_t *message, size_t message_len,
                          uint8_t signature[AIRPLAY_ED25519_SIGNATURE_BYTES],
                          void *context)
{
    app_state_t *app = context;
    return airplay_ed25519_sign(&app->keypair, message, message_len,
                                signature);
}

static bool provider_verify(
    const uint8_t public_key[AIRPLAY_ED25519_PUBLIC_KEY_BYTES],
    const uint8_t *message, size_t message_len,
    const uint8_t signature[AIRPLAY_ED25519_SIGNATURE_BYTES], void *context)
{
    (void)context;
    return airplay_ed25519_verify(public_key, message, message_len, signature);
}

static void fill_pairing(app_state_t *app,
                         rtcp_pairing_provider_t *pairing)
{
    memset(pairing, 0, sizeof(*pairing));

    pairing->pair_setup.pairing_code = app->pairing_code;
    pairing->pair_setup.accessory_identifier = app->accessory_identifier;
    pairing->pair_setup.accessory_identifier_len =
        app->accessory_identifier_len;
    pairing->pair_setup.accessory_public_key = app->keypair.public_key;
    pairing->pair_setup.random = provider_random;
    pairing->pair_setup.sign_accessory = provider_sign;
    pairing->pair_setup.verify_controller = provider_verify;
    pairing->pair_setup.save_pairing = provider_save_controller;
    pairing->pair_setup.provider_context = app;

    pairing->pair_verify.accessory_identifier = app->accessory_identifier;
    pairing->pair_verify.accessory_identifier_len =
        app->accessory_identifier_len;
    pairing->pair_verify.random = provider_random;
    pairing->pair_verify.sign_accessory = provider_sign;
    pairing->pair_verify.verify_controller = provider_verify;
    pairing->pair_verify.lookup_controller =
        airplay_pairing_store_lookup_controller;
    pairing->pair_verify.provider_context = app;
}

/* ------------------------------------------------------------------ */
/* Server callbacks (poll thread)                                      */
/* ------------------------------------------------------------------ */

static int on_screen_message(rtcp_video_message_type_t type,
                             uint8_t *annex_b, size_t annex_b_len,
                             size_t prefix_capacity, uint64_t sender_time_ns,
                             uint64_t arrival_time_us, void *context)
{
    (void)prefix_capacity;
#if !defined(RTCARPLAY_VIDEO)
    (void)arrival_time_us;
#endif
    app_state_t *app = context;
    bool configuration = type == RTCP_VIDEO_CONFIG_H264;
    if (!configuration)
    {
        ++app->screen_frames;
        if (app->screen_frames == 1U ||
            (atomic_load(&s_perf_logging) && app->screen_frames % 120U == 0U))
        {
            APP_LOG("screen frames=%u last=%u bytes", app->screen_frames,
                    (unsigned)annex_b_len);
        }
    }
    else if (app->screen_frames == 0U || atomic_load(&s_perf_logging))
    {
        APP_LOG("screen H.264 configuration: %u bytes", (unsigned)annex_b_len);
    }
#if defined(RTCARPLAY_VIDEO)
    if (app->on_screen == carplay_video_sink_frame)
    {
        carplay_video_sink_frame_at(configuration, annex_b, annex_b_len, sender_time_ns, arrival_time_us);
        return RTCP_OK;
    }
#endif
    if (app->on_screen != NULL)
    {
        app->on_screen(configuration, annex_b, annex_b_len, sender_time_ns,
                       app->screen_context);
    }
    return RTCP_OK;
}

#if defined(RTCARPLAY_AUDIO)
static int on_audio_pcm(uint16_t stream_type, const uint8_t *data, size_t len,
                        const rtcp_audio_format_t *format,
                        uint32_t rtp_timestamp, uint64_t arrival_us,
                        void *context)
{
    (void)rtp_timestamp;
    (void)arrival_us;
    app_state_t *app = context;
    if (!app->audio_started || s_receiver_shutting_down)
        return RTCP_OK;
    if (format == NULL)
        return RTCP_EINVAL;
    if (app->audio_packets == 0U)
    {
        APP_LOG("audio stream=%u %u Hz %u ch first packet %u bytes",
                (unsigned)stream_type, (unsigned)format->sample_rate,
                (unsigned)format->channels, (unsigned)len);
    }
    ++app->audio_packets;
    return carplay_audio_sink_packet(stream_type, format, data, len, rtp_timestamp, arrival_us);
}

#if defined(RTCARPLAY_MICROPHONE)
static bool on_audio_input_start(const rtcp_audio_input_config_t *input, void *context)
{
    (void)context;
    return carplay_audio_input_start(input);
}
static void on_audio_input_stop(void *context)
{
    (void)context;
    carplay_audio_input_request_stop();
}
#endif
#endif

#if defined(RTCARPLAY_AUDIO) || defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static void on_audio_modes(const rtcp_audio_modes_t *modes, void *context)
{
    app_state_t *app = context;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (app->projection_resume_waiting && modes->has_screen)
        app->projection_resume_confirmed =
            rtcp_receiver_get_projection_owner(app->receiver) == RTCP_PROJECTION_PHONE;
#endif
#if defined(RTCARPLAY_AUDIO)
    if (!app->audio_started)
        return;
    if (rt_mutex_take(app->lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        if (modes->has_main_audio)
        {
            app->audio_modes.has_main_audio = true;
            app->audio_modes.main_audio_entity = modes->main_audio_entity;
        }
        if (modes->has_main_audio_permanent)
        {
            app->audio_modes.has_main_audio_permanent = true;
            app->audio_modes.main_audio_permanent_entity = modes->main_audio_permanent_entity;
        }
        if (modes->has_screen)
        {
            app->audio_modes.has_screen = true;
            app->audio_modes.screen_entity = modes->screen_entity;
        }
        if (modes->has_screen_permanent)
        {
            app->audio_modes.has_screen_permanent = true;
            app->audio_modes.screen_permanent_entity = modes->screen_permanent_entity;
        }
        if (modes->has_call)
        {
            app->audio_modes.has_call = true;
            app->audio_modes.call_active = modes->call_active;
        }
        if (modes->has_speech)
        {
            app->audio_modes.has_speech = true;
            app->audio_modes.speech_mode = modes->speech_mode;
        }
        (void)rt_mutex_release(app->lock);
    }
    carplay_audio_sink_modes(modes);
#endif
}
#endif

#if defined(RTCARPLAY_AUDIO)
static bool on_audio_duck(bool ducked, float db, uint32_t duration, void *context)
{
    (void)context;
    return carplay_audio_sink_duck(ducked, db, duration);
}

static void on_audio_stop(unsigned slot, uint16_t type, void *context)
{
    (void)context;
    (void)type;
    carplay_audio_sink_slot_stop(slot);
}

static void publish_phone_volume(app_state_t *app)
{
    int percent = carplay_audio_sink_volume_percent();
    if (percent >= 0)
    {
        float volume_db = percent == 0 ? -144.0f : -45.0f + (float)percent * 0.45f;
        (void)rtcp_receiver_set_volume_db(app->receiver, volume_db);
    }
}

static void on_volume(float volume_db, void *context)
{
    app_state_t *app = context;
    if (!app->audio_started || s_receiver_shutting_down)
        return;
    carplay_audio_sink_set_volume_db(volume_db);
    publish_phone_volume(app);
}
#endif

static void copy_phone_text(char *destination, size_t size, const char *text)
{
    if (text)
        rt_snprintf(destination, size, "%s", text);
}

static void update_phone_track(carplay_phone_track_t *track, const rtcp_now_playing_details_t *update,
                               bool artwork_has_transfer_lifecycle)
{
    if (update->invalidated)
    {
        *track = (carplay_phone_track_t){ 0 };
        return;
    }
    if (update->item.title && strcmp(track->title, update->item.title))
    {
        track->has_duration = false;
        track->has_elapsed = false;
        /* iAP2 reports cover replacement separately via the file transfer ID. */
        if (!artwork_has_transfer_lifecycle || !*update->item.title)
            track->artwork_revision = 0;
    }
    copy_phone_text(track->title, sizeof(track->title), update->item.title);
    copy_phone_text(track->artist, sizeof(track->artist), update->item.artist);
    copy_phone_text(track->album, sizeof(track->album), update->item.album);
    if (update->item.has_playing)
    {
        track->has_playing = true;
        track->playing = update->item.playing;
    }
    if (update->has_duration)
    {
        track->has_duration = true;
        track->duration_ms = update->duration_ms;
    }
    if (update->has_elapsed)
    {
        track->has_elapsed = true;
        track->elapsed_ms = update->elapsed_ms;
    }
}

static void update_phone_artwork(app_state_t *app, carplay_phone_track_t *track,
                                 const rtcp_now_playing_details_t *update)
{
#if defined(RTCARPLAY_DEMO_LAUNCHER)
    if (update->has_artwork)
    {
        if (update->artwork && update->artwork_length && update->artwork_length <= sizeof(s_phone_artwork))
        {
            if (!app->phone_artwork_revision || app->phone_artwork_size != update->artwork_length ||
                memcmp(s_phone_artwork, update->artwork, update->artwork_length))
            {
                memcpy(s_phone_artwork, update->artwork, update->artwork_length);
                app->phone_artwork_size = update->artwork_length;
                if (++s_artwork_revision == 0)
                    ++s_artwork_revision;
                app->phone_artwork_revision = s_artwork_revision;
                APP_LOG("cover stored revision=%u bytes=%u", (unsigned)s_artwork_revision,
                        (unsigned)app->phone_artwork_size);
            }
            track->artwork_revision = app->phone_artwork_revision;
        }
        else
        {
            track->artwork_revision = 0;
        }
    }
#else
    (void)app;
    (void)track;
    (void)update;
#endif
}

static void on_airplay_metadata(const rtcp_now_playing_details_t *update, void *context)
{
    app_state_t *app = context;
    on_classic_playback(update, context);
    if (!app->lock || rt_mutex_take(app->lock, RT_WAITING_FOREVER) != RT_EOK)
        return;
    carplay_phone_track_t *track = &app->phone_media.track;
    update_phone_track(track, update, false);
    update_phone_artwork(app, track, update);
    (void)rt_mutex_release(app->lock);
}

static void clear_phone_media(app_state_t *app)
{
    memset(&app->phone_media, 0, sizeof(app->phone_media));
    app->iap2_track = (carplay_phone_track_t){ 0 };
    app->phone_artwork_size = 0;
    app->phone_artwork_revision = 0;
#ifdef RT_USING_RTC
    atomic_store(&s_phone_clock_received, false);
#endif
#if defined(RTCARPLAY_IAP2) || defined(RTCARPLAY_USB_HOST)
    memset(app->maneuver_cache, 0, sizeof(app->maneuver_cache));
    app->maneuver_slot = 0;
    app->has_lane_index = app->has_lane_payload = app->lanes_hidden = false;
#endif
}

#if defined(RTCARPLAY_IAP2) || defined(RTCARPLAY_USB_HOST)
static bool phone_metadata_enter(void)
{
    if (!snapshot_enter())
        return false;
    if (s_receiver_running && !s_receiver_shutting_down && s_app.lock &&
        rt_mutex_take(s_app.lock, RT_WAITING_FOREVER) == RT_EOK)
        return true;
    snapshot_leave();
    return false;
}

static void phone_metadata_leave(void)
{
    (void)rt_mutex_release(s_app.lock);
    snapshot_leave();
}

static void on_iap2_playback(const rtcp_now_playing_details_t *update, void *context)
{
    on_classic_playback(update, context);
    if (!phone_metadata_enter())
        return;
    update_phone_track(&s_app.iap2_track, update, true);
    update_phone_artwork(&s_app, &s_app.iap2_track, update);
    phone_metadata_leave();
}

static void on_classic_phone(const rtcp_phone_status_t *update, void *context)
{
    (void)context;
    if (atomic_load(&s_classic_diagnostics))
        APP_LOG("classic phone kind=%u", (unsigned)update->kind);
    if (!phone_metadata_enter())
        return;
    carplay_phone_media_t *media = &s_app.phone_media;
    switch (update->kind)
    {
    case RTCP_PHONE_STATUS_INVALIDATED:
        media->has_battery = media->has_charging = media->has_signal = media->has_airplane = false;
        media->has_call = media->has_external_power = media->has_cellular = false;
        media->carrier[0] = '\0';
        break;
    case RTCP_PHONE_STATUS_POWER:
        if (update->data.power.has_battery_level)
        {
            media->has_battery = true;
            media->battery = update->data.power.battery_level;
        }
        if (update->data.power.has_charging_state)
        {
            media->has_charging = true;
            media->charging = update->data.power.charging_state;
        }
        if (update->data.power.has_external_charger)
        {
            media->has_external_power = true;
            media->external_power = update->data.power.external_charger;
        }
        break;
    case RTCP_PHONE_STATUS_CELLULAR:
        copy_phone_text(media->carrier, sizeof(media->carrier), update->data.cellular.carrier);
        if (update->data.cellular.signal_strength)
        {
            media->has_signal = update->data.cellular.signal_strength_count > 0;
            if (media->has_signal)
                media->signal = update->data.cellular.signal_strength[0];
        }
        if (update->data.cellular.has_airplane_mode)
        {
            media->has_airplane = true;
            media->airplane = update->data.cellular.airplane_mode;
        }
        if (update->data.cellular.has_cellular_supported)
        {
            media->has_cellular = true;
            media->cellular = update->data.cellular.cellular_supported;
        }
        break;
    case RTCP_PHONE_STATUS_CALL:
        if (update->data.call.has_status)
        {
            media->has_call = true;
            media->call = update->data.call.status;
        }
        break;
    }
    phone_metadata_leave();
}

static void on_classic_route(const rtcp_route_details_t *update, void *context)
{
    (void)context;
    if (atomic_load(&s_classic_diagnostics))
        APP_LOG("classic route clear=%u order=%u lanes=%u",
                (unsigned)update->invalidated, (unsigned)update->maneuver_index_count, (unsigned)update->lane_count);
    if (!phone_metadata_enter())
        return;
    carplay_phone_media_t *media = &s_app.phone_media;
    if (update->invalidated)
    {
        media->route_road[0] = media->route_destination[0] = media->route_remaining[0] = media->route_turn_distance[0] = '\0';
        memset(media->maneuvers, 0, sizeof(media->maneuvers));
        memset(s_app.maneuver_cache, 0, sizeof(s_app.maneuver_cache));
        s_app.maneuver_slot = 0;
        media->lane_count = 0;
        s_app.has_lane_index = s_app.has_lane_payload = s_app.lanes_hidden = false;
    }
    else
    {
        copy_phone_text(media->route_road, sizeof(media->route_road), update->guidance.road);
        copy_phone_text(media->route_destination, sizeof(media->route_destination), update->guidance.destination);
        copy_phone_text(media->route_remaining, sizeof(media->route_remaining), update->guidance.distance_remaining);
        copy_phone_text(media->route_turn_distance, sizeof(media->route_turn_distance), update->guidance.distance_to_maneuver);
        if (update->maneuver_indices)
        {
            memset(media->maneuvers, 0, sizeof(media->maneuvers));
            for (size_t row = 0; row < CARPLAY_PHONE_MANEUVER_ROWS && row < update->maneuver_index_count; ++row)
            {
                media->maneuvers[row].present = true;
                media->maneuvers[row].index = update->maneuver_indices[row];
            }
        }
        if (update->has_lane_index)
        {
            s_app.has_lane_index = true;
            s_app.lane_index = update->lane_index;
        }
        if (update->has_lanes_shown)
        {
            s_app.lanes_hidden = !update->lanes_shown;
            if (s_app.lanes_hidden)
            {
                media->lane_count = 0;
                s_app.has_lane_payload = false;
            }
        }
    }
    phone_metadata_leave();
}

static void on_classic_maneuver(const rtcp_route_maneuver_t *update, void *context)
{
    (void)context;
    if (atomic_load(&s_classic_diagnostics))
        APP_LOG("classic maneuver index=%u", (unsigned)update->index);
    if (!phone_metadata_enter())
        return;
    const unsigned capacity = sizeof(s_app.maneuver_cache) / sizeof(s_app.maneuver_cache[0]);
    unsigned slot = 0;
    while (slot < capacity && (!s_app.maneuver_cache[slot].present || s_app.maneuver_cache[slot].index != update->index))
        ++slot;
    if (slot == capacity)
    {
        slot = s_app.maneuver_slot++ % capacity;
        s_app.maneuver_cache[slot] = (carplay_phone_maneuver_t){ .present = true, .index = update->index };
    }
    carplay_phone_maneuver_t *maneuver = &s_app.maneuver_cache[slot];
    copy_phone_text(maneuver->instruction, sizeof(maneuver->instruction),
                    update->description ? update->description : update->road_after);
    copy_phone_text(maneuver->distance, sizeof(maneuver->distance), update->distance);
    phone_metadata_leave();
}

static void on_classic_lanes(const rtcp_lane_guidance_t *update, void *context)
{
    (void)context;
    if (atomic_load(&s_classic_diagnostics))
        APP_LOG("classic lanes index=%u count=%u", (unsigned)update->index, (unsigned)update->lane_count);
    if (!update->lanes || !phone_metadata_enter())
        return;
    s_app.has_lane_payload = true;
    s_app.lane_payload_index = update->index;
    carplay_phone_media_t *media = &s_app.phone_media;
    const size_t capacity = sizeof(media->lanes) / sizeof(media->lanes[0]);
    media->lane_count = update->lane_count < capacity ? update->lane_count : capacity;
    for (size_t lane = 0; lane < media->lane_count; ++lane)
    {
        const rtcp_lane_t *source = &update->lanes[lane];
        media->lanes[lane].highlighted = source->has_highlight_angle;
        media->lanes[lane].angle = source->has_highlight_angle ? source->highlight_angle : source->angle_count ? source->angles[0]
                                                                                                               : 0;
    }
    phone_metadata_leave();
}

void carplay_app_configure_classic_metadata(iap2_session_config_t *config)
{
    config->on_phone_status = on_classic_phone;
    config->on_now_playing_details = on_iap2_playback;
    config->on_route_details = on_classic_route;
    config->on_route_maneuver = on_classic_maneuver;
    config->on_lane_guidance = on_classic_lanes;
}
#endif

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static bool on_host_ui(void *context)
{
    app_state_t *app = context;
    if (rt_mutex_take(app->lock, RT_WAITING_FOREVER) != RT_EOK)
        return false;
    app->projection_pending = 0U;
    app->projection_pause_waiting = false;
    (void)rt_mutex_release(app->lock);
    carplay_fusion_snapshot_t display;
    carplay_fusion_snapshot(0, &display);
    app->projection_retained_stream = display.effective != CARPLAY_LOCAL
                                          ? rtcp_receiver_get_projection_stream_generation(app->receiver)
                                          : 0U;
    app->projection_resume_waiting = false;
    app->projection_resume_confirmed = false;
    carplay_fusion_set_projection_hidden(true);
    APP_LOG("phone requested host UI");
    return true;
}
#endif

static void on_event_state(bool connected, void *context)
{
    app_state_t *app = context;
    if (s_receiver_shutting_down)
        return;
    app->airplay_connected = connected;
    app->phone_appearance_sent = -1;
    if (!connected)
    {
        app->projection_pause_waiting = false;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        app->projection_retained_stream = 0U;
        app->projection_resume_waiting = false;
        app->projection_resume_confirmed = false;
        carplay_fusion_video_stopped();
        carplay_fusion_set_projection_hidden(false);
#endif
    }
    if (!connected && rt_mutex_take(app->lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        app->phone_action_pending = 0;
        app->siri_pending = false;
        app->projection_pending = 0;
        clear_phone_media(app);
        (void)rt_mutex_release(app->lock);
    }
    APP_LOG("event channel %s", connected ? "connected" : "closed");
    if (!connected && app->bridge != NULL)
    {
        /* The controller is gone; drop any held contacts so a reconnect does
         * not inherit a stuck finger. */
        input_bridge_release_all(app->bridge);
    }
}

static bool send_hid_report(uint32_t hid_uid, const uint8_t *report,
                            size_t report_len, void *context)
{
    app_state_t *app = context;
    bool sent = app->receiver != NULL &&
                rtcp_receiver_send_hid_report(app->receiver, hid_uid, report,
                                              report_len);
    if (sent)
        ++app->hid_reports_sent;
    else
        ++app->hid_reports_failed;
    return sent;
}

#if defined(RTCARPLAY_TOUCH_PANEL) && !defined(RTCARPLAY_DEMO_LAUNCHER)
/* Panel reader thread -> touch queue; the poll thread turns the queue into
 * HID reports, so the panel thread never touches the event channel. */
static void on_panel_touch(uint8_t phase, uint16_t pointer_id, uint16_t x,
                           uint16_t y, void *context)
{
    (void)context;
    (void)carplay_app_touch(phase, pointer_id, x, y);
}
#endif

/* ------------------------------------------------------------------ */
/* Touch queue: any thread -> poll thread                              */
/* ------------------------------------------------------------------ */

static int enqueue_touch_owned(const touch_event_t *event)
{
    app_state_t *app = &s_app;
    if (!s_receiver_running || app->lock == NULL)
        return RTCP_ESTATE;
    if (rt_mutex_take(app->lock, RT_WAITING_NO) != RT_EOK)
        return RTCP_ESTATE;
    unsigned next = (app->touch_head + 1U) & TOUCH_QUEUE_MASK;
    if (next == app->touch_tail)
    {
        ++app->touch_queue_full;
        (void)rt_mutex_release(app->lock);
        return RTCP_ENOMEM;
    }
    ++app->touch_queued;
    app->touch[app->touch_head] = *event;
    app->touch_head = next;
    (void)rt_mutex_release(app->lock);
    return RTCP_OK;
}

static int enqueue_touch(const touch_event_t *event)
{
    /* Stop holds this gate until the poll worker has destroyed the queue
     * mutex. Never even read that mutex while start/stop owns the state. */
    if (atomic_flag_test_and_set_explicit(&s_lifecycle_call,
                                          memory_order_acquire))
        return RTCP_ESTATE;
    int result = enqueue_touch_owned(event);
    atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
    return result;
}

static void drain_touch(app_state_t *app)
{
    touch_event_t batch[TOUCH_BATCH];
    bool pending;
    do
    {
        size_t count = 0U;
        pending = false;
        if (rt_mutex_take(app->lock, RT_WAITING_NO) != RT_EOK)
            return;
        while (count < TOUCH_BATCH && app->touch_tail != app->touch_head)
        {
            batch[count++] = app->touch[app->touch_tail];
            app->touch_tail = (app->touch_tail + 1U) & TOUCH_QUEUE_MASK;
        }
        pending = app->touch_tail != app->touch_head;
        (void)rt_mutex_release(app->lock);

        for (size_t i = 0U; i < count; ++i)
        {
            input_bridge_phase_t phase;
            switch (batch[i].phase)
            {
            case RTCP_TOUCH_DOWN:
                phase = INPUT_BRIDGE_TOUCH_DOWN;
                break;
            case RTCP_TOUCH_MOVE:
                phase = INPUT_BRIDGE_TOUCH_MOVE;
                break;
            case RTCP_TOUCH_UP:
                phase = INPUT_BRIDGE_TOUCH_UP;
                break;
            default:
                continue;
            }
            double x = (double)batch[i].x / (double)app->width;
            double y = (double)batch[i].y / (double)app->height;
            if (x > 1.0)
                x = 1.0;
            if (y > 1.0)
                y = 1.0;
            (void)input_bridge_touch(app->bridge, phase, batch[i].pointer_id,
                                     x, y, app->os.now_ms());
        }
    } while (pending);
}

/* ------------------------------------------------------------------ */
/* Thread and teardown                                                 */
/* ------------------------------------------------------------------ */

static void shutdown_stack(app_state_t *app)
{
    s_receiver_shutting_down = true;
#if defined(RTCARPLAY_TOUCH_PANEL)
    if (app->touch_panel_started)
    {
        /* Keep the callback/context alive until the reader is quiescent,
         * including after a bounded device-read stop timeout. */
        while (carplay_touch_panel_stop() != RTCP_OK) rt_thread_mdelay(10);
        app->touch_panel_started = false;
    }
#endif
#if defined(RTCARPLAY_VIDEO)
    if (app->video_started)
    {
        while (carplay_video_sink_stop() != RTCP_OK) rt_thread_mdelay(10);
        app->video_started = false;
    }
#endif
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_fusion_set_projection_hidden(false);
#endif
    if (app->bridge != NULL)
    {
        input_bridge_release_all(app->bridge);
        input_bridge_destroy(app->bridge);
        app->bridge = NULL;
    }
    if (app->mdns != NULL)
    {
        mdns_responder_close(app->mdns, &rtcp_net_rtthread);
        mdns_responder_destroy(app->mdns);
        app->mdns = NULL;
    }
    if (app->browser != NULL)
    {
        mdns_browser_destroy(app->browser);
        app->browser = NULL;
    }
    rtcp_net_rtthread_multicast_interface(NULL);
    if (app->receiver != NULL)
    {
        (void)rtcp_receiver_destroy(app->receiver);
        app->receiver = NULL;
    }
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
    /* Receiver borrows the model until destruction. */
    carplay_vehicle_protocol_demo_destroy(&app->vehicle_demo);
#endif
#if defined(RTCARPLAY_AUDIO)
    /* After the server: no PCM callback can run once it is stopped. */
    if (app->audio_started)
    {
#if defined(RTCARPLAY_MICROPHONE)
        while (carplay_audio_input_stop() != RTCP_OK) rt_thread_mdelay(10);
#endif
        while (carplay_audio_sink_stop() != RTCP_OK) rt_thread_mdelay(10);
        app->audio_started = false;
    }
#endif
    if (app->store_initialized)
    {
        airplay_pairing_store_deinit();
        app->store_initialized = false;
    }
    airplay_ed25519_keypair_clear(&app->keypair);
    if (app->ap_started)
    {
#if defined(LPKG_RT_CARPLAY_LUBANLITE_WIFI)
        (void)rtcp_wifi_lubanlite.ap_stop();
#endif
        app->ap_started = false;
    }
    if (app->lock != NULL)
    {
        (void)rt_mutex_delete(app->lock);
        app->lock = NULL;
    }
    /* The worker still owns this session. Reset only at the next start,
     * after its completion has been published by the last worker access. */
}

static bool bytes_contain(const uint8_t *bytes, size_t length,
                          const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0U || length < needle_len)
        return false;
    for (size_t i = 0U; i + needle_len <= length; ++i)
    {
        if (memcmp(bytes + i, needle, needle_len) == 0)
            return true;
    }
    return false;
}

/* Every packet received on the shared mDNS socket also feeds the browser. */
static void mdns_packet_sink(const uint8_t *packet, size_t packet_len,
                             const rtcp_addr_t *from, void *context)
{
    app_state_t *app = context;
    if (app->browser == NULL || !app->ctrl_enabled)
        return;
    const uint8_t *ipv4 = NULL;
    if (from != NULL && from->family == RTCP_AF_INET)
        ipv4 = from->addr;
    bool control = bytes_contain(packet, packet_len, "_carplay-ctrl");
    bool airplay = bytes_contain(packet, packet_len, "_airplay");
    bool response =
        packet_len >= 3U && (packet[2] & 0x80U) != 0U; /* DNS QR bit */
    int updates = mdns_browser_handle_packet(app->browser, packet, packet_len,
                                             ipv4,
                                             (uint32_t)app->os.now_ms());
    ++app->browse_packets;
    if (updates > 0)
        app->browse_updates += (unsigned)updates;
    if (app->browse_packets <= 10U || (app->browse_packets % 64U) == 0U ||
        (response && control))
    {
        APP_LOG("mdns_rx #%u from=%u.%u.%u.%u bytes=%u response=%u ctrl=%u "
                "airplay=%u updates=%d",
                app->browse_packets, ipv4 != NULL ? ipv4[0] : 0U,
                ipv4 != NULL ? ipv4[1] : 0U, ipv4 != NULL ? ipv4[2] : 0U,
                ipv4 != NULL ? ipv4[3] : 0U, (unsigned)packet_len,
                response ? 1U : 0U, control ? 1U : 0U, airplay ? 1U : 0U,
                updates);
    }
}

/*
 * Browses _carplay-ctrl._tcp and performs the reverse connect-out.  Runs in
 * the poll thread; carplay_ctrl_connect() bounds the handshake and the status
 * read at CARPLAY_CTRL_TIMEOUT_MS each, so a silent endpoint cannot stall
 * mDNS/RTSP for long.  Once the phone's event channel is up the nudge is no
 * longer needed and is suppressed for the rest of the session.
 */
static void carplay_ctrl_tick(app_state_t *app, uint32_t now_ms)
{
    if (app->browser == NULL || !app->ctrl_enabled)
        return;

    /* Bootstrap from a query for our AirPlay records, before RTSP exists.
     * This SDK's DHCP AP is /24. A candidate is only queried for DNS-SD;
     * connect-out still requires the browser to resolve a Control SRV. */
    rtcp_addr_t peer;
    if (mdns_responder_take_query_peer(app->mdns, &peer) &&
        memcmp(peer.addr, app->ipv4, 3U) == 0 &&
        peer.addr[3] != 0U && peer.addr[3] != 255U)
    {
        if (!app->ctrl_peer_valid || memcmp(app->ctrl_peer_ipv4, peer.addr, 4U))
            APP_LOG("carplay_ctrl=peer source=mdns_query ip=%u.%u.%u.%u",
                    peer.addr[0], peer.addr[1], peer.addr[2], peer.addr[3]);
        memcpy(app->ctrl_peer_ipv4, peer.addr, 4U);
        app->ctrl_peer_valid = true;
    }
    if (rtcp_receiver_get_peer(app->receiver, &peer) == RTCP_OK && peer.family == RTCP_AF_INET)
    {
        memcpy(app->ctrl_peer_ipv4, peer.addr, 4U);
        app->ctrl_peer_valid = true;
    }

    if (mdns_browser_query_due(app->browser, now_ms))
    {
        uint8_t query[512];
        size_t query_len = mdns_browser_build_query(app->browser, query,
                                                    sizeof(query));
        if (query_len != 0U)
        {
            ++app->browse_queries;
            int multicast_rc = mdns_responder_send(
                app->mdns, &rtcp_net_rtthread, query, query_len);
            int unicast_rc = RTCP_ESTATE;
            if (app->ctrl_peer_valid)
            {
                rtcp_addr_t destination;
                memset(&destination, 0, sizeof(destination));
                destination.family = RTCP_AF_INET;
                memcpy(destination.addr, app->ctrl_peer_ipv4, 4U);
                destination.port = MDNS_PORT;
                unicast_rc = mdns_responder_send_to(
                    app->mdns, &rtcp_net_rtthread, &destination, query,
                    query_len);
            }
            if (multicast_rc < 0)
                ++app->browse_tx_errors;
            if (unicast_rc < 0 && app->ctrl_peer_valid)
                ++app->browse_tx_errors;
            if (app->browse_queries <= 3U ||
                (app->browse_queries % 15U) == 0U)
            {
                if (app->ctrl_peer_valid)
                {
                    APP_LOG("carplay_ctrl=query #%u multicast_rc=%d "
                            "unicast_rc=%d peer=%u.%u.%u.%u",
                            app->browse_queries, multicast_rc, unicast_rc,
                            app->ctrl_peer_ipv4[0], app->ctrl_peer_ipv4[1],
                            app->ctrl_peer_ipv4[2], app->ctrl_peer_ipv4[3]);
                }
                else
                {
                    APP_LOG("carplay_ctrl=query #%u multicast_rc=%d "
                            "unicast=none",
                            app->browse_queries, multicast_rc);
                }
            }
        }
        mdns_browser_mark_queried(app->browser, now_ms);
    }

    if (app->airplay_connected)
        return;
    const mdns_browser_endpoint_t *endpoint =
        mdns_browser_best(app->browser, now_ms);
    if (endpoint == NULL)
        return;
    if (strcmp(app->ctrl_last_endpoint, endpoint->instance) != 0)
    {
        strncpy(app->ctrl_last_endpoint, endpoint->instance,
                sizeof(app->ctrl_last_endpoint) - 1U);
        APP_LOG("carplay_ctrl=endpoint ip=%u.%u.%u.%u port=%u instance=%s",
                endpoint->ipv4[0], endpoint->ipv4[1], endpoint->ipv4[2],
                endpoint->ipv4[3], (unsigned)endpoint->port,
                endpoint->instance);
    }
    if (app->ctrl_last_attempt_ms != 0U &&
        (uint32_t)(now_ms - app->ctrl_last_attempt_ms) <
            CARPLAY_CTRL_RETRY_MS)
    {
        return;
    }
    app->ctrl_last_attempt_ms = now_ms;
    ++app->ctrl_attempts;
    APP_LOG("carplay_ctrl=connect_out ip=%u.%u.%u.%u port=%u attempt=%u",
            endpoint->ipv4[0], endpoint->ipv4[1], endpoint->ipv4[2],
            endpoint->ipv4[3], (unsigned)endpoint->port, app->ctrl_attempts);
    int status = 0;
    int result = carplay_ctrl_connect(
        &rtcp_net_rtthread, endpoint->ipv4, endpoint->port, app->ctrl_device_id,
        CARPLAY_CTRL_USER_AGENT, CARPLAY_CTRL_TIMEOUT_MS, &status);
    uint32_t elapsed = (uint32_t)((uint32_t)app->os.now_ms() - now_ms);
    if (result == RTCP_OK)
    {
        ++app->ctrl_responses;
        app->ctrl_last_status = status;
        APP_LOG("carplay_ctrl=connected status=%d elapsed_ms=%u responses=%u",
                status, (unsigned)elapsed, app->ctrl_responses);
    }
    else
    {
        APP_LOG("carplay_ctrl=connect_failed rc=%d elapsed_ms=%u", result,
                (unsigned)elapsed);
    }
}

/* Browser queries and endpoint selection stay on the poll owner. A busy
 * reader delays publication until the next tick; network I/O never holds lock. */
static void publish_control_status(app_state_t *app, uint32_t now_ms)
{
    control_status_t status = { 0 };
    status.available = app->browser != NULL;
    status.enabled = app->ctrl_enabled;
    status.device_id = app->ctrl_device_id;
    status.endpoints = (unsigned)mdns_browser_endpoint_count(app->browser);
    status.attempts = app->ctrl_attempts;
    status.responses = app->ctrl_responses;
    status.last_status = app->ctrl_last_status;
    status.queries = app->browse_queries;
    status.tx_errors = app->browse_tx_errors;
    status.packets = app->browse_packets;
    status.updates = app->browse_updates;
    status.peer_valid = app->ctrl_peer_valid;
    memcpy(status.peer_ipv4, app->ctrl_peer_ipv4, sizeof(status.peer_ipv4));
    const mdns_browser_endpoint_t *endpoint = app->browser != NULL
                                                  ? mdns_browser_best(app->browser, now_ms)
                                                  : NULL;
    if (endpoint != NULL)
    {
        status.endpoint = *endpoint;
        status.has_endpoint = true;
    }
    if (rt_mutex_take(app->lock, RT_WAITING_NO) == RT_EOK)
    {
        app->ctrl_status = status;
        (void)rt_mutex_release(app->lock);
    }
}

/* Only the poll owner touches the Receiver. The shell publishes requests
 * while holding the lifecycle gate and this mutex; no server pointer escapes. */
static void drain_receiver_commands(app_state_t *app)
{
    bool siri = false;
    unsigned projection = 0;
    carplay_phone_action_t phone_action = 0;
    if (rt_mutex_take(app->lock, RT_WAITING_NO) == RT_EOK)
    {
        if (app->ctrl_enable_pending)
        {
            app->ctrl_enabled = app->browser != NULL && app->ctrl_requested_enabled;
            app->ctrl_enable_pending = false;
        }
        if (app->ctrl_connect_pending)
        {
            app->ctrl_last_attempt_ms = (uint32_t)app->os.now_ms() - CARPLAY_CTRL_RETRY_MS;
            app->ctrl_connect_pending = false;
        }
        siri = app->siri_pending;
        app->siri_pending = false;
        projection = app->projection_pending;
        app->projection_pending = 0;
        phone_action = app->phone_action_pending;
        app->phone_action_pending = 0;
        if (app->cipher_pending &&
            rtcp_receiver_set_single_cipher_frame(app->receiver,
                                                  app->cipher_requested) == RTCP_OK)
        {
            app->cipher_applied = app->cipher_requested;
            app->cipher_pending = false;
        }
        (void)rt_mutex_release(app->lock);
    }
    if (siri && !rtcp_receiver_request_siri(app->receiver))
        APP_LOG("Siri request unavailable: event channel not ready");

    if (!app->airplay_connected)
        return;
    if (projection)
    {
        bool resume = projection == 2U;
        int result = rtcp_receiver_set_projection_enabled(app->receiver, resume);
        if (result == RTCP_OK)
        {
            app->projection_pause_waiting = !resume;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
            if (!resume)
                app->projection_retained_stream = 0U;
            app->projection_resume_waiting = resume && app->projection_retained_stream != 0U;
            app->projection_resume_confirmed = false;
            if (resume)
                carplay_fusion_resume_projection_on_frame();
#endif
        }
        APP_LOG("projection %s command rc=%d", resume ? "resume" : "pause", result);
    }
    rtcp_appearance_setting_t appearance = carplay_app_phone_appearance();
    if ((int)appearance != app->phone_appearance_sent)
    {
        int result = rtcp_receiver_set_appearance(app->receiver, appearance, false, false);
        app->phone_appearance_sent = (int)appearance;
        if (result != RTCP_OK)
            APP_LOG("phone appearance command failed rc=%d", result);
    }
    if (phone_action == CARPLAY_PHONE_BACK || phone_action == CARPLAY_PHONE_HOME)
    {
        const rtcp_knob_report_t press = { .home = phone_action == CARPLAY_PHONE_HOME,
                                           .back = phone_action == CARPLAY_PHONE_BACK };
        const rtcp_knob_report_t release = { 0 };
        if (rtcp_receiver_send_knob(app->receiver, &press))
            (void)rtcp_receiver_send_knob(app->receiver, &release);
    }
    else if (phone_action)
    {
        rtcp_media_button_t button = RTCP_MEDIA_PLAY_PAUSE;
        if (phone_action == CARPLAY_PHONE_PREVIOUS)
            button = RTCP_MEDIA_PREVIOUS;
        else if (phone_action == CARPLAY_PHONE_NEXT)
            button = RTCP_MEDIA_NEXT;
        if (rtcp_receiver_send_media_button(app->receiver, button))
            (void)rtcp_receiver_send_media_button(app->receiver, RTCP_MEDIA_RELEASE);
    }
}

static void poll_thread(void *argument)
{
    app_state_t *app = argument;
    s_receiver_worker_live = true;
    APP_LOG("receiver running (PIN %s)", app->pairing_code);

    while (!s_receiver_stop)
    {
        if (app->mdns != NULL)
        {
            int mdns_result = mdns_responder_poll_ex(
                app->mdns, &rtcp_net_rtthread, 0, mdns_packet_sink, app);
            if (mdns_result < 0 && app->mdns_errors < 3U)
            {
                ++app->mdns_errors;
                APP_LOG("mDNS poll failed rc=%d", mdns_result);
            }
        }
        drain_receiver_commands(app);
        carplay_ctrl_tick(app, (uint32_t)app->os.now_ms());
        publish_control_status(app, (uint32_t)app->os.now_ms());
        /* In wired mode wait for socket readiness, not a fixed sleep before
         * each recv. Screen headers and bodies need separate readable polls;
         * pacing every fragment by 10 ms caps throughput and builds latency. */
        /* Wait on session sockets, never on an unrelated mDNS socket. */
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
        int vehicle_error = carplay_vehicle_protocol_demo_tick(&app->vehicle_demo, app->os.now_ms());
#endif
        (void)rtcp_receiver_poll(app->receiver, 10);
#if defined(RTCARPLAY_VIDEO)
        if (app->on_screen == carplay_video_sink_frame)
        {
            if (!carplay_video_sink_needs_keyframe())
                app->screen_keyframe_retry_ms = 0;
            else
            {
                uint64_t now = app->os.now_ms();
                if (now >= app->screen_keyframe_retry_ms)
                {
                    app->screen_keyframe_retry_ms = now + 500U;
                    (void)rtcp_receiver_request_screen_keyframe(app->receiver);
                }
            }
        }
#endif
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        if (app->projection_resume_confirmed)
        {
            app->projection_resume_waiting = false;
            app->projection_resume_confirmed = false;
            uint32_t stream = rtcp_receiver_get_projection_stream_generation(app->receiver);
            if (stream && stream == app->projection_retained_stream &&
                carplay_fusion_resume_latched_projection())
                APP_LOG("projection resumed from the retained frame");
        }
#endif
#ifdef RT_USING_RTC
        apply_phone_clock();
#endif
        /* Snapshot on the owner thread; the shell never dereferences a
         * screen server that the protocol loop may tear down. */
        uint32_t sample_ms = (uint32_t)app->os.now_ms();
#if defined(RTCARPLAY_AUDIO)
        if ((uint32_t)(sample_ms - app->screen_sample_ms) >= 100U && app->audio_started)
            publish_phone_volume(app);
#endif
        if ((uint32_t)(sample_ms - app->screen_sample_ms) >= 100U &&
            rt_mutex_take(app->lock, RT_WAITING_NO) == RT_EOK)
        {
            rtcp_receiver_diagnostics_t diagnostics;
            (void)rtcp_receiver_get_diagnostics(app->receiver, &diagnostics);
            bool previously_connected = app->receiver_state.transport_connected;
            (void)rtcp_receiver_get_state(app->receiver, &app->receiver_state);
            app->projection_owner = rtcp_receiver_get_projection_owner(app->receiver);
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
            if (app->projection_pause_waiting && app->projection_owner == RTCP_PROJECTION_HOST)
            {
                carplay_fusion_set_projection_hidden(true);
                app->projection_pause_waiting = false;
            }
#endif
            if (previously_connected && !app->receiver_state.transport_connected)
                clear_phone_media(app);
            app->screen_stats = diagnostics.screen;
            app->audio_stats = diagnostics.audio;
            app->timing_stats = diagnostics.timing;
            app->screen_sample_ms = sample_ms;
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
            carplay_vehicle_protocol_demo_snapshot(&app->vehicle_demo, app->receiver, &app->vehicle_snapshot);
            app->vehicle_snapshot.sample_error = vehicle_error;
#endif
            (void)rt_mutex_release(app->lock);
        }
        drain_touch(app);
        input_bridge_tick(app->bridge, app->os.now_ms());
    }

    APP_LOG("stopping (%u screen frames)", app->screen_frames);
    shutdown_stack(app);
    app->thread = NULL;
    s_receiver_worker_live = false;
    s_receiver_running = false;
}

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

void carplay_app_config_defaults(carplay_app_config_t *config)
{
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->name = DEFAULT_NAME;
    config->model = DEFAULT_MODEL;
    config->ssid = DEFAULT_SSID;
    config->passphrase = DEFAULT_PASSPHRASE;
    config->channel = DEFAULT_CHANNEL;
    config->seed_path = DEFAULT_SEED_PATH;
    config->store_path = DEFAULT_STORE_PATH;
    config->rotation_degrees = 0U;
}

static bool make_pairing_code(app_state_t *app, const char *requested)
{
    if (requested == NULL || requested[0] == '\0')
    {
        requested = DEFAULT_PAIR_CODE;
    }
    size_t length = strlen(requested);
    if (length > PAIR_CODE_DIGITS)
        return false;
    memcpy(app->pairing_code, requested, length + 1U);
    return true;
}

#if defined(LPKG_RT_CARPLAY_LUBANLITE_WIFI)
static bool format_device_id_from_mac(app_state_t *app)
{
    uint8_t mac[6];
    if (rtcp_wifi_lubanlite_get_mac(mac) != RTCP_OK)
        return false;
    memcpy(app->wifi_mac, mac, sizeof(mac));
    app->wifi_mac_valid = true;
    rt_snprintf(app->device_id, sizeof(app->device_id),
                "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);
    rt_snprintf(app->host_name, sizeof(app->host_name), "rtcp-%02x%02x%02x",
                mac[3], mac[4], mac[5]);
    return true;
}
#endif /* LPKG_RT_CARPLAY_LUBANLITE_WIFI */

static bool format_device_id_from_seed(app_state_t *app,
                                       const uint8_t seed[32])
{
    /* Fallback identity: a locally administered address derived from the
     * persisted seed, so it stays stable across boots. */
    rt_snprintf(app->device_id, sizeof(app->device_id),
                "02:%02X:%02X:%02X:%02X:%02X", seed[0], seed[1], seed[2],
                seed[3], seed[4]);
    rt_snprintf(app->host_name, sizeof(app->host_name), "rtcp-%02x%02x%02x",
                seed[2], seed[3], seed[4]);
    return true;
}

static int build_identity(app_state_t *app, const char *pair_code,
                          const char *seed_path)
{
#if defined(RTCARPLAY_NATIVE_NCM)
    uint8_t wired_mac[6];
    if (app->wired && !rtcp_lubanlite_wired_mac(wired_mac))
    {
        APP_LOG("wired identity: selected NCM MAC is unavailable");
        return RTCP_ENOENT;
    }
#endif
    uint8_t seed[AIRPLAY_ED25519_SEED_BYTES];
    bool loaded = load_seed(seed_path, seed);
    if (!loaded)
    {
        if (app->os.random == NULL ||
            !app->os.random(seed, sizeof(seed)))
        {
            APP_LOG("no entropy source for the Ed25519 identity");
            return RTCP_ENOTSUP;
        }
        if (!write_seed(seed_path, seed))
        {
            APP_LOG("cannot write identity seed %s", seed_path);
            memset(seed, 0, sizeof(seed));
            return RTCP_EIO;
        }
        APP_LOG("generated a new Ed25519 identity in %s", seed_path);
    }
    bool ok = airplay_ed25519_keypair_from_seed(seed, &app->keypair);
    /* The Realtek MAC is only readable after the WLAN device is registered
     * and the AP is up, which happens in start_wifi().  Use the stable
     * seed-derived identity until then and refine it once the AP is
     * active; querying the driver earlier leaves its wext path on a DOWN
     * netif and is not needed. */
    if (ok)
        ok = format_device_id_from_seed(app, seed);
    memset(seed, 0, sizeof(seed));
    if (!ok)
        return RTCP_EIO;

#if defined(RTCARPLAY_NATIVE_NCM)
    if (app->wired)
    {
        /* Bind identity before /info, pairing and the receiver are built.
         * Do not patch only the later iAP2 invitation or mutate a live
         * receiver. The native function's descriptor MAC is available
         * before carrier/DAD, avoiding a dependency on phone activation. */
        rt_snprintf(app->device_id, sizeof(app->device_id),
                    "%02X:%02X:%02X:%02X:%02X:%02X", wired_mac[0], wired_mac[1],
                    wired_mac[2], wired_mac[3], wired_mac[4], wired_mac[5]);
        rt_snprintf(app->host_name, sizeof(app->host_name), "rtcp-%02x%02x%02x",
                    wired_mac[3], wired_mac[4], wired_mac[5]);
    }
#endif

    app->accessory_identifier_len = strlen(app->device_id);
    memcpy(app->accessory_identifier, app->device_id,
           app->accessory_identifier_len);
    return make_pairing_code(app, pair_code) ? RTCP_OK : RTCP_EIO;
}

#if defined(LPKG_RT_CARPLAY_LUBANLITE_WIFI)
static int wait_for_ipv4(app_state_t *app)
{
    for (unsigned attempt = 0U; attempt < IP_WAIT_TRIES; ++attempt)
    {
        if (rtcp_wifi_lubanlite_get_ipv4(app->ipv4) == RTCP_OK)
            return RTCP_OK;
        rt_thread_mdelay(IP_WAIT_STEP_MS);
    }
    return RTCP_ETIMEDOUT;
}

static int start_wifi(app_state_t *app)
{
    int result = rtcp_wifi_lubanlite.ap_start(app->ssid, app->passphrase,
                                              app->channel);
    if (result != RTCP_OK)
    {
        APP_LOG("SoftAP start failed rc=%d", result);
        return result;
    }
    app->ap_started = true;

#if !defined(RT_WLAN_PROT_LWIP_ENABLE) || !defined(LWIP_USING_DHCPD)
    result = RTCP_ESTATE;
    for (unsigned attempt = 0; attempt < IP_WAIT_TRIES && result == RTCP_ESTATE; ++attempt)
    {
        result = rtcp_wifi_lubanlite_start_dhcpd();
        if (result == RTCP_ESTATE)
            rt_thread_mdelay(IP_WAIT_STEP_MS);
    }
    if (result != RTCP_OK)
    {
        APP_LOG("DHCP server unavailable rc=%d", result);
        return result;
    }
#endif

    size_t ie_len = 0U;
    const uint8_t *ie = airplay_vendor_ie(&ie_len);
    result = rtcp_wifi_lubanlite.set_vendor_ie(ie, ie_len);
    if (result != RTCP_OK)
    {
        /* Without the IE the iPhone may not offer CarPlay automatically, but
         * the AirPlay service itself is still discoverable over mDNS. */
        APP_LOG("Apple vendor IE not applied rc=%d", result);
    }

    result = wait_for_ipv4(app);
    if (result != RTCP_OK)
    {
        APP_LOG("AP netif did not get an IPv4 address");
        return result;
    }
    APP_LOG("SoftAP %s ip=%u.%u.%u.%u", app->ssid, app->ipv4[0], app->ipv4[1],
            app->ipv4[2], app->ipv4[3]);

    /* The AP is active now, so the driver can report its MAC.  Prefer it
     * over the seed-derived fallback chosen in build_identity(). */
    if (format_device_id_from_mac(app))
    {
        app->accessory_identifier_len = strlen(app->device_id);
        memcpy(app->accessory_identifier, app->device_id,
               app->accessory_identifier_len);
    }
    else
    {
        APP_LOG("cannot read the Wi-Fi MAC; keeping the seed-derived device id");
    }

    return RTCP_OK;
}
#endif /* LPKG_RT_CARPLAY_LUBANLITE_WIFI */

static uint64_t capability_features(uint64_t offered, bool legacy)
{
    if (legacy)
        return offered;
    /* catplay/LIVI's explicit CarPlay capabilities, restricted to our offer.
     * Do not add Rotate (bit 8): this receiver uses a fixed display.
     * Omit inherited Photo/Slideshow/PhotoCaching, metadata, iTunes encryption,
     * undocumented bits and redundant-audio discovery claims. Audio formats,
     * microphone, pairing identities and initial modes are independent. */
    const uint64_t supported = (UINT64_C(1) << 7)     /* Screen */
                               | (UINT64_C(1) << 9)   /* Audio */
                               | (UINT64_C(1) << 18)  /* PCM */
                               | (UINT64_C(1) << 20)  /* AAC-LC, only if offered */
                               | (UINT64_C(1) << 22)  /* unencrypted audio */
                               | (UINT64_C(1) << 26)  /* MFi-SAP audio */
                               | (UINT64_C(1) << 30)  /* unified Bonjour */
                               | (UINT64_C(1) << 32)  /* Car */
                               | (UINT64_C(1) << 37)  /* CarPlay Control */
                               | (UINT64_C(1) << 38); /* encrypted pairing */
    return offered & supported;
}

/* Compare the encoded buffers, not only the configuration used to build them. */
static bool announcement_features_match(const uint8_t *info, size_t info_length,
                                        const uint8_t *txt, size_t txt_length)
{
    airplay_bplist_doc_t doc;
    airplay_bplist_object_t root, object;
    uint64_t features;
    char words[32];
    if (airplay_bplist_open(info, info_length, &doc) != AIRPLAY_BPLIST_OK ||
        airplay_bplist_root(&doc, &root) != AIRPLAY_BPLIST_OK ||
        airplay_bplist_dict_get(&doc, root, "features", &object) != AIRPLAY_BPLIST_OK ||
        airplay_bplist_get_uint(&doc, object, &features) != AIRPLAY_BPLIST_OK ||
        !airplay_txt_format_features(features, words, sizeof(words)))
        return false;
    unsigned matches = 0;
    for (size_t offset = 0; offset < txt_length;)
    {
        size_t length = txt[offset++];
        if (length > txt_length - offset)
            return false;
        if (length >= 9 && memcmp(txt + offset, "features=", 9) == 0)
        {
            if (length != 9 + strlen(words) || memcmp(txt + offset + 9, words, strlen(words)) != 0)
                return false;
            ++matches;
        }
        offset += length;
    }
    return matches == 1;
}

static int build_announcement_data(app_state_t *app)
{
    airplay_info_plist_config_t info;
    memset(&info, 0, sizeof(info));
    info.device_id = app->device_id;
    info.bluetooth_id = app->bluetooth_id;
    info.name = app->name;
    info.model = app->model;
    info.width_pixels = app->width;
    info.height_pixels = app->height;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    /* Physical viewport metadata must shrink with the viewport, too. These
     * configurable defaults preserve the old estimate; measure the panel. */
#ifndef RTCARPLAY_PANEL_WIDTH_MM
#define RTCARPLAY_PANEL_WIDTH_MM 200
#endif
#ifndef RTCARPLAY_PANEL_HEIGHT_MM
#define RTCARPLAY_PANEL_HEIGHT_MM 120
#endif
    info.width_physical_mm = (uint16_t)(((uint32_t)RTCARPLAY_PANEL_WIDTH_MM * app->width + 512U) / 1024U);
    info.height_physical_mm = (uint16_t)(((uint32_t)RTCARPLAY_PANEL_HEIGHT_MM * app->height + 300U) / 600U);
#endif
    info.max_fps = app->max_fps;
    info.oem_icons = app->oem_icons;
    info.oem_icon_count = app->oem_icon_count;
    info.oem_icon_label = app->oem_icon_label;
    const rtcp_appearance_setting_t appearance = carplay_app_phone_appearance();
    const rtcp_display_profile_t display = {
        .struct_size = sizeof(display),
        .ui_night = appearance == RTCP_APPEARANCE_DARK,
        .map_night = appearance == RTCP_APPEARANCE_DARK,
        .appearance_setting = (uint8_t)appearance,
    };
    info.main_display_profile = &display;
#if defined(RTCARPLAY_DEMO_LAUNCHER)
    info.hid_knob = true;
    info.hid_media = true;
#endif
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
    info.vehicle = app->vehicle_demo.model;
#endif
#if defined(RTCARPLAY_AUDIO)
    if (app->audio_started)
    {
        /* The sink can decode the compiled formats. Discovery must publish
         * this same effective capability mask, not its video-only default. */
        info.audio_output = true;
        info.audio_compressed = true;
#if defined(RTCARPLAY_MICROPHONE)
        info.audio_input = carplay_audio_input_available();
#endif
        info.audio_sample_rate = CARPLAY_APP_AUDIO_SAMPLE_RATE;
    }
#endif
    info.features = capability_features(airplay_info_plist_features(&info),
                                        CARPLAY_APP_LEGACY_FEATURES != 0);
    if (!airplay_info_plist_build(&info, app->info_plist,
                                  sizeof(app->info_plist),
                                  &app->info_plist_len) ||
        app->info_plist_len > RTCP_CAPABILITIES_MAX_BYTES)
    {
        APP_LOG("cannot build the /info plist");
        return RTCP_EIO;
    }

    hex_encode(app->keypair.public_key, AIRPLAY_ED25519_PUBLIC_KEY_BYTES,
               app->public_key_hex);
    char feature_words[32];
    if (!airplay_txt_format_features(airplay_info_plist_features(&info), feature_words, sizeof(feature_words)))
        return RTCP_EIO;
    APP_LOG("capabilities features=%s audio_output=%u compressed=%u", feature_words,
            info.audio_output ? 1U : 0U, info.audio_compressed ? 1U : 0U);
    APP_LOG("capabilities_profile=%s", CARPLAY_APP_LEGACY_FEATURES ? "legacy" : "explicit");
    airplay_txt_record_t records[AIRPLAY_TXT_MAX_RECORDS];
    airplay_txt_config_t txt;
    memset(&txt, 0, sizeof(txt));
    txt.features = feature_words;
    txt.device_id = app->device_id;
    txt.model = app->model;
    txt.source_version = AIRPLAY_INFO_DEFAULT_SOURCE_VERSION;
    txt.protocol_version = "1.1";
    txt.pairing_id = app->device_id;
    txt.public_key = app->public_key_hex;
    size_t record_count = airplay_txt_build(&txt, records,
                                            AIRPLAY_TXT_MAX_RECORDS);
    app->txt_len = airplay_txt_encode(records, record_count, app->txt,
                                      sizeof(app->txt));
    if (app->txt_len == 0U)
    {
        APP_LOG("cannot encode the mDNS TXT record");
        return RTCP_EIO;
    }
    if (!announcement_features_match(app->info_plist, app->info_plist_len, app->txt, app->txt_len))
    {
        APP_LOG("capabilities TXT and /info Features mismatch");
        return RTCP_EIO;
    }
    APP_LOG("capabilities_check txt_features=%s info_match=1", feature_words);
    return RTCP_OK;
}

/* The receiver starts before Bluetooth. Refresh on the protocol owner once
 * the controller attaches, when the BR/EDR identity is available. The server
 * copies this result; no active session's borrowed document is modified. */
static bool refresh_info_plist(const uint8_t **bytes, size_t *length, void *context)
{
    app_state_t *app = context;
#if defined(LPKG_RT_CARPLAY_LUBANLITE_BT)
    uint8_t address[6];
    if (!app->wired && rtcp_bt_lubanlite_local_bdaddr(address) == RTCP_OK)
    {
        rt_snprintf(app->bluetooth_id, sizeof(app->bluetooth_id),
                    "%02X:%02X:%02X:%02X:%02X:%02X", address[0], address[1],
                    address[2], address[3], address[4], address[5]);
    }
#endif
    if (build_announcement_data(app) != RTCP_OK)
        return false;
    APP_LOG("session capabilities bluetoothID=%s bytes=%u",
            app->bluetooth_id[0] ? app->bluetooth_id : "none",
            (unsigned)app->info_plist_len);
    *bytes = app->info_plist;
    *length = app->info_plist_len;
    return true;
}

static int carplay_app_start_owned(const carplay_app_config_t *config)
{
    if (config == NULL || config->width_pixels == 0U ||
        config->height_pixels == 0U)
    {
        return RTCP_EINVAL;
    }
    if (s_receiver_running || s_receiver_worker_live)
        return RTCP_ESTATE;

    app_state_t *app = &s_app;
    memset(app, 0, sizeof(*app));
    atomic_init(&app->airplay_connected, false);
    atomic_store(&s_pairing_storage_result, RTCP_OK);
    atomic_init(&app->hid_reports_sent, 0);
    atomic_init(&app->hid_reports_failed, 0);
    s_receiver_shutting_down = false;
    s_receiver_stop = false;
    app->os = rtcp_os_rtthread;
    app->os.random = rtcp_lubanlite_random;
    app->width = config->width_pixels;
    app->height = config->height_pixels;
    app->max_fps = carplay_preferences_get(CARPLAY_PREF_HIGH_FPS, CARPLAY_PREF_HIGH_FPS_DEFAULT) ? 60U : 30U;
    app->rotation = config->rotation_degrees;
    app->wired = config->wired;
    app->on_screen = config->on_screen;
    app->screen_context = config->screen_context;
    app->oem_icons = config->oem_icons;
    app->oem_icon_count = config->oem_icon_count;
    app->oem_icon_label = config->oem_icon_label;

    if (config->rotation_degrees != 0U && config->rotation_degrees != 90U &&
        config->rotation_degrees != 180U && config->rotation_degrees != 270U)
    {
        return RTCP_EINVAL;
    }
    if (!copy_string(app->name, sizeof(app->name),
                     config->name != NULL ? config->name : DEFAULT_NAME) ||
        !copy_string(app->model, sizeof(app->model),
                     config->model != NULL ? config->model : DEFAULT_MODEL) ||
        !copy_string(app->ssid, sizeof(app->ssid),
                     config->ssid != NULL ? config->ssid : DEFAULT_SSID) ||
        !copy_string(app->seed_path, sizeof(app->seed_path),
                     config->seed_path != NULL ? config->seed_path
                                               : DEFAULT_SEED_PATH) ||
        !copy_string(app->store_path, sizeof(app->store_path),
                     config->store_path != NULL ? config->store_path
                                                : DEFAULT_STORE_PATH))
    {
        return RTCP_EINVAL;
    }
    const char *passphrase = config->passphrase != NULL
                                 ? config->passphrase
                                 : DEFAULT_PASSPHRASE;
    if (passphrase[0] != '\0' &&
        !copy_string(app->passphrase, sizeof(app->passphrase), passphrase))
    {
        return RTCP_EINVAL;
    }
    app->channel = config->channel != 0U ? config->channel : DEFAULT_CHANNEL;

    int result = build_identity(app, config->pair_code, app->seed_path);
    if (result != RTCP_OK)
        return result;

    if (!app->wired)
    {
#if defined(LPKG_RT_CARPLAY_LUBANLITE_WIFI)
        result = start_wifi(app);
        if (result != RTCP_OK)
        {
            shutdown_stack(app);
            return result;
        }
#else
        APP_LOG("wireless mode needs LPKG_RT_CARPLAY_LUBANLITE_WIFI; "
                "use 'carplay_start wired' on this build");
        shutdown_stack(app);
        return RTCP_ENOTSUP;
#endif
    }

    airplay_pairing_store_backend_t backend = {
        .load = store_load,
        .save = store_save,
        .context = app,
    };
    result = airplay_pairing_store_init(&backend);
    if (result != RTCP_OK)
    {
        APP_LOG("cannot load pairing store %s", app->store_path);
        shutdown_stack(app);
        return result;
    }
    app->store_initialized = true;

#if defined(RTCARPLAY_AUDIO)
    /* Start the sink before /info is built: its presence decides whether the
     * receiver advertises audio output.  The codec itself stays untouched
     * until the first PCM packet. */
    result = carplay_audio_sink_start();
    if (result != RTCP_OK)
    {
        APP_LOG("audio sink unavailable rc=%d; running without audio", result);
    }
    else
    {
        app->audio_started = true;
    }
#endif

#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
    result = carplay_vehicle_protocol_demo_create(&app->vehicle_demo);
    if (result != RTCP_OK)
    {
        shutdown_stack(app);
        return result;
    }
    APP_LOG("Vehicle State protocol bench: SIMULATED engine RPM only; no official Ultra renderer");
#endif
    result = build_announcement_data(app);
    if (result != RTCP_OK)
    {
        shutdown_stack(app);
        return result;
    }

    rtcp_receiver_config_t receiver_config;
    memset(&receiver_config, 0, sizeof(receiver_config));
    receiver_config.platform.os = &app->os;
    receiver_config.platform.net = &rtcp_net_rtthread;
    fill_pairing(app, &receiver_config.platform.pairing);
    receiver_config.platform.info_plist = app->info_plist;
    receiver_config.platform.info_plist_len = app->info_plist_len;
    receiver_config.platform.get_info_plist = refresh_info_plist;
    receiver_config.platform.info_context = app;
    receiver_config.port = 0; /* SDK default RTSP port. */
    receiver_config.family = app->wired ? RTCP_AF_INET6 : RTCP_AF_INET;
    receiver_config.media.on_video = on_screen_message;
    receiver_config.media.video_context = app;
#if defined(RTCARPLAY_AUDIO) || defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    receiver_config.media.on_audio_modes = on_audio_modes;
    receiver_config.media.audio_context = app;
#endif
    receiver_config.on_event_state = on_event_state;
    receiver_config.event_state_context = app;
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
    receiver_config.vehicle = app->vehicle_demo.model;
    receiver_config.negotiate_vehicle = true;
#endif
#if defined(RTCARPLAY_IAP2)
    if (!app->wired)
    {
#ifdef RT_USING_RTC
        app->tunnel_callbacks.on_device_time = carplay_app_on_device_time;
#endif
        app->tunnel_callbacks.context = app;
        app->tunnel_callbacks.configure_metadata = carplay_app_configure_classic_metadata;
        receiver_config.iap2.context = &app->tunnel_callbacks;
        receiver_config.iap2.on_bytes = carplay_iap2_tunnel_bytes;
        receiver_config.iap2.on_tick = carplay_iap2_tunnel_tick;
        receiver_config.iap2.on_stop = carplay_iap2_tunnel_stop;
        receiver_config.iap2.disable_bluetooth = carplay_iap2_request_handoff;
    }
#endif
#if defined(RTCARPLAY_AUDIO)
    if (app->audio_started)
    {
        receiver_config.media.on_audio = on_audio_pcm;
        receiver_config.media.on_audio_stop_ex = on_audio_stop;
        receiver_config.media.on_audio_duck = on_audio_duck;
#if defined(RTCARPLAY_MICROPHONE)
        receiver_config.media.on_audio_input_start = on_audio_input_start;
        receiver_config.media.on_audio_input_stop = on_audio_input_stop;
#endif
        receiver_config.media.on_volume = on_volume;
        receiver_config.media.volume_context = app;
    }
#endif
    result = rtcp_receiver_create(&receiver_config, &app->receiver);
    if (result == RTCP_OK)
        (void)rtcp_receiver_set_now_playing_details_callback(app->receiver, on_airplay_metadata, app);
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (result == RTCP_OK)
        (void)rtcp_receiver_set_host_ui_callback(app->receiver, on_host_ui, app);
#endif
#if defined(RTCARPLAY_AUDIO)
    if (result == RTCP_OK && app->audio_started)
        publish_phone_volume(app);
#endif
    if (result == RTCP_OK)
        result = rtcp_receiver_start(app->receiver);
    if (result != RTCP_OK)
    {
        APP_LOG("RTSP server start failed rc=%d", result);
        shutdown_stack(app);
        return result;
    }
    /* Cache the port for the iAP2 session; it must not touch the server. */
    app->airplay_port = rtcp_receiver_port(app->receiver);

    mdns_responder_config_t mdns_config;
    memset(&mdns_config, 0, sizeof(mdns_config));
    mdns_config.host_name = app->host_name;
    mdns_config.instance_name = app->name;
    mdns_config.port = app->airplay_port;
    mdns_config.txt = app->txt;
    mdns_config.txt_len = app->txt_len;
    memcpy(mdns_config.ipv4, app->ipv4, sizeof(app->ipv4));
    if (app->wired)
    {
        /* Wired CarPlay learns the AirPlay endpoint from CarPlayStartSession,
         * so there is nothing to advertise and no IEEE 802.11 link to browse
         * the phone on.  The response arrives over the NCM netif. */
        APP_LOG("wired mode: mDNS/Control browse disabled");
    }
    else
    {
        /* IPHETH may still be attached. Join/send mDNS on this AP rather
         * than whichever interface currently owns lwIP's default route. */
        rtcp_net_rtthread_multicast_interface(app->ipv4);
        app->mdns = mdns_responder_create(&mdns_config);
        result = app->mdns == NULL ? RTCP_ENOMEM
                                   : mdns_responder_open(app->mdns,
                                                         &rtcp_net_rtthread);
        if (result != RTCP_OK)
        {
            APP_LOG("mDNS responder start failed rc=%d", result);
            shutdown_stack(app);
            return result;
        }

        /* Browser state only; it shares the responder's socket. */
        app->browser = mdns_browser_create(CARPLAY_CTRL_SERVICE);
        app->ctrl_enabled = app->browser != NULL;
        if (app->browser == NULL)
        {
            APP_LOG("carplay_ctrl=disabled reason=no_memory");
        }
        else if (!carplay_ctrl_parse_device_id(app->device_id,
                                               &app->ctrl_device_id))
        {
            APP_LOG("carplay_ctrl=disabled reason=device_id_parse id=%s",
                    app->device_id);
            app->ctrl_enabled = false;
        }
    }

    app->bridge = input_bridge_create(app->width, app->height, app->rotation,
                                      send_hid_report, app);
    if (app->bridge == NULL)
    {
        shutdown_stack(app);
        return RTCP_ENOMEM;
    }

#if defined(RTCARPLAY_VIDEO)
    /* A caller-supplied sink wins; otherwise drive the on-board decoder. */
    if (app->on_screen == NULL)
    {
        result = carplay_video_sink_start(app->width, app->height);
        if (result != RTCP_OK)
        {
            APP_LOG("video sink start failed rc=%d", result);
            shutdown_stack(app);
            return result;
        }
        app->video_started = true;
        app->on_screen = carplay_video_sink_frame;
        app->screen_context = NULL;
    }
#endif

    app->lock = rt_mutex_create("capp", RT_IPC_FLAG_FIFO);
    if (app->lock == NULL)
    {
        shutdown_stack(app);
        return RTCP_ENOMEM;
    }

    publish_control_status(app, (uint32_t)app->os.now_ms());
    s_receiver_stop = false;
    s_receiver_running = true;
    app->thread = rt_thread_create("carplay", poll_thread, app,
                                   POLL_THREAD_STACK_BYTES,
                                   POLL_THREAD_PRIORITY, POLL_THREAD_TICK);
    if (app->thread == NULL)
    {
        s_receiver_running = false;
        shutdown_stack(app);
        return RTCP_ENOMEM;
    }
    s_receiver_worker_live = true;
    if (rt_thread_startup(app->thread) != RT_EOK)
    {
        (void)rt_thread_delete(app->thread);
        app->thread = NULL;
        s_receiver_worker_live = false;
        s_receiver_running = false;
        shutdown_stack(app);
        return RTCP_EIO;
    }

#if defined(RTCARPLAY_TOUCH_PANEL) && !defined(RTCARPLAY_DEMO_LAUNCHER)
    /* The touch queue exists now (s_receiver_running and app->lock); a board without
     * the panel driver keeps working through carplay_touch. */
    result = carplay_touch_panel_start(app->width, app->height, on_panel_touch,
                                       app);
    if (result != RTCP_OK)
    {
        APP_LOG("touch panel unavailable rc=%d; use carplay_touch", result);
    }
    else
    {
        app->touch_panel_started = true;
    }
#endif

    if (app->wired)
    {
        APP_LOG("wired AirPlay on IPv6:%u (device %s, PIN %s)",
                (unsigned)rtcp_receiver_port(app->receiver),
                app->device_id, app->pairing_code);
    }
    else
    {
        APP_LOG("advertising \"%s\" on %u.%u.%u.%u:%u (device %s, PIN %s)",
                app->name, app->ipv4[0], app->ipv4[1], app->ipv4[2],
                app->ipv4[3],
                (unsigned)rtcp_receiver_port(app->receiver),
                app->device_id, app->pairing_code);
    }
    return RTCP_OK;
}

static atomic_bool s_maintenance;

bool carplay_app_maintenance_begin(void)
{
    if (atomic_flag_test_and_set_explicit(&s_lifecycle_call, memory_order_acquire))
        return false;
    atomic_store(&s_maintenance, true);
    atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
    return true;
}

void carplay_app_maintenance_end(void)
{
    atomic_store(&s_maintenance, false);
}

int carplay_app_start(const carplay_app_config_t *config)
{
    if (atomic_flag_test_and_set_explicit(&s_lifecycle_call,
                                          memory_order_acquire))
        return RTCP_ESTATE;
    if (atomic_load(&s_maintenance))
    {
        atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
        return RTCP_ESTATE;
    }
    snapshots_retire();
    int result = carplay_app_start_owned(config);
    snapshots_enable();
    atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
    return result;
}

static int carplay_app_stop_owned(void)
{
    if (s_app.thread != NULL && rt_thread_self() == s_app.thread)
        return RTCP_ESTATE; /* A callback cannot join its own poll worker. */
    if (!s_receiver_running)
        return RTCP_OK;
#if defined(RTCARPLAY_IAP2)
    /* The iAP2 session borrows the receiver state, so retire it before the
     * poll thread clears the stack.  The iAP2 thread owns its teardown. */
    if (carplay_iap2_is_running())
    {
        int result = carplay_iap2_stop();
        if (result != RTCP_OK)
            return result;
    }
#endif
    s_receiver_stop = true;
    /* The poll thread tears the stack down before clearing running. */
    while (s_receiver_running)
    {
        rt_thread_mdelay(10);
    }
    return RTCP_OK;
}

int carplay_app_stop(void)
{
    if (atomic_flag_test_and_set_explicit(&s_lifecycle_call,
                                          memory_order_acquire))
        return RTCP_ESTATE;
    snapshots_retire();
    int result = carplay_app_stop_owned();
    snapshots_enable();
    atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
    return result;
}

bool carplay_app_is_running(void)
{
    return s_receiver_running;
}

int carplay_app_session_state(rtcp_receiver_state_t *state)
{
    if (!state)
        return RTCP_EINVAL;
    memset(state, 0, sizeof(*state));
    if (!snapshot_enter())
        return RTCP_ESTATE;
    int result = RTCP_ESTATE;
    if (s_receiver_running && !s_receiver_stop && s_app.lock &&
        rt_mutex_take(s_app.lock, RT_WAITING_NO) == RT_EOK)
    {
        *state = s_app.receiver_state;
        (void)rt_mutex_release(s_app.lock);
        result = RTCP_OK;
    }
    snapshot_leave();
    return result;
}

int carplay_app_display_info(carplay_app_display_info_t *info)
{
    if (info == NULL)
        return RTCP_EINVAL;
    memset(info, 0, sizeof(*info));
    if (!snapshot_enter())
        return RTCP_ESTATE;
    info->width = s_app.width;
    info->height = s_app.height;
    memcpy(info->pairing_code, s_app.pairing_code, sizeof(info->pairing_code));
    snapshot_leave();
    return RTCP_OK;
}

void carplay_app_display_size(uint16_t *width, uint16_t *height)
{
    carplay_app_display_info_t info;
    (void)carplay_app_display_info(&info);
    if (width != NULL)
        *width = info.width;
    if (height != NULL)
        *height = info.height;
}

const char *carplay_app_pairing_code(void)
{
    return s_app.pairing_code;
}

static int iap2_info_owned(carplay_app_iap2_info_t *info)
{
    app_state_t *app = &s_app;
    if (!s_receiver_running || s_receiver_stop || app->wired)
        return RTCP_ESTATE;

    memcpy(info->name, app->name, sizeof(info->name));
    memcpy(info->model, app->model, sizeof(info->model));
    memcpy(info->ssid, app->ssid, sizeof(info->ssid));
    memcpy(info->passphrase, app->passphrase, sizeof(info->passphrase));
    info->channel = app->channel;
    info->security_type = app->passphrase[0] != '\0' ? 2U : 0U;
    info->airplay_port = app->airplay_port;
    memcpy(info->ipv4, app->ipv4, sizeof(info->ipv4));
    if (app->wifi_mac_valid)
    {
        memcpy(info->bssid, app->wifi_mac, sizeof(info->bssid));
        info->bssid_valid = true;
    }
    memcpy(info->device_id, app->device_id, sizeof(info->device_id));
    memcpy(info->public_key_hex, app->public_key_hex, sizeof(info->public_key_hex));
    info->source_version = AIRPLAY_INFO_DEFAULT_SOURCE_VERSION;
    info->mdns_ready = app->mdns != NULL;
    info->airplay_connected = app->airplay_connected;
    return RTCP_OK;
}

static int wired_info_owned(carplay_app_wired_info_t *info)
{
    app_state_t *app = &s_app;
    if (!s_receiver_running || s_receiver_stop || !app->wired)
        return RTCP_ESTATE;
#if !defined(LPKG_RT_CARPLAY_LUBANLITE_USB) || !defined(RT_USING_LWIP)
    (void)info;
    return RTCP_ENOTSUP;
#else
    bool valid = false;
    if (!rtcp_lubanlite_wired_ipv6(info->ipv6, sizeof(info->ipv6), &valid) ||
        !valid)
    {
        /* Tentative addresses have not passed duplicate-address detection;
         * advertising one would race the phone's first connection. */
        return RTCP_ENOENT;
    }
    memcpy(info->device_id, app->device_id, sizeof(info->device_id));
    memcpy(info->public_key_hex, app->public_key_hex, sizeof(info->public_key_hex));
    info->source_version = AIRPLAY_INFO_DEFAULT_SOURCE_VERSION;
    info->airplay_port = app->airplay_port;
    info->airplay_connected = app->airplay_connected;
    return RTCP_OK;
#endif
}

int carplay_app_iap2_info(carplay_app_iap2_info_t *info)
{
    if (info == NULL)
        return RTCP_EINVAL;
    memset(info, 0, sizeof(*info));
    if (!snapshot_enter())
        return RTCP_ESTATE;
    int result = iap2_info_owned(info);
    snapshot_leave();
    return result;
}

int carplay_app_wired_info(carplay_app_wired_info_t *info)
{
    if (info == NULL)
        return RTCP_EINVAL;
    memset(info, 0, sizeof(*info));
    if (!snapshot_enter())
        return RTCP_ESTATE;
    int result = wired_info_owned(info);
    snapshot_leave();
    return result;
}

/* The launcher, shell and serving workers can claim/release concurrently. */
static atomic_int s_bt_owner = ATOMIC_VAR_INIT(CARPLAY_BT_OWNER_NONE);

bool carplay_bt_claim(carplay_bt_owner_t owner)
{
    if (owner == CARPLAY_BT_OWNER_NONE)
        return false;
    int expected = CARPLAY_BT_OWNER_NONE;
    return atomic_compare_exchange_strong(&s_bt_owner, &expected, (int)owner);
}

void carplay_bt_release(carplay_bt_owner_t owner)
{
    int expected = (int)owner;
    (void)atomic_compare_exchange_strong(&s_bt_owner, &expected,
                                         CARPLAY_BT_OWNER_NONE);
}

carplay_bt_owner_t carplay_bt_owner(void)
{
    return (carplay_bt_owner_t)s_bt_owner;
}

int carplay_app_touch(uint8_t phase, uint16_t pointer_id,
                      uint16_t x, uint16_t y)
{
    if (phase < RTCP_TOUCH_DOWN || phase > RTCP_TOUCH_UP)
    {
        return RTCP_EINVAL;
    }
    touch_event_t event = {
        .phase = phase,
        .pointer_id = pointer_id,
        .x = x,
        .y = y,
    };
    return enqueue_touch(&event);
}

int carplay_app_autostart_display(bool wired, uint16_t width, uint16_t height)
{
    carplay_app_config_t config;
    carplay_app_config_defaults(&config);
    config.width_pixels = width;
    config.height_pixels = height;
    config.rotation_degrees = CARPLAY_APP_DEFAULT_ROTATION;
    config.wired = wired;
#if defined(RTCARPLAY_DEMO_LAUNCHER)
    config.oem_icons = &demo_oem_icon;
    config.oem_icon_count = 1U;
    config.oem_icon_label = "rt-carplay";
#endif
    return carplay_app_start(&config);
}

int carplay_app_autostart(void)
{
    return carplay_app_autostart_display(false, CARPLAY_APP_DEFAULT_WIDTH, CARPLAY_APP_DEFAULT_HEIGHT);
}

int carplay_app_autostart_wired(void)
{
    return carplay_app_autostart_display(true, CARPLAY_APP_DEFAULT_WIDTH, CARPLAY_APP_DEFAULT_HEIGHT);
}

/* ------------------------------------------------------------------ */
/* MSH commands                                                        */
/* ------------------------------------------------------------------ */

static int cmd_carplay_start(int argc, char **argv)
{
#if defined(RTCARPLAY_DEMO_LAUNCHER) || defined(RTCARPLAY_AUTO_CONNECT)
    (void)argc;
    (void)argv;
    rt_kprintf("Automatic connection manager owns the receiver\n");
    return 0;
#endif
    bool wired = argc > 1 && strcmp(argv[1], "wired") == 0;
    if (argc > 1 && !wired)
    {
        rt_kprintf("usage: carplay_start [wired]\n");
        return 0;
    }
    if (carplay_app_is_running())
    {
        rt_kprintf("carplay is already running\n");
        return 0;
    }
    int result = wired ? carplay_app_autostart_wired()
                       : carplay_app_autostart();
    if (result != RTCP_OK)
    {
        rt_kprintf("carplay start failed: %d\n", result);
        return 0;
    }
    carplay_app_display_info_t display_info;
    (void)carplay_app_display_info(&display_info);
    rt_kprintf("carplay started (%s), pairing PIN %s\n",
               wired ? "wired IPv6" : "wireless", display_info.pairing_code);
    return 0;
}

static int cmd_carplay_stop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if defined(RTCARPLAY_DEMO_LAUNCHER) || defined(RTCARPLAY_AUTO_CONNECT)
    rt_kprintf("Automatic connection manager owns the receiver\n");
    return 0;
#endif
    (void)carplay_app_stop();
    rt_kprintf("carplay stopped\n");
    return 0;
}

static int carplay_status_owned(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint16_t width = 0U;
    uint16_t height = 0U;
    width = s_app.width;
    height = s_app.height;
    rt_kprintf("carplay: %s, display %ux%u, PIN %s\n",
               carplay_app_is_running() ? "running" : "stopped",
               (unsigned)width, (unsigned)height,
               carplay_app_pairing_code()[0] != '\0'
                   ? carplay_app_pairing_code()
                   : "-");
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
    carplay_vehicle_protocol_snapshot_t vehicle = { 0 };
    if (s_app.lock && rt_mutex_take(s_app.lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        vehicle = s_app.vehicle_snapshot;
        (void)rt_mutex_release(s_app.lock);
    }
    rt_kprintf("carplay vehicle: SIMULATED combustion RPM=%u max=6000 redline=5000 sample_error=%d\n",
               vehicle.rpm, vehicle.sample_error);
    rt_kprintf("vehicle protocol: offered=%u enabled=%u normal=%u high=%u requests=%u rejected=%u subscriptions=%u notifications=%u\n",
               vehicle.normal.offered ? 1U : 0U, vehicle.normal.enabled ? 1U : 0U,
               (unsigned)vehicle.normal.state, (unsigned)vehicle.high.state,
               vehicle.model.requests, vehicle.model.rejected_messages,
               (unsigned)vehicle.model.subscribed, vehicle.model.notifications);
    static const char * const methods[] = { "unknown", "config", "read", "register", "write", "control" };
    for (unsigned method = 0; method < RTCP_VEHICLE_METHOD_COUNT; ++method)
        rt_kprintf("vehicle method %s: requests=%u responses=%u\n", methods[method],
                   vehicle.model.method_requests[method], vehicle.model.method_responses[method]);
    rt_kprintf("vehicle last: method=%s lane=%s result=%d valid=%u\n",
               methods[(unsigned)vehicle.model.last_method < RTCP_VEHICLE_METHOD_COUNT ? vehicle.model.last_method : 0],
               vehicle.model.last_lane == RTCP_VEHICLE_HIGH ? "HIGH" : "NORMAL", vehicle.model.last_result,
               vehicle.model.requests ? 1U : 0U);
    rt_kprintf("vehicle delivery: queued=%u retries=%u rejected=%u abandoned_replies=%u abandoned_notify=%u error=%d\n",
               (unsigned)vehicle.delivery.queued_messages, (unsigned)vehicle.delivery.queue_retries,
               (unsigned)vehicle.delivery.rejected_requests, (unsigned)vehicle.delivery.abandoned_replies,
               (unsigned)vehicle.delivery.abandoned_notifications, vehicle.delivery.last_error);
#endif
#if defined(RTCARPLAY_AUDIO)
    unsigned packets = 0U, dropped = 0U, errors = 0U, replays = 0U;
    carplay_audio_sink_stats(&packets, &dropped, &errors, &replays);
    rt_kprintf("carplay audio: %s, %u packets, %u replay(s), %u dropped "
               "bytes, %u write errors\n",
               carplay_audio_sink_is_running() ? "ready" : "stopped", packets,
               replays, dropped, errors);
    rtcp_audio_stats_t audio = { 0 };
    rtcp_audio_modes_t modes = { 0 };
    rtcp_timing_stats_t timing = { 0 };
    if (s_app.lock && rt_mutex_take(s_app.lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        audio = s_app.audio_stats;
        modes = s_app.audio_modes;
        timing = s_app.timing_stats;
        (void)rt_mutex_release(s_app.lock);
    }
    rt_kprintf("carplay timing: active=%u request_tx=%u response_rx=%u request_rx=%u response_tx=%u rejected=%u send_errors=%u timeouts=%u\n",
               timing.active ? 1U : 0U, (unsigned)timing.requests_sent,
               (unsigned)timing.responses_received, (unsigned)timing.requests_received,
               (unsigned)timing.responses_sent, (unsigned)timing.rejected,
               (unsigned)timing.send_errors, (unsigned)timing.timeouts);
    rt_kprintf("carplay audio transport: streams=%u datagrams=%u auth_failures=%u\n",
               audio.active_streams, audio.datagrams, audio.auth_failures);
    rt_kprintf("audio receive: delivered=%u sink_rejected=%u malformed=%u replay=%u\n",
               audio.delivered, audio.sink_rejected, audio.malformed, audio.replay_drops);
    rt_kprintf("audio receive: seq_missing=%u reordered=%u poll_gap_max_us=%u\n",
               audio.sequence_missing, audio.sequence_reordered, audio.poll_gap_max_us);
    rt_kprintf("carplay audio modes: entity=%s%u call=%u speech=%d\n",
               modes.has_main_audio ? "" : "unknown/", modes.main_audio_entity,
               modes.call_active ? 1U : 0U, modes.has_speech ? modes.speech_mode : -1);
    rt_kprintf("carplay audio resource: permanent=%s%u screen=%s%u screen_permanent=%s%u (0=none 1=iPhone 2=accessory)\n",
               modes.has_main_audio_permanent ? "" : "unknown/", modes.main_audio_permanent_entity,
               modes.has_screen ? "" : "unknown/", modes.screen_entity,
               modes.has_screen_permanent ? "" : "unknown/", modes.screen_permanent_entity);
    carplay_audio_sink_diagnostics();
#if defined(RTCARPLAY_MICROPHONE)
    carplay_audio_input_status();
#endif
    int volume = carplay_audio_sink_volume_percent();
    if (volume >= 0)
    {
        rt_kprintf("carplay audio speaker volume: %d%%\n", volume);
    }
#endif
#if defined(RTCARPLAY_TOUCH_PANEL)
    uint16_t points = carplay_touch_panel_points();
    unsigned reads = 0U, events = 0U;
    carplay_touch_panel_stats(&reads, &events);
    rt_kprintf("carplay touch panel: %s, %u point(s), reads=%u events=%u\n",
               carplay_touch_panel_is_running() ? "running" : "stopped",
               (unsigned)points, reads, events);
#endif
    unsigned queued = 0, full = 0;
    rtcp_projection_owner_t projection_owner = RTCP_PROJECTION_UNKNOWN;
    bool media_title = false, media_artist = false, media_state = false, playing = false;
    bool route_road = false, route_destination = false, route_remaining = false;
    if (s_app.lock && rt_mutex_take(s_app.lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        queued = s_app.touch_queued;
        full = s_app.touch_queue_full;
        projection_owner = s_app.projection_owner;
        const carplay_phone_track_t *track = s_app.phone_media.track.title[0] ? &s_app.phone_media.track : &s_app.iap2_track;
        media_title = track->title[0] != '\0';
        media_artist = track->artist[0] != '\0';
        media_state = track->has_playing;
        playing = track->playing;
        route_road = s_app.phone_media.route_road[0] != '\0';
        route_destination = s_app.phone_media.route_destination[0] != '\0';
        route_remaining = s_app.phone_media.route_remaining[0] != '\0';
        (void)rt_mutex_release(s_app.lock);
    }
    rt_kprintf("carplay now_playing: title=%u artist=%u playback=%u playing=%u\n",
               media_title ? 1U : 0U, media_artist ? 1U : 0U, media_state ? 1U : 0U, playing ? 1U : 0U);
    rt_kprintf("carplay route_guidance: road=%u destination=%u remaining=%u\n",
               route_road ? 1U : 0U, route_destination ? 1U : 0U, route_remaining ? 1U : 0U);
    rt_kprintf("carplay touch: queued=%u queue_full=%u HID_sent=%u HID_failed=%u\n",
               queued, full,
               s_app.hid_reports_sent, s_app.hid_reports_failed);
    const char *owner = projection_owner == RTCP_PROJECTION_PHONE ? "phone" : projection_owner == RTCP_PROJECTION_HOST ? "host"
                                                                                                                       : "unknown";
    rt_kprintf("carplay MainScreen owner=%s\n", owner);
    rtcp_screen_stats_t screen = { 0 };
    if (s_app.lock != NULL && rt_mutex_take(s_app.lock, RT_WAITING_NO) == RT_EOK)
    {
        screen = s_app.screen_stats;
        (void)rt_mutex_release(s_app.lock);
    }
    uint32_t now = (uint32_t)rt_tick_get_millisecond();
    uint32_t interval = now - s_app.screen_report_ms;
    uint32_t delta = screen.frames >= s_app.screen_report_frames ? screen.frames - s_app.screen_report_frames : screen.frames;
    unsigned fps10 = interval != 0U ? (unsigned)((uint64_t)delta * 10000U / interval) : 0U;
    rt_kprintf("carplay screen offer: %ux%u offered_max_fps=%u\n",
               (unsigned)s_app.width, (unsigned)s_app.height, (unsigned)s_app.max_fps);
    rt_kprintf("carplay screen RX: frames=%u wire_KiB=%u recv_calls=%u interval=%ums fps=%u.%u\n",
               (unsigned)screen.frames, (unsigned)(screen.wire_bytes / 1024U),
               (unsigned)screen.recv_calls, (unsigned)interval, fps10 / 10U, fps10 % 10U);
    rt_kprintf("carplay screen process_us avg/max=%u/%u (includes callback/scheduling)\n",
               screen.messages ? (unsigned)(screen.process_us / screen.messages) : 0U,
               (unsigned)screen.process_max_us);
    rt_kprintf("carplay screen gaps_ms avg/max: receive=%u/%u sender=%u/%u assembly=%u/%u\n",
               screen.frame_gap_count ? (unsigned)(screen.frame_gap_us / screen.frame_gap_count / 1000U) : 0U,
               screen.frame_gap_max_us / 1000U,
               screen.sender_gap_count ? (unsigned)(screen.sender_gap_us / screen.sender_gap_count / 1000U) : 0U,
               screen.sender_gap_max_us / 1000U,
               screen.messages ? (unsigned)(screen.assembly_us / screen.messages / 1000U) : 0U,
               screen.assembly_max_us / 1000U);
    rt_kprintf("carplay screen gap counts: <=40ms=%u 40..100ms=%u >100ms=%u\n",
               screen.frame_gap_le40ms, screen.frame_gap_le100ms, screen.frame_gap_gt100ms);
    s_app.screen_report_ms = now;
    s_app.screen_report_frames = screen.frames;
    return 0;
}

static int cmd_carplay_status(int argc, char **argv)
{
    if (atomic_flag_test_and_set_explicit(&s_lifecycle_call, memory_order_acquire))
        return RTCP_ESTATE;
    int result = carplay_status_owned(argc, argv);
    atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
    return result;
}

static int cmd_carplay_touch(int argc, char **argv)
{
    if (argc < 4)
    {
        rt_kprintf("usage: carplay_touch down|move|up X Y [pointer]\n");
        return 0;
    }
    uint8_t phase;
    if (strcmp(argv[1], "down") == 0)
    {
        phase = RTCP_TOUCH_DOWN;
    }
    else if (strcmp(argv[1], "move") == 0)
    {
        phase = RTCP_TOUCH_MOVE;
    }
    else if (strcmp(argv[1], "up") == 0)
    {
        phase = RTCP_TOUCH_UP;
    }
    else
    {
        rt_kprintf("unknown touch phase: %s\n", argv[1]);
        return 0;
    }
    unsigned x = (unsigned)atoi(argv[2]);
    unsigned y = (unsigned)atoi(argv[3]);
    unsigned pointer = argc >= 5 ? (unsigned)atoi(argv[4]) : 0U;
    int result = carplay_app_touch(phase, (uint16_t)pointer, (uint16_t)x,
                                   (uint16_t)y);
    if (result != RTCP_OK)
    {
        rt_kprintf("touch rejected: %d\n", result);
    }
    return 0;
}

static int cmd_carplay_randtest(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint8_t first[32] = { 0 };
    uint8_t second[32] = { 0 };
    bool ok_first = rtcp_lubanlite_random(first, sizeof(first));
    bool ok_second = rtcp_lubanlite_random(second, sizeof(second));
    for (size_t i = 0U; i < sizeof(first); ++i)
    {
        rt_kprintf("%02x", first[i]);
    }
    rt_kprintf("\n");
    for (size_t i = 0U; i < sizeof(second); ++i)
    {
        rt_kprintf("%02x", second[i]);
    }
    rt_kprintf("\n");
    if (!ok_first || !ok_second)
    {
        rt_kprintf("carplay_randtest: entropy provider unavailable "
                   "(check the CE TRNG error log)\n");
    }
    else if (memcmp(first, second, sizeof(first)) == 0)
    {
        rt_kprintf("carplay_randtest: FAIL, two calls returned identical "
                   "bytes\n");
    }
    else
    {
        rt_kprintf("carplay_randtest: OK, distinct output\n");
    }
    memset(first, 0, sizeof(first));
    memset(second, 0, sizeof(second));
    return 0;
}

#if defined(RTCARPLAY_BT)
/*
 * Bluetooth RFCOMM bring-up.  This is a transport check, not CarPlay: it
 * advertises the iAP2 service class UUID over SDP, accepts one phone
 * connection and echoes bytes back.  A Bluetooth serial terminal that
 * browses SDP for the iAP2 UUID (or an iAP2 probe) exercises the path.
 *
 * The UUID is the accessory-side iAP2 service class (...CAFF), the same
 * value the vendor reference Bluetooth library registers with its RFCOMM server
 * (iap2_uuid_srv).  ...CAFE is the iPhone-side service and must not be
 * used here: advertising it made iOS see no accessory iAP2 service and
 * drop the link (diag7).
 */
#define BT_BRINGUP_UUID     "00000000-deca-fade-deca-deafdecacaff"
#define BT_BRINGUP_STACK    (8U * 1024U)
#define BT_BRINGUP_PRIORITY 20U
#define BT_BRINGUP_TICK     20U

static atomic_bool s_bt_serve = ATOMIC_VAR_INIT(false);
static atomic_bool s_bt_thread_running = ATOMIC_VAR_INIT(false);
static atomic_flag s_bt_call = ATOMIC_FLAG_INIT;
static _Atomic(rt_thread_t) s_bt_thread;

static void bt_event_cb(int ev, void *user)
{
    (void)user;
    APP_LOG("bt event: %s", ev == RTCP_BT_EV_CONNECTED      ? "connected"
                            : ev == RTCP_BT_EV_DISCONNECTED ? "disconnected"
                                                            : "other");
}

static void bt_serve_thread(void *parameter)
{
    (void)parameter;
    int result = RTCP_OK;
    char bt_name[CARPLAY_BT_NAME_MAX + 1];
    if (!s_bt_serve)
        goto done;
    carplay_preferences_bt_name(bt_name);
    result = rtcp_bt_lubanlite.enable(bt_name, NULL);
    if (result != RTCP_OK)
    {
        APP_LOG("bt enable failed rc=%d", result);
        goto done;
    }

    if (!s_bt_serve)
        goto done;
    uint8_t channel = 0U;
    result = rtcp_bt_lubanlite.rfcomm_listen(0U, "rt-carplay",
                                             BT_BRINGUP_UUID, &channel);
    if (result != RTCP_OK)
    {
        APP_LOG("bt listen failed rc=%d", result);
        goto done;
    }
    APP_LOG("bt RFCOMM listening on channel %u", (unsigned)channel);

    while (s_bt_serve)
    {
        int fd = -1;
        result = rtcp_bt_lubanlite_rfcomm_accept(1, &fd);
        if (result != RTCP_OK)
        {
            APP_LOG("bt accept stopped rc=%d", result);
            break;
        }
        APP_LOG("bt link up (fd %d)", fd);
        while (s_bt_serve)
        {
            uint8_t buffer[256];
            int received = rtcp_bt_lubanlite_recv(fd, buffer, sizeof(buffer));
            if (received == RTCP_ETIMEDOUT)
                continue;
            if (received <= 0)
                break;
            APP_LOG("bt rx %d byte(s)", received);
            if (rtcp_bt_lubanlite_send(fd, buffer, (size_t)received) < 0)
            {
                break;
            }
        }
        APP_LOG("bt link down");
    }

done:
    rtcp_bt_lubanlite_disconnect();
    rtcp_bt_lubanlite_set_event_cb(NULL, NULL);
    s_bt_serve = false;
    s_bt_thread = NULL;
    carplay_bt_release(CARPLAY_BT_OWNER_ECHO);
    /* Quiescence is the final publication, including stop before scheduling. */
    s_bt_thread_running = false;
}

static int carplay_bt_command_owned(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "status";
#if defined(RTCARPLAY_DEMO_LAUNCHER) || defined(RTCARPLAY_AUTO_CONNECT)
    if (strcmp(sub, "status") != 0)
    {
        rt_kprintf("Automatic connection manager owns Bluetooth; use the connection selector\n");
        return 0;
    }
#endif
    if (strcmp(sub, "start") == 0)
    {
        if (s_bt_thread_running)
        {
            rt_kprintf("carplay_bt already running\n");
            return 0;
        }
        if (!carplay_bt_claim(CARPLAY_BT_OWNER_ECHO))
        {
            rt_kprintf("carplay_bt: Bluetooth link is in use by %s\n",
                       carplay_bt_owner() == CARPLAY_BT_OWNER_IAP2
                           ? "carplay_iap2"
                           : "another service");
            return 0;
        }
        rtcp_bt_lubanlite_set_event_cb(bt_event_cb, NULL);
        s_bt_serve = true;
        s_bt_thread = rt_thread_create("carplay-bt", bt_serve_thread, NULL,
                                       BT_BRINGUP_STACK, BT_BRINGUP_PRIORITY,
                                       BT_BRINGUP_TICK);
        if (s_bt_thread == NULL)
        {
            s_bt_serve = false;
            rtcp_bt_lubanlite_set_event_cb(NULL, NULL);
            carplay_bt_release(CARPLAY_BT_OWNER_ECHO);
            rt_kprintf("carplay_bt: cannot create thread\n");
            return RTCP_ENOMEM;
        }
        s_bt_thread_running = true;
        if (rt_thread_startup(s_bt_thread) != RT_EOK)
        {
            (void)rt_thread_delete(s_bt_thread);
            s_bt_thread = NULL;
            s_bt_serve = false;
            rtcp_bt_lubanlite_set_event_cb(NULL, NULL);
            carplay_bt_release(CARPLAY_BT_OWNER_ECHO);
            s_bt_thread_running = false;
            return RTCP_EIO;
        }
        return 0;
    }
    if (strcmp(sub, "stop") == 0)
    {
        if (!s_bt_thread_running)
            return RTCP_OK;
        if (rt_thread_self() == s_bt_thread)
            return RTCP_ESTATE;
        s_bt_serve = false;
        rtcp_bt_lubanlite_disconnect();
        for (unsigned waited = 0; s_bt_thread_running && waited < 3000U; waited += 10U)
            rt_thread_mdelay(10);
        if (s_bt_thread_running)
            return RTCP_ETIMEDOUT;
        rt_kprintf("carplay_bt stopped\n");
        return 0;
    }
    if (strcmp(sub, "status") == 0)
    {
        rt_kprintf("carplay_bt: %s, channel %u, link %s, rx drops %u\n",
                   !rtcp_bt_lubanlite_is_up()
                       ? "off"
                       : (s_bt_thread_running ? "serving" : "idle"),
                   (unsigned)rtcp_bt_lubanlite_channel(),
                   rtcp_bt_lubanlite_link_up() ? "up" : "down",
                   rtcp_bt_lubanlite_rx_dropped());
        rtcp_bt_pairing_status_t pairing;
        rtcp_bt_lubanlite_pairing_status(&pairing);
        rt_kprintf("carplay_bt bonds=%u/%u persistent=%u storage_error=%d HCI=0x%02x security=%u failures=%u\n",
                   pairing.bond_count, pairing.bond_capacity, pairing.persistent,
                   pairing.storage_error, pairing.hci_error, pairing.security_error,
                   pairing.failures);
        rt_kprintf("carplay_bt radio: scan=%s0x%02x error=%d reconnect_attempts=%u active_ACL=%u pairing=%u\n",
                   pairing.scan_known ? "" : "unknown/", pairing.scan_enable,
                   pairing.radio_error, pairing.reconnect_attempts, pairing.active_acls, pairing.kind);
        return 0;
    }
    rt_kprintf("usage: carplay_bt start|stop|status\n");
    return 0;
}
static int cmd_carplay_bt(int argc, char **argv)
{
    if (argc < 1 || argc > 2 ||
        (argc == 2 && strcmp(argv[1], "start") != 0 &&
         strcmp(argv[1], "stop") != 0 && strcmp(argv[1], "status") != 0))
        return RTCP_EINVAL;
    if (atomic_flag_test_and_set_explicit(&s_bt_call, memory_order_acquire))
        return RTCP_ESTATE;
    int result = carplay_bt_command_owned(argc, argv);
    atomic_flag_clear_explicit(&s_bt_call, memory_order_release);
    return result;
}
#endif /* RTCARPLAY_BT */

/* Caller holds lifecycle gate; the poll owner alone accesses live state. */
static int control_request_owned(const char *sub, control_status_t *status,
                                 bool *enable_pending, bool *connect_pending,
                                 bool *requested_enabled)
{
    if (!s_receiver_running || s_receiver_shutting_down || s_app.lock == NULL)
        return RTCP_ESTATE;
    if (rt_mutex_take(s_app.lock, RT_WAITING_NO) != RT_EOK)
        return RTCP_ETIMEDOUT;
    int result = RTCP_OK;
    if (strcmp(sub, "status") != 0)
    {
        if (!s_app.ctrl_status.available)
        {
            result = RTCP_ESTATE;
        }
        else if (strcmp(sub, "connect") == 0)
        {
            s_app.ctrl_connect_pending = true;
        }
        else
        {
            s_app.ctrl_requested_enabled = strcmp(sub, "on") == 0;
            s_app.ctrl_enable_pending = true;
        }
    }
    *status = s_app.ctrl_status;
    *enable_pending = s_app.ctrl_enable_pending;
    *connect_pending = s_app.ctrl_connect_pending;
    *requested_enabled = s_app.ctrl_requested_enabled;
    (void)rt_mutex_release(s_app.lock);
    return result;
}

static int cmd_carplay_ctrl(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "status";
    if (argc < 1 || argc > 2 ||
        (strcmp(sub, "on") != 0 && strcmp(sub, "off") != 0 &&
         strcmp(sub, "connect") != 0 && strcmp(sub, "status") != 0))
    {
        rt_kprintf("usage: carplay_ctrl on|off|connect|status\n");
        return RTCP_EINVAL;
    }
    if (atomic_flag_test_and_set_explicit(&s_lifecycle_call, memory_order_acquire))
        return RTCP_ESTATE;
    control_status_t status;
    bool enable_pending, connect_pending, requested_enabled;
    int result = control_request_owned(sub, &status, &enable_pending,
                                       &connect_pending, &requested_enabled);
    atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
    if (result != RTCP_OK)
    {
        rt_kprintf("carplay_ctrl: unavailable rc=%d\n", result);
        return result;
    }
    if (strcmp(sub, "status") != 0)
    {
        rt_kprintf("carplay_ctrl: %s requested (queued)\n", sub);
        return RTCP_OK;
    }
    rt_kprintf("carplay_ctrl: %s, %u endpoint(s), id %llu, attempts %u, "
               "responses %u, last status %d\n",
               !status.available ? "unavailable" : status.enabled ? "enabled"
                                                                  : "disabled",
               status.endpoints, (unsigned long long)status.device_id,
               status.attempts, status.responses, status.last_status);
    rt_kprintf("  pending: enable=%u requested=%u connect=%u\n",
               (unsigned)enable_pending, (unsigned)requested_enabled, (unsigned)connect_pending);
    rt_kprintf("  browse: queries %u, tx errors %u, packets %u, updates %u\n",
               status.queries, status.tx_errors, status.packets, status.updates);
    if (status.peer_valid)
    {
        rt_kprintf("  unicast peer %u.%u.%u.%u\n", status.peer_ipv4[0],
                   status.peer_ipv4[1], status.peer_ipv4[2], status.peer_ipv4[3]);
    }
    else
    {
        rt_kprintf("  unicast peer: none\n");
    }
    if (status.has_endpoint)
    {
        const mdns_browser_endpoint_t *endpoint = &status.endpoint;
        rt_kprintf("  endpoint %u.%u.%u.%u:%u instance=%s\n", endpoint->ipv4[0],
                   endpoint->ipv4[1], endpoint->ipv4[2], endpoint->ipv4[3],
                   (unsigned)endpoint->port, endpoint->instance);
    }
    return RTCP_OK;
}

/* Wireless A/B knob for the auth-setup close: Apple's transport chunks a
 * reply at 1024 plaintext bytes, but the observed controller closes right
 * after the 1166-byte auth-setup reply.  Flip framing here and retest without
 * reflashing; the default (chunked) is unchanged. */
static int cipher_request_owned(bool update, bool enabled,
                                bool *requested, bool *applied, bool *pending)
{
    if (!s_receiver_running || s_receiver_shutting_down || s_app.lock == NULL)
        return RTCP_ESTATE;
    if (rt_mutex_take(s_app.lock, RT_WAITING_NO) != RT_EOK)
        return RTCP_ETIMEDOUT;
    if (update)
    {
        s_app.cipher_requested = enabled;
        s_app.cipher_pending = true;
    }
    *requested = s_app.cipher_requested;
    *applied = s_app.cipher_applied;
    *pending = s_app.cipher_pending;
    (void)rt_mutex_release(s_app.lock);
    return RTCP_OK;
}

static int cmd_carplay_ab(int argc, char **argv)
{
    bool enabled = false;
    if (argc < 1 || argc > 2 ||
        (argc == 2 && strcmp(argv[1], "single") != 0 &&
         strcmp(argv[1], "chunked") != 0))
    {
        rt_kprintf("usage: carplay_ab [single|chunked]\n");
        return RTCP_EINVAL;
    }
    if (argc == 2)
        enabled = strcmp(argv[1], "single") == 0;
    if (atomic_flag_test_and_set_explicit(&s_lifecycle_call, memory_order_acquire))
        return RTCP_ESTATE;
    bool requested = false, applied = false, pending = false;
    int result = cipher_request_owned(argc == 2, enabled, &requested, &applied, &pending);
    atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
    if (result == RTCP_OK)
    {
        rt_kprintf("carplay_ab: ControlCipher framing %s requested=%s pending=%u\n",
                   applied ? "single-frame" : "chunked (1024 B)",
                   requested ? "single-frame" : "chunked (1024 B)",
                   (unsigned)pending);
    }
    else
    {
        rt_kprintf("carplay_ab: receiver unavailable rc=%d\n", result);
    }
    return result;
}

static int request_siri_owned(void)
{
    if (!s_receiver_running || s_receiver_shutting_down || !s_app.lock)
        return RTCP_ESTATE;
    if (rt_mutex_take(s_app.lock, RT_WAITING_NO) != RT_EOK)
        return RTCP_ETIMEDOUT;
    s_app.siri_pending = true;
    (void)rt_mutex_release(s_app.lock);
    return RTCP_OK;
}
int carplay_app_request_siri(void)
{
    if (atomic_flag_test_and_set_explicit(&s_lifecycle_call,
                                          memory_order_acquire))
        return RTCP_ESTATE;
    int result = request_siri_owned();
    atomic_flag_clear_explicit(&s_lifecycle_call, memory_order_release);
    return result;
}

void carplay_app_set_phone_appearance(rtcp_appearance_setting_t appearance)
{
    if ((unsigned)appearance <= RTCP_APPEARANCE_DARK)
        atomic_store(&s_phone_appearance, appearance);
}

rtcp_appearance_setting_t carplay_app_phone_appearance(void)
{
    return (rtcp_appearance_setting_t)atomic_load(&s_phone_appearance);
}

int carplay_app_set_projection_enabled(bool enabled)
{
    if (!snapshot_enter())
        return RTCP_ESTATE;
    int result = RTCP_ESTATE;
    if (s_receiver_running && !s_receiver_shutting_down && s_app.lock &&
        rt_mutex_take(s_app.lock, RT_WAITING_NO) == RT_EOK)
    {
        if (s_app.airplay_connected)
        {
            s_app.projection_pending = enabled ? 2U : 1U;
            result = RTCP_OK;
        }
        (void)rt_mutex_release(s_app.lock);
    }
    snapshot_leave();
    return result;
}

static int cmd_carplay_projection(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "resume"))
        return carplay_app_set_projection_enabled(true);
    if (argc == 2 && !strcmp(argv[1], "pause"))
        return carplay_app_set_projection_enabled(false);
    rt_kprintf("carplay_projection pause|resume\n");
    return RTCP_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(cmd_carplay_projection, carplay_projection, Request MainScreen focus without disconnecting);

int carplay_app_phone_action(carplay_phone_action_t action)
{
    if (action < CARPLAY_PHONE_BACK || action > CARPLAY_PHONE_NEXT)
        return RTCP_EINVAL;
    if (!snapshot_enter())
        return RTCP_ESTATE;
    int result = RTCP_ESTATE;
    if (s_receiver_running && !s_receiver_shutting_down && s_app.lock &&
        rt_mutex_take(s_app.lock, RT_WAITING_NO) == RT_EOK)
    {
        if (s_app.airplay_connected && s_app.phone_action_pending == 0)
        {
            s_app.phone_action_pending = action;
            result = RTCP_OK;
        }
        (void)rt_mutex_release(s_app.lock);
    }
    snapshot_leave();
    return result;
}

bool carplay_app_phone_media(carplay_phone_media_t *media)
{
    if (!media)
        return false;
    *media = (carplay_phone_media_t){ 0 };
    if (!snapshot_enter())
        return true;
    bool available = true;
    if (s_receiver_running && !s_receiver_shutting_down && s_app.lock)
    {
        available = rt_mutex_take(s_app.lock, RT_WAITING_NO) == RT_EOK;
        if (available)
        {
            *media = s_app.phone_media;
            if (!media->track.title[0] && s_app.iap2_track.title[0])
                media->track = s_app.iap2_track;
            else if (media->track.title[0] && !strcmp(media->track.title, s_app.iap2_track.title) &&
                     (!media->track.artist[0] || !s_app.iap2_track.artist[0] ||
                      !strcmp(media->track.artist, s_app.iap2_track.artist)))
            {
                if (!media->track.has_duration)
                {
                    media->track.has_duration = s_app.iap2_track.has_duration;
                    media->track.duration_ms = s_app.iap2_track.duration_ms;
                }
                if (!media->track.has_elapsed)
                {
                    media->track.has_elapsed = s_app.iap2_track.has_elapsed;
                    media->track.elapsed_ms = s_app.iap2_track.elapsed_ms;
                }
                if (media->track.artwork_revision != s_app.phone_artwork_revision)
                    media->track.artwork_revision = s_app.iap2_track.artwork_revision;
            }
            if (media->track.artwork_revision != s_app.phone_artwork_revision)
                media->track.artwork_revision = 0;
#if defined(RTCARPLAY_IAP2) || defined(RTCARPLAY_USB_HOST)
            for (size_t row = 0; row < CARPLAY_PHONE_MANEUVER_ROWS && media->maneuvers[row].present; ++row)
                for (size_t slot = 0; slot < sizeof(s_app.maneuver_cache) / sizeof(s_app.maneuver_cache[0]); ++slot)
                    if (s_app.maneuver_cache[slot].present && s_app.maneuver_cache[slot].index == media->maneuvers[row].index)
                    {
                        media->maneuvers[row] = s_app.maneuver_cache[slot];
                        break;
                    }
            if (s_app.lanes_hidden ||
                (s_app.has_lane_index && (!s_app.has_lane_payload || s_app.lane_payload_index != s_app.lane_index)))
                media->lane_count = 0;
#endif
            (void)rt_mutex_release(s_app.lock);
        }
    }
    snapshot_leave();
    return available;
}

size_t carplay_app_phone_artwork(uint32_t revision, uint8_t *buffer, size_t capacity)
{
    size_t size = 0;
#if defined(RTCARPLAY_DEMO_LAUNCHER)
    if (!revision || !buffer || !snapshot_enter())
        return 0;
    if (s_receiver_running && !s_receiver_shutting_down && s_app.lock &&
        rt_mutex_take(s_app.lock, RT_WAITING_NO) == RT_EOK)
    {
        if (revision == s_app.phone_artwork_revision && s_app.phone_artwork_size <= capacity &&
            (revision == s_app.phone_media.track.artwork_revision || revision == s_app.iap2_track.artwork_revision))
        {
            size = s_app.phone_artwork_size;
            memcpy(buffer, s_phone_artwork, size);
        }
        (void)rt_mutex_release(s_app.lock);
    }
    snapshot_leave();
#else
    (void)revision;
    (void)buffer;
    (void)capacity;
#endif
    return size;
}
static int cmd_carplay_siri(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int rc = carplay_app_request_siri();
    rt_kprintf("carplay Siri request queued rc=%d\n", rc);
    return rc;
}
MSH_CMD_EXPORT_ALIAS(cmd_carplay_siri, carplay_siri, Request Siri through the encrypted event channel);

#if defined(RTCARPLAY_DEMO_LAUNCHER)
static int cmd_carplay_phone_key(int argc, char **argv)
{
    static const char * const names[] = {
        [CARPLAY_PHONE_BACK] = "back",
        [CARPLAY_PHONE_HOME] = "home",
        [CARPLAY_PHONE_PLAY_PAUSE] = "play-pause",
        [CARPLAY_PHONE_PREVIOUS] = "previous",
        [CARPLAY_PHONE_NEXT] = "next",
    };
    if (argc == 2)
    {
        for (unsigned action = CARPLAY_PHONE_BACK; action < sizeof(names) / sizeof(names[0]); ++action)
        {
            if (strcmp(argv[1], names[action]) == 0)
            {
                int result = carplay_app_phone_action((carplay_phone_action_t)action);
                rt_kprintf("carplay_phone_key: %s rc=%d\n", result == RTCP_OK ? "queued" : "unavailable", result);
                return result;
            }
        }
    }
    rt_kprintf("Usage: carplay_phone_key back|home|play-pause|previous|next\n");
    return RTCP_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(cmd_carplay_phone_key, carplay_phone_key, Queue a CarPlay HID button);
#endif

MSH_CMD_EXPORT_ALIAS(cmd_carplay_start, carplay_start,
                     Start the rt - carplay receiver);
MSH_CMD_EXPORT_ALIAS(cmd_carplay_stop, carplay_stop,
                     Stop the rt - carplay receiver);
MSH_CMD_EXPORT_ALIAS(cmd_carplay_status, carplay_status,
                     Show rt - carplay receiver status);
MSH_CMD_EXPORT_ALIAS(cmd_carplay_touch, carplay_touch,
                     Inject a touch : carplay_touch down | move | up X Y[pointer]);
MSH_CMD_EXPORT_ALIAS(cmd_carplay_randtest, carplay_randtest,
                     Verify the CE TRNG : two calls must differ);
MSH_CMD_EXPORT_ALIAS(cmd_carplay_ctrl, carplay_ctrl,
                     CarPlay Control browse / connect : on | off | connect | status);
MSH_CMD_EXPORT_ALIAS(cmd_carplay_ab, carplay_ab,
                     ControlCipher A / B framing : single | chunked);
#if defined(RTCARPLAY_BT)
MSH_CMD_EXPORT_ALIAS(cmd_carplay_bt, carplay_bt,
                     Bluetooth RFCOMM bring - up : start | stop | status);
#endif

int carplay_app_pairing_storage_error(void)
{
    return atomic_load(&s_pairing_storage_result);
}

void carplay_app_debug_counters(unsigned *received, unsigned *hid_sent, unsigned *hid_failed)
{
    if (received)
        *received = 0;
    if (hid_sent)
        *hid_sent = 0;
    if (hid_failed)
        *hid_failed = 0;
    if (!snapshot_enter())
        return;
    if (received && s_app.lock && rt_mutex_take(s_app.lock, RT_WAITING_NO) == RT_EOK)
    {
        *received = s_app.screen_stats.frames;
        (void)rt_mutex_release(s_app.lock);
    }
    if (hid_sent)
        *hid_sent = atomic_load(&s_app.hid_reports_sent);
    if (hid_failed)
        *hid_failed = atomic_load(&s_app.hid_reports_failed);
    snapshot_leave();
}

/* Called by the low-priority demo owner, never from the network/decoder path.
 * Print common-window deltas automatically, so a cadence report does not
 * require two shell commands with different startup/static intervals. */
extern void ipheth_performance_report(void) __attribute__((weak));
extern void rtcp_lubanlite_usb_probe_report(void) __attribute__((weak));
extern void carplay_usb_mux_probe_report(void) __attribute__((weak));
void carplay_app_performance_report(void)
{
    bool enabled = atomic_load(&s_perf_logging);
    if (enabled)
    {
        if (ipheth_performance_report)
            ipheth_performance_report();
        if (rtcp_lubanlite_usb_probe_report)
            rtcp_lubanlite_usb_probe_report();
        if (carplay_usb_mux_probe_report)
            carplay_usb_mux_probe_report();
    }
#if defined(RTCARPLAY_VIDEO)
    app_state_t *app = &s_app;
    if (!s_receiver_running || !app->lock)
        return;
    if (!enabled)
    {
        app->perf_ms = 0;
        return;
    }
    uint32_t now = (uint32_t)rt_tick_get_millisecond();
    uint32_t dt = now - app->perf_ms;
    if (app->perf_ms && dt < 5000U)
        return;
    rtcp_screen_stats_t rx;
    if (rt_mutex_take(app->lock, RT_WAITING_NO) != RT_EOK)
        return;
    rx = app->screen_stats;
    (void)rt_mutex_release(app->lock);
    if (!rx.frames && !rx.owner_calls)
        return;
    carplay_video_perf_t video;
    carplay_video_sink_perf(&video);
    if (!app->perf_ms)
    {
        app->perf_ms = now;
        app->perf_screen = rx;
        app->perf_video = video;
        return;
    }
    const rtcp_screen_stats_t *old = &app->perf_screen;
    const carplay_video_perf_t *prev = &app->perf_video;
    unsigned rx10 = (unsigned)((uint64_t)(rx.frames - old->frames) * 10000U / dt);
    unsigned show10 = (unsigned)((uint64_t)(video.latched - prev->latched) * 10000U / dt);
    unsigned messages = rx.messages - old->messages;
    unsigned gaps = rx.frame_gap_count - old->frame_gap_count;
    unsigned sender = rx.sender_gap_count - old->sender_gap_count;
    unsigned dequeued = video.dequeued - prev->dequeued;
    unsigned decodes = video.decode_calls - prev->decode_calls;
    unsigned presents = video.present_calls - prev->present_calls;
    /* Keep each call below the SDK's 128-byte console buffer in ordinary
     * operation. Long multi-metric calls truncate diagnostic fields. */
    rt_kprintf("[carplay-perf] dt=%ums rx=%u.%u show=%u.%u KiB/s=%u\n",
               (unsigned)dt, rx10 / 10, rx10 % 10, show10 / 10, show10 % 10,
               (unsigned)((rx.wire_bytes - old->wire_bytes) * 1000U / dt / 1024U));
    rt_kprintf("[carplay-perf] reads=%u batches=%u budget_yields=%u queue=%u peak_total=%u drop=%u err=%u\n",
               rx.recv_calls - old->recv_calls, rx.read_batches - old->read_batches,
               rx.read_budget_yields - old->read_budget_yields, video.pending, video.queue_peak,
               video.drops - prev->drops, video.errors - prev->errors);
    rt_kprintf("[carplay-perf] avg_us rx_gap=%u sender_gap=%u assembly=%u process=%u\n",
               gaps ? (unsigned)((rx.frame_gap_us - old->frame_gap_us) / gaps) : 0,
               sender ? (unsigned)((rx.sender_gap_us - old->sender_gap_us) / sender) : 0,
               messages ? (unsigned)((rx.assembly_us - old->assembly_us) / messages) : 0,
               messages ? (unsigned)((rx.process_us - old->process_us) / messages) : 0);
    rt_kprintf("[carplay-perf] avg_ms queue=%u decode=%u present=%u\n",
               dequeued ? (unsigned)((video.queue_ms - prev->queue_ms) / dequeued) : 0,
               decodes ? (unsigned)((video.decode_ms - prev->decode_ms) / decodes) : 0,
               presents ? (unsigned)((video.present_ms - prev->present_ms) / presents) : 0);
    const carplay_video_window_t *window = &video.window;
    rt_kprintf("[carplay-perf] peak_window=%u queue_ms p95/max=%u/%u decode_us p95/max=%u/%u\n",
               window->peak, window->queue_p95_ms, window->queue_max_ms,
               window->decode_p95_us, window->decode_max_us);
    rt_kprintf("[carplay-perf] packet_waits=%u packet_max_us=%u frame_starves=%u\n",
               window->packet_waits, window->packet_wait_max_us, window->frame_starves);
    rt_kprintf("[carplay-perf] submit_max_us=%u fence_max_us=%u fence_fallbacks=%u\n",
               window->submit_max_us, window->fence_max_us, window->fence_fallbacks);
    rt_kprintf("[carplay-perf] enqueue_max_us alloc/copy/lock_wait/hold=%u/%u/%u/%u\n",
               window->alloc_max_us, window->copy_max_us, window->lock_wait_max_us, window->lock_hold_max_us);
    rt_kprintf("[carplay-perf] pressure_peak=%u deferred=%u discarded=%u filtered=%u\n",
               window->pressure_peak, window->deferred_outputs,
               window->discarded_outputs, window->recovery_filtered);
    rt_kprintf("[carplay-perf] rx_latch_ms p95/max=%u/%u samples=%u\n",
               window->rx_latch_p95_ms, window->rx_latch_max_ms, window->rx_latch_samples);
    if (window->overflows)
    {
        rt_kprintf("[carplay-perf] queue_full count=%u depth=%u oldest_ms=%u\n",
                   window->overflows, window->overflow_depth, window->overflow_oldest_ms);
        rt_kprintf("[carplay-perf] last_overflow packet_waits=%u frame_starves=%u\n",
                   window->overflow_packet_waits, window->overflow_frame_starves);
    }
    rt_kprintf("[screen-wait] owner_calls=%u ge100ms=%u max_total_us=%u\n",
               rx.owner_calls - old->owner_calls, rx.owner_gap_over100ms - old->owner_gap_over100ms,
               rx.owner_gap_max_us);
    rt_kprintf("[screen-wait] partial_gaps=%u ge100ms=%u max_total_us=%u\n",
               rx.partial_gaps - old->partial_gaps, rx.partial_gap_over100ms - old->partial_gap_over100ms,
               rx.partial_gap_max_us);
    rt_kprintf("[screen-wait] recv_ge10ms=%u max_total_us=%u pending_header=%u body_left=%u age_ms=%u\n",
               rx.recv_block_over10ms - old->recv_block_over10ms, rx.recv_block_max_us,
               rx.pending_header_bytes, rx.pending_body_bytes, rx.pending_age_ms);
    app->perf_ms = now;
    app->perf_screen = rx;
    app->perf_video = video;
#endif
}

bool carplay_app_send_iap(const uint8_t *bytes, size_t len)
{
    return rtcp_receiver_send_iap(s_app.receiver, bytes, len);
}

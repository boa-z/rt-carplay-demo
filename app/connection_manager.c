/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Application connection policy and session worker, independent of LVGL. */
#include <rtthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include "connection_manager.h"
#include "carplay_app.h"
#include "carplay_usb.h"
#include "video_sink_lubanlite.h"
#include "iap2_link_lubanlite.h"
#include "rtcp/rtcp.h"

static rt_mutex_t status_lock;
static bool worker_started;
static demo_status_t status;
static bool requested_wireless = DEMO_HAS_WIRELESS;
static atomic_bool automatic = true, wireless_enabled = DEMO_HAS_WIRELESS;
static bool usb_armed;
static uint32_t usb_open_at;
static bool retry_requested;
/* Worker-owned backoff; only copied summaries cross the UI boundary. */
static uint32_t retry_at[2];
static bool retry_waiting[2];
static unsigned failures[2];

static bool retry_due(bool wireless)
{
    unsigned i = wireless ? 1U : 0U;
    return !retry_waiting[i] ||
        (int32_t)(rt_tick_get_millisecond() - retry_at[i]) >= 0;
}

static void connection_finished(bool wireless, int error)
{
    unsigned i = wireless ? 1U : 0U;
    unsigned delay = 1000U;
    if (error) {
        if (failures[i] < 5U) ++failures[i];
        delay = 5000U << (failures[i] - 1U);
        if (delay > 60000U) delay = 60000U;
    } else failures[i] = 0;
    retry_at[i] = rt_tick_get_millisecond() + delay;
    retry_waiting[i] = true;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    if (wireless) status.wireless_error = error;
    else status.usb_error = error;
    if (error) status.last_error = error;
    ++status.reconnects;
    (void)rt_mutex_release(status_lock);
}

static uint32_t pairing_reply_request;
static bool pairing_reply_pending, pairing_reply_accept;
typedef enum { FILE_NONE, FILE_SCAN, FILE_DELETE, FILE_RETRY } file_action_t;
static file_action_t file_action;
static bool manager_open, files_more;
static unsigned file_count, files_revision;
static int files_result;
static carplay_pair_file_t pair_files[CARPLAY_PAIR_FILES_MAX];
static char delete_name[CARPLAY_PAIR_FILENAME_MAX];
static bool automatic_select(bool wireless)
{
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    bool allowed = automatic && !manager_open && file_action == FILE_NONE;
    if (allowed) requested_wireless = wireless;
    (void)rt_mutex_release(status_lock);
    return allowed;
}
#if defined(RTCARPLAY_AUDIO)
static void close_session(void);
static int start_receiver(void);
static void publish(demo_stage_t, int);
static bool audio_open, audio_ready, audio_exit;
static unsigned audio_action; /* 1 play, 2 stop; consumed by demo worker */
static int audio_result;
static audio_test_config_t audio_config={AUDIO_TEST_TONE,48000,0};
static unsigned audio_volume=20;
static bool audio_muted;
/* The session owner alone closes/restarts CarPlay and starts/stops tests. */
static bool audio_service(void)
{
    (void)rt_mutex_take(status_lock,RT_WAITING_FOREVER);
    bool open=audio_open, exit=audio_exit, ready=audio_ready;
    unsigned action=audio_action;audio_action=0;
    audio_test_config_t config=audio_config;
    unsigned volume=audio_volume; bool muted=audio_muted;
    (void)rt_mutex_release(status_lock);
    if(!open)return false;
    if(!ready) {
        close_session();
        (void)rt_mutex_take(status_lock,RT_WAITING_FOREVER);
        audio_ready=true;
        (void)rt_mutex_release(status_lock);
    }
    carplay_audio_test_gain(volume,muted);
    int result=RTCP_OK;
    if(action || exit)result=carplay_audio_test_stop();
    if(result==RTCP_OK && action==1 && !exit)result=carplay_audio_test_start(&config);
    (void)rt_mutex_take(status_lock,RT_WAITING_FOREVER);
    if(action || exit)audio_result=result;
    if(exit && result==RTCP_OK) {
        audio_open=audio_ready=audio_exit=false;manager_open=false;
    }
    (void)rt_mutex_release(status_lock);
    if(exit && result==RTCP_OK && !status.wireless) {
        int start=start_receiver();publish(start==RTCP_OK?DEMO_WAIT:DEMO_FAILED,start);
    }
    return true;
}
#endif

#if defined(RT_USING_FINSH)
#include <finsh.h>
static int connection_command(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "retry")) {
        carplay_connection_retry();
        return RTCP_OK;
    }
    if (argc == 3 && !strcmp(argv[1], "wireless") &&
        (!strcmp(argv[2], "on") || !strcmp(argv[2], "off")))
        return carplay_connection_set_wireless_enabled(!strcmp(argv[2], "on"))
            ? RTCP_OK : RTCP_ESTATE;
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "status"))) {
        rt_kprintf("usage: carplay_connection [status|retry|wireless on|off]\n");
        return RTCP_EINVAL;
    }
    demo_status_t s;
    if (!carplay_connection_status(&s)) return RTCP_ESTATE;
    rt_kprintf("[connection] auto=%u wireless_enabled=%u owner=%s stage=%u "
               "usb_ready=%u attached=%u bt_ready=%u errors=%d/%d "
               "retry_s=%u recovery=%u transport=%u auth=%u session=%u "
               "video=%u audio=%u input=%u\n",
               s.automatic, s.wireless_enabled, s.wireless ? "wireless" : "USB",
               (unsigned)s.stage, s.usb_ready, s.usb, s.bt_ready,
               s.usb_error, s.wireless_error, s.retry_seconds, s.reconnects,
               s.receiver.transport_connected, s.receiver.authenticated,
               s.receiver.session_established, s.receiver.video_ready,
               s.receiver.audio_ready, s.receiver.input_ready);
    return RTCP_OK;
}
MSH_CMD_EXPORT_ALIAS(connection_command, carplay_connection,
                     Automatic connection status/retry/wireless policy);
#endif

static bool mode_changed(void)
{
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    bool changed = requested_wireless != status.wireless;
    (void)rt_mutex_release(status_lock);
    return changed;
}

static int start_receiver(void)
{
    return status.wireless ? carplay_app_autostart() : carplay_app_autostart_wired();
}

static unsigned displayed_frames(void)
{
#if defined(RTCARPLAY_VIDEO)
    return carplay_video_sink_has_presented();
#else
    rtcp_receiver_state_t state;
    return carplay_app_session_state(&state) == RTCP_OK && state.session_established;
#endif
}

static void sample_debug(void)
{
    demo_status_t sample = {0};
    uint32_t now = rt_tick_get_millisecond();
    if (!usb_armed && (int32_t)(now - usb_open_at) >= 0) {
        int error = carplay_usb_open();
        usb_armed = error == RTCP_OK;
        usb_open_at = now + 5000U;
        (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
        status.usb_error = error;
        (void)rt_mutex_release(status_lock);
    }
    sample.usb = usb_armed && carplay_usb_attached();
    carplay_app_wired_info_t endpoint;
    if (carplay_app_is_running() && carplay_app_wired_info(&endpoint) == RTCP_OK)
        snprintf(sample.ipv6, sizeof(sample.ipv6), "%s", endpoint.ipv6);
#if DEMO_HAS_WIRELESS
    if (status.wireless) {
        carplay_app_iap2_info_t info;
        if (carplay_app_iap2_info(&info) == RTCP_OK) {
            snprintf(sample.ssid, sizeof(sample.ssid), "%s", info.ssid);
            snprintf(sample.ipv6, sizeof(sample.ipv6), "%u.%u.%u.%u",
                     info.ipv4[0], info.ipv4[1], info.ipv4[2], info.ipv4[3]);
        }
        carplay_iap2_status_t bt;
        carplay_iap2_get_status(&bt);
        sample.bt_ready = bt.listening; sample.bt_connected = bt.connected;
        sample.bt_identified = bt.identified;
        sample.bt_authenticated = bt.authenticated || bt.mfi_completed;
        sample.wifi_configs = bt.wifi_configs;
        sample.availability = bt.availability; sample.invite_acks = bt.invite_acks;
        sample.invites = bt.invites; sample.reinvites = bt.reinvites;
        rtcp_bt_lubanlite_pairing_status(&sample.pairing);
    }
#endif
#if defined(RTCARPLAY_VIDEO)
    carplay_video_sink_stats(NULL, &sample.frames, &sample.drops, &sample.errors);
#endif
    (void)carplay_app_session_state(&sample.receiver);
    if (!sample.usb) { failures[0] = 0; retry_waiting[0] = false; }
    unsigned remaining = 0;
    unsigned i = status.wireless ? 1U : 0U;
    if (!retry_due(status.wireless))
        remaining = (retry_at[i] - rt_tick_get_millisecond() + 999U) / 1000U;
    carplay_app_debug_counters(&sample.received, &sample.hid_sent, &sample.hid_failed);
    carplay_app_performance_report();
    sample.trust = carplay_usb_trust_cache_status();
    sample.storage_error = carplay_app_pairing_storage_error();
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    status.usb = sample.usb;
    status.usb_ready = usb_armed;
    status.automatic = automatic; status.wireless_enabled = wireless_enabled;
    status.retry_seconds = remaining;
    status.receiver = sample.receiver;
    memcpy(status.ipv6, sample.ipv6, sizeof(status.ipv6));
    memcpy(status.ssid, sample.ssid, sizeof(status.ssid));
    status.bt_ready = sample.bt_ready; status.bt_connected = sample.bt_connected;
    status.bt_identified = sample.bt_identified; status.bt_authenticated = sample.bt_authenticated;
    status.wifi_configs = sample.wifi_configs;
    status.availability = sample.availability; status.invite_acks = sample.invite_acks;
    status.invites = sample.invites; status.reinvites = sample.reinvites;
    status.pairing = sample.pairing;
    status.received = sample.received; status.frames = sample.frames;
    status.drops = sample.drops; status.errors = sample.errors;
    status.hid_sent = sample.hid_sent; status.hid_failed = sample.hid_failed;
    status.trust = sample.trust;
    if (sample.storage_error) status.storage_error = sample.storage_error;
    (void)rt_mutex_release(status_lock);
}

static void publish(demo_stage_t stage, int error)
{
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    if (stage != DEMO_FAILED || status.stage != DEMO_CLOSING) status.previous = status.stage;
    if (stage == DEMO_WAIT) status.storage_error = 0;
    status.stage = stage;
    status.error = error;
    if (error) status.last_error = error;
    if (stage == DEMO_LIVE) {
        unsigned i = status.wireless ? 1U : 0U;
        failures[i] = 0; retry_waiting[i] = false;
        if (status.wireless) status.wireless_error = 0;
        else status.usb_error = 0;
    }
    (void)rt_mutex_release(status_lock);
    rt_kprintf("[carplay-demo] stage=%u error=%d\n", (unsigned)stage, error);
}

/* Leave the hook armed for reconnect. Never stop the receiver while an
 * iAP2 worker still borrows its strings or its TLS carkit stream. */
static void close_session(void)
{
    publish(DEMO_CLOSING, 0);
    while (carplay_usb_close_session() != RTCP_OK)
        rt_thread_mdelay(250);
#if DEMO_HAS_WIRELESS
    /* An asynchronous BT controller startup may exceed one stop timeout.
     * Keep its borrowed receiver state alive until the owner really exits. */
    while (carplay_iap2_stop() != RTCP_OK) rt_thread_mdelay(250);
#endif
    while (carplay_app_stop() != RTCP_OK) rt_thread_mdelay(250);
}

/* Called only with no active USB session. The launcher's single panel
 * reader remains usable even if receiver startup/storage loading failed. */
static bool idle_actions(void)
{
#if defined(RTCARPLAY_AUDIO)
    if(audio_service()) {
        (void)rt_mutex_take(status_lock,RT_WAITING_FOREVER);
        bool closed=!audio_open;
        (void)rt_mutex_release(status_lock);
        return closed;
    }
#endif
    char name[CARPLAY_PAIR_FILENAME_MAX];
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    file_action_t action = file_action;
    file_action = FILE_NONE;
    snprintf(name, sizeof(name), "%s", delete_name);
    (void)rt_mutex_release(status_lock);
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    bool retry = retry_requested || action == FILE_RETRY; retry_requested = false;
    (void)rt_mutex_release(status_lock);
    if (retry) {
        memset(retry_waiting, 0, sizeof(retry_waiting));
        memset(failures, 0, sizeof(failures));
        return true;
    }
    if (action == FILE_NONE) return false;
    int result = RTCP_OK;
    if (action == FILE_DELETE) {
        /* USB is quiescent here. Restart reloads the AirPlay controller table
         * and prevents an old in-memory copy from rewriting a deleted file. */
        result = carplay_app_stop();
        if (result == RTCP_OK) {
#if DEMO_HAS_WIRELESS
            if (!strcmp(name, "Bluetooth bonds")) result = rtcp_bt_lubanlite_forget_bonds();
            else
#endif
                result = carplay_pair_file_forget("/data", name);
        }
        int start = status.wireless ? RTCP_OK : start_receiver();
        if (start != RTCP_OK) { publish(DEMO_FAILED, start); result = start; }
    }
    carplay_pair_file_t files[CARPLAY_PAIR_FILES_MAX];
    unsigned count = 0; bool more = false;
    int scan = carplay_pair_files_scan("/data", files, CARPLAY_PAIR_FILES_MAX, &count, &more);
    if (result == RTCP_OK) result = scan;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    memcpy(pair_files, files, count * sizeof(files[0]));
    file_count = count; files_more = more; files_result = result;
    ++files_revision;
    (void)rt_mutex_release(status_lock);
    return false;
}

/* Reserve the pairing transition while holding the same lock as UI actions. */
static bool claim_connection(void)
{
    bool ready;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    ready = !manager_open && file_action == FILE_NONE && !requested_wireless &&
        (!automatic || retry_due(false));
    if (ready) {
        status.previous = status.stage; status.stage = DEMO_PAIR;
        status.storage_error = 0;
    }
    (void)rt_mutex_release(status_lock);
    return ready;
}

#if DEMO_HAS_WIRELESS
static int wireless_connection(void)
{
    int result = carplay_iap2_start();
    if (result != RTCP_OK) return result;
    bool handover = false, live = false, connecting = false, session_seen = false;
    uint32_t started = rt_tick_get_millisecond(), pairing_started = 0;
    while (!mode_changed() && carplay_app_is_running()) {
        (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
        bool managing = manager_open;
        bool reply = pairing_reply_pending, accept = pairing_reply_accept;
        bool retry = retry_requested; retry_requested = false;
        uint32_t request = pairing_reply_request;
        pairing_reply_pending = false;
        (void)rt_mutex_release(status_lock);
        if (retry) {
            memset(retry_waiting, 0, sizeof(retry_waiting));
            memset(failures, 0, sizeof(failures));
        }
        if (reply) (void)rtcp_bt_lubanlite_pairing_reply(request, accept);
        if (managing) {
            /* Freeze incoming BT/AirPlay pairing before reading/deleting
             * records, and keep it frozen until the modal is closed. */
            close_session();
            for (;;) {
                (void)idle_actions();
                (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
                managing = manager_open || file_action != FILE_NONE;
                (void)rt_mutex_release(status_lock);
                if (!managing || mode_changed()) return RTCP_OK;
                rt_thread_mdelay(100);
            }
        }
        carplay_iap2_status_t bt;
        carplay_iap2_get_status(&bt);
        if (bt.error) return bt.error;
        if (!bt.running) return RTCP_EIO;
        if (!bt.listening && (uint32_t)(rt_tick_get_millisecond() - started) > 60000U)
            return RTCP_ETIMEDOUT;
        carplay_app_iap2_info_t info;
        if (carplay_app_iap2_info(&info) != RTCP_OK) return RTCP_ESTATE;
        /* First observed contender reserves the sole Receiver. USB wins an
         * idle same-tick tie; it never preempts a pairing/handoff/live phone. */
        if (automatic && ((!wireless_enabled) ||
            (!connecting && !handover && !bt.connected && !info.airplay_connected &&
             usb_armed && carplay_usb_attached() && retry_due(false)))) {
            if (automatic_select(false)) return RTCP_OK;
        }
        if (!connecting && (bt.connected || info.airplay_connected)) {
            connecting = true; pairing_started = rt_tick_get_millisecond();
        }
        handover |= bt.authenticated || info.airplay_connected;
        unsigned frames = displayed_frames();
        if (carplay_app_pairing_storage_error()) return RTCP_EIO;
        rtcp_receiver_state_t session;
        bool sampled = carplay_app_session_state(&session) == RTCP_OK;
        if (sampled && session.session_established) session_seen = true;
        if (live && (!info.airplay_connected ||
            (sampled && session_seen && !session.session_established))) return RTCP_OK;
        demo_stage_t stage = frames && info.airplay_connected ? DEMO_LIVE :
            info.airplay_connected ? DEMO_CONNECT : handover ? DEMO_NETWORK :
            bt.connected ? DEMO_PAIR : bt.listening ? DEMO_WAIT : DEMO_BOOT;
        if (stage != status.stage) publish(stage, 0);
        live |= stage == DEMO_LIVE;
        if (connecting && !live &&
            (uint32_t)(rt_tick_get_millisecond() - pairing_started) > 90000U)
            return RTCP_ETIMEDOUT;
        sample_debug();
        rt_thread_mdelay(200);
    }
    return mode_changed() ? RTCP_OK : RTCP_EIO;
}
#endif

static void demo_worker(void *argument)
{
    (void)argument;
    int ret = carplay_usb_open();
    usb_armed = ret == RTCP_OK;
    usb_open_at = rt_tick_get_millisecond() + 5000U;
    if (ret != RTCP_OK) {
        (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
        status.usb_error = ret;
        (void)rt_mutex_release(status_lock);
    }

    for (;;) {
        if (mode_changed()) {
            close_session();
            (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
            status.wireless = requested_wireless;
            status.stage = DEMO_BOOT;
            if (!automatic) status.last_error = 0;
            status.storage_error = 0;
            (void)rt_mutex_release(status_lock);
        }
        if (automatic) {
            (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
            bool managing = manager_open || file_action != FILE_NONE;
            (void)rt_mutex_release(status_lock);
            if (managing) {
                (void)idle_actions(); sample_debug(); rt_thread_mdelay(200);
                continue;
            }
            if (!carplay_app_is_running() && !retry_due(status.wireless)) {
                if (status.stage != DEMO_WAIT) publish(DEMO_WAIT, 0);
                (void)idle_actions(); sample_debug();
                bool attached_timeout = !status.wireless &&
                    status.usb_error == RTCP_ETIMEDOUT && status.usb;
                bool alternate = status.wireless ? usb_armed && carplay_usb_attached() && retry_due(false)
                    : !attached_timeout && wireless_enabled && retry_due(true);
                if (alternate) (void)automatic_select(!status.wireless);
                rt_thread_mdelay(200); continue;
            }
        }
        ret = carplay_app_is_running() ? RTCP_OK : start_receiver();
        if (ret != RTCP_OK) {
            publish(DEMO_FAILED, ret);
            if (automatic) {
                connection_finished(status.wireless, ret);
                (void)automatic_select(!status.wireless && wireless_enabled && retry_due(true));
                while (!mode_changed() && !retry_due(status.wireless) && !idle_actions()) {
                    sample_debug(); rt_thread_mdelay(200);
                }
            } else while (!mode_changed() && !idle_actions()) rt_thread_mdelay(200);
            continue;
        }
        carplay_app_display_info_t display_info;
        (void)carplay_app_display_info(&display_info);
        (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
        snprintf(status.pin, sizeof(status.pin), "%s", display_info.pairing_code);
        (void)rt_mutex_release(status_lock);
#if DEMO_HAS_WIRELESS
        if (status.wireless) {
            ret = wireless_connection();
            sample_debug();
            close_session();
            if (automatic) {
                if (!mode_changed()) connection_finished(true, ret);
                (void)automatic_select(false);
                continue;
            }
            if (ret != RTCP_OK && !mode_changed()) {
                publish(DEMO_FAILED, ret);
                while (!mode_changed() && !idle_actions()) rt_thread_mdelay(200);
            }
            continue;
        }
#endif
        publish(DEMO_WAIT, 0);
        for (;;) {
            if (mode_changed()) break;
            bool retry = idle_actions();
            if (retry) {
                int start = carplay_app_is_running() ? RTCP_OK : start_receiver();
                publish(start == RTCP_OK ? DEMO_WAIT : DEMO_FAILED, start);
            }
            sample_debug();
            if (automatic && wireless_enabled && retry_due(true) &&
                (!usb_armed || !carplay_usb_attached() || !retry_due(false))) {
                if (automatic_select(true)) break;
            }
            if (carplay_app_is_running() && usb_armed && carplay_usb_attached() && claim_connection()) break;
            rt_thread_mdelay(200);
        }
        if (mode_changed()) continue;

        publish(DEMO_PAIR, 0);
        ret = carplay_usb_pair();
        if (ret == RTCP_OK) {
            publish(DEMO_NETWORK, 0);
            /* CarKit iAP2 identification/MFi must run while USB networking
             * comes up. Waiting for IPHETH/DAD before reading the CarKit
             * stream can prevent the phone from progressing its transport. */
            ret = carplay_usb_start_iap2();
            carplay_usb_network_status();
        }
        if (ret == RTCP_OK && !mode_changed()) {
            ret = RTCP_ETIMEDOUT;
            for (unsigned wait = 0; wait < 15000U && carplay_usb_attached() &&
                                      carplay_usb_iap2_running() && !mode_changed(); wait += 100U) {
                carplay_app_wired_info_t endpoint;
                if (carplay_app_wired_info(&endpoint) == RTCP_OK) { ret = RTCP_OK; break; }
                if (wait == 5000U || wait == 10000U) {
                    rt_kprintf("[connection] wired network waiting %u ms (before cleanup)\n", wait);
                    carplay_usb_network_status();
                }
                sample_debug(); rt_thread_mdelay(100);
            }
        }
        if (ret == RTCP_OK && !mode_changed()) {
            publish(DEMO_CONNECT, 0);
        }
        if (ret == RTCP_OK) {
            ret = RTCP_ETIMEDOUT;
            for (unsigned wait = 0; wait < 90000U && carplay_usb_attached() &&
                                      carplay_usb_iap2_running() && !mode_changed(); wait += 200U) {
                sample_debug();
                if (wait == 5000U || wait == 15000U || wait == 30000U) {
                    rt_kprintf("[connection] wired waiting %u ms: TCP=%u authenticated=%u session=%u\n",
                        wait, status.receiver.transport_connected,
                        status.receiver.authenticated, status.receiver.session_established);
                    carplay_usb_network_status();
                }
                if (carplay_app_pairing_storage_error() != RTCP_OK) { ret = RTCP_EIO; break; }
                unsigned frames = displayed_frames();
                if (frames != 0U) { ret = RTCP_OK; break; }
                rt_thread_mdelay(200);
            }
        }
        if (ret == RTCP_OK && !mode_changed()) {
            publish(DEMO_LIVE, 0);
            bool session_seen = false;
            while (carplay_usb_attached() && carplay_usb_iap2_running() &&
                   carplay_app_is_running() && !mode_changed()) {
                rtcp_receiver_state_t state;
                if (carplay_app_session_state(&state) == RTCP_OK) {
                    if (state.session_established) session_seen = true;
                    else if (session_seen) break;
                }
                sample_debug(); rt_thread_mdelay(250);
            }
        }
        rt_kprintf("[connection] wired exit stage=%u rc=%d attached=%u mode_changed=%u\n",
            (unsigned)status.stage, ret, carplay_usb_attached(), mode_changed());
        if (ret != RTCP_OK) carplay_usb_network_status();
        sample_debug();
        close_session();
        if (mode_changed()) continue;
        if (automatic) {
            connection_finished(false, ret);
            /* A trusted phone can remain attached while its network/session
             * is not ready. Retry with the existing bounded backoff instead
             * of power-cycling the wireless stack between every timeout.
             * Other failures and physical removal retain wireless fallback. */
            bool attached_timeout = ret == RTCP_ETIMEDOUT && carplay_usb_attached();
            if (!attached_timeout && wireless_enabled && retry_due(true))
                (void)automatic_select(true);
            continue;
        }
        if (carplay_usb_attached()) {
            /* One attempt per attachment: never spin pairing/key generation
             * against a rejected trust dialog or an incompatible phone. */
            int failed = ret != RTCP_OK ? ret : RTCP_EIO;
            int restart = start_receiver();
            publish(DEMO_FAILED, restart != RTCP_OK ? restart : failed);
            while (carplay_usb_attached() && !mode_changed()) {
                if (idle_actions()) break;
                rt_thread_mdelay(200);
            }
        }
    }
}


int carplay_connection_manager_start(void)
{
    /* The application startup owner calls this once; concurrent init is not
     * supported. A worker failure remains visible through the status API. */
    if (worker_started) return RTCP_OK;
    if (status_lock == NULL) {
        status_lock = rt_mutex_create("cpconn", RT_IPC_FLAG_PRIO);
        if (status_lock == NULL) return RTCP_ENOMEM;
    }
    status.automatic = automatic; status.wireless_enabled = wireless_enabled;
    status.wireless = requested_wireless;
    rt_thread_t worker = rt_thread_create("cpconn", demo_worker, NULL,
                                         24U * 1024U, 21, 10);
    int result = worker == NULL ? RTCP_ENOMEM : RTCP_OK;
    worker_started = worker != NULL;
    if (worker != NULL && rt_thread_startup(worker) != RT_EOK) {
        rt_thread_delete(worker);
        worker_started = false;
        result = RTCP_EIO;
    }
    if (result != RTCP_OK) publish(DEMO_FAILED, result);
    return result;
}

bool carplay_connection_status(demo_status_t *out)
{
    if (out == NULL || status_lock == NULL) return false;
    if (rt_mutex_take(status_lock, RT_WAITING_NO) != RT_EOK) return false;
    *out = status;
    (void)rt_mutex_release(status_lock);
    return true;
}

int carplay_connection_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y)
{
    if (status_lock == NULL) return RTCP_ESTATE;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    int result = status.stage == DEMO_CONNECT || status.stage == DEMO_LIVE
        ? carplay_app_touch(phase, id, x, y) : RTCP_ESTATE;
    (void)rt_mutex_release(status_lock);
    return result;
}

void carplay_connection_select_mode(bool wireless)
{
    if (status_lock == NULL || (wireless && !DEMO_HAS_WIRELESS)) return;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    if (!manager_open && file_action == FILE_NONE &&
        status.stage != DEMO_CLOSING && status.stage != DEMO_LIVE)
    { requested_wireless = wireless; automatic = false; }
    (void)rt_mutex_release(status_lock);
}

bool carplay_connection_set_wireless_enabled(bool enabled)
{
    if (!status_lock || (enabled && !DEMO_HAS_WIRELESS)) return false;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    bool idle = (status.stage == DEMO_WAIT || status.stage == DEMO_FAILED) &&
        !manager_open && file_action == FILE_NONE;
    if (idle) {
        automatic = true; wireless_enabled = enabled;
        status.automatic = true; status.wireless_enabled = enabled;
        retry_requested = true;
    }
    (void)rt_mutex_release(status_lock);
    return idle;
}

void carplay_connection_pairing_reply(uint32_t request, bool accept)
{
    if (status_lock == NULL) return;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    pairing_reply_request = request;
    pairing_reply_accept = accept;
    pairing_reply_pending = true;
    (void)rt_mutex_release(status_lock);
}

void carplay_connection_retry(void)
{
    if (status_lock == NULL) return;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    if ((status.stage == DEMO_WAIT || status.stage == DEMO_FAILED) &&
        !manager_open && file_action == FILE_NONE) {
        retry_requested = true;
        if (!automatic) file_action = FILE_RETRY;
    }
    (void)rt_mutex_release(status_lock);
}

bool carplay_connection_files_open(void)
{
    if (status_lock == NULL) return false;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    bool idle = (status.stage == DEMO_WAIT || status.stage == DEMO_FAILED) && !manager_open;
    if (idle) { manager_open = true; file_action = FILE_SCAN; }
    (void)rt_mutex_release(status_lock);
    return idle;
}

void carplay_connection_files_close(void)
{
    if (status_lock == NULL) return;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    manager_open = false;
    (void)rt_mutex_release(status_lock);
}

void carplay_connection_files_refresh(void)
{
    if (status_lock == NULL) return;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    if (manager_open && file_action == FILE_NONE) file_action = FILE_SCAN;
    (void)rt_mutex_release(status_lock);
}

void carplay_connection_files_forget(const char *name)
{
    if (status_lock == NULL || name == NULL) return;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    if (manager_open && file_action == FILE_NONE) {
        snprintf(delete_name, sizeof(delete_name), "%s", name);
        file_action = FILE_DELETE;
    }
    (void)rt_mutex_release(status_lock);
}

bool carplay_connection_files_snapshot(unsigned *revision,
    carplay_pair_file_t files[CARPLAY_PAIR_FILES_MAX], unsigned *count,
    bool *more, int *result)
{
    if (status_lock == NULL || !revision || !files || !count || !more || !result)
        return false;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    bool changed = *revision != files_revision && manager_open;
    if (changed) {
        *revision = files_revision; *count = file_count;
        *more = files_more; *result = files_result;
        memcpy(files, pair_files, file_count * sizeof(files[0]));
    }
    (void)rt_mutex_release(status_lock);
    return changed;
}

#if defined(RTCARPLAY_AUDIO)
bool carplay_connection_audio_open(void)
{
    if (status_lock == NULL) return false;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    bool idle = (status.stage == DEMO_WAIT || status.stage == DEMO_FAILED) &&
        !manager_open && file_action == FILE_NONE;
    if (idle) {
        manager_open = audio_open = true;
        audio_ready = audio_exit = false; audio_action = 0; audio_result = 0;
    }
    (void)rt_mutex_release(status_lock);
    return idle;
}

void carplay_connection_audio_select(unsigned id)
{
    if (status_lock == NULL) return;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    if (!audio_open) { (void)rt_mutex_release(status_lock); return; }
    if (id < 5) audio_config.mode = (audio_test_mode_t)id;
    else if (id == 10) audio_config.rate = 44100;
    else if (id == 11) audio_config.rate = 48000;
    else if (id >= 20 && id <= 22) audio_config.channel = id - 20;
    else if (id == 30) audio_volume = audio_volume < 10 ? 0 : audio_volume - 10;
    else if (id == 31) audio_volume = audio_volume > 90 ? 100 : audio_volume + 10;
    else if (id == 32) audio_muted = !audio_muted;
    else if (id == 40 && audio_ready && !audio_exit) audio_action = 1;
    else if (id == 41) audio_action = 2;
    else if (id == 42) { audio_exit = true; audio_action = 2; }
    (void)rt_mutex_release(status_lock);
}

void carplay_connection_audio_snapshot(bool *open, bool *ready, bool *exiting,
    audio_test_config_t *config, int *result, audio_test_status_t *test)
{
    if (!open || !ready || !exiting || !config || !result || !test) return;
    *open = *ready = *exiting = false;
    *config = (audio_test_config_t){AUDIO_TEST_TONE, 48000, 0};
    *result = RTCP_ESTATE;
    memset(test, 0, sizeof(*test));
    if (status_lock == NULL) return;
    (void)rt_mutex_take(status_lock, RT_WAITING_FOREVER);
    *open = audio_open; *ready = audio_ready; *exiting = audio_exit;
    *config = audio_config; *result = audio_result;
    (void)rt_mutex_release(status_lock);
    carplay_audio_test_status(test);
}
#endif

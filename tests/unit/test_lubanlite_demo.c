/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Drive the actual application connection manager. UI compiles against SDK LVGL headers;
 * unused UI sections are removed, so this does not test framebuffer rendering. */
#include <assert.h>
#include <setjmp.h>
#ifndef RTCP_LAUNCHER_SOURCE
#error "Define RTCP_LAUNCHER_SOURCE to the host application LVGL launcher"
#endif
#ifndef RTCP_CONNECTION_SOURCE
#error "Define RTCP_CONNECTION_SOURCE to the host application connection manager"
#endif
#include RTCP_CONNECTION_SOURCE
#include RTCP_LAUNCHER_SOURCE

static jmp_buf finished;
static unsigned mode, starts, pairs, closes, stops, net_checks, pictures, failed_waits;
static bool session_live, quiesced, receiver_running;
static bool failed_handoff;
static unsigned reports;
static unsigned now_ms, radio_mode, radio_starts;
static bool fail_mutex, fail_worker, fail_schedule;
static unsigned scheduled, deleted;
rt_mutex_t rt_mutex_create(const char *name, int flags)
{
    (void)name;
    (void)flags;
    return fail_mutex ? NULL : (void *)1;
}
rt_thread_t rt_thread_create(const char *name, void (*entry)(void *), void *arg,
                             unsigned stack, unsigned priority, unsigned tick)
{
    (void)name;
    (void)arg;
    (void)stack;
    (void)priority;
    (void)tick;
    assert(entry == demo_worker);
    return fail_worker ? NULL : (void *)2;
}
int rt_thread_startup(rt_thread_t thread)
{
    assert(thread == (void *)2);
    ++scheduled;
    return fail_schedule ? -1 : RT_EOK;
}
int rt_thread_delete(rt_thread_t thread)
{
    assert(thread == (void *)2);
    ++deleted;
    return RT_EOK;
}
uint32_t rt_tick_get_millisecond(void)
{
    return now_ms;
}
void carplay_app_performance_report(void)
{
}
int carplay_usb_trust_cache_status(void)
{
    return 0;
}
int carplay_app_pairing_storage_error(void)
{
    return mode == 3 && status.stage == DEMO_CONNECT ? RTCP_EIO : 0;
}
void carplay_app_debug_counters(unsigned *r, unsigned *h, unsigned *e)
{
    *r = *h = *e = 0;
}
int rt_kprintf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}
int rt_mutex_take(rt_mutex_t m, int wait)
{
    (void)m;
    (void)wait;
    return 0;
}
int rt_mutex_release(rt_mutex_t m)
{
    (void)m;
    return 0;
}
void rt_thread_mdelay(int ms)
{
    assert(ms > 0);
    now_ms += (unsigned)ms;
    if (status.wireless && radio_mode == 1 && now_ms >= 1000)
        requested_wireless = false;
    if (status.wireless && radio_mode == 3)
    {
        if (status.stage == DEMO_WAIT && now_ms >= 400)
            manager_open = true;
        if (status.stage == DEMO_CLOSING)
            manager_open = false;
    }
    if (status.stage == DEMO_FAILED)
        ++failed_waits;
}
int carplay_usb_open(void)
{
    return 0;
}
bool carplay_usb_attached(void)
{
    if (status.stage == DEMO_LIVE)
        return false; /* unplug after first picture */
    if (status.stage == DEMO_FAILED)
        return failed_waits < 3;
    return true;
}
int carplay_usb_pair(void)
{
    if ((mode == 1 || mode == 3) && pairs == 1)
        longjmp(finished, 1);
    assert(status.stage == DEMO_PAIR && starts == 1);
    ++pairs;
    session_live = true;
    return mode == 1 ? RTCP_EIO : RTCP_OK;
}
int carplay_usb_start_iap2(void)
{
    /* Simulate a phone that starts Ethernet only after CarKit is serviced. */
    assert(net_checks == 0 && status.stage == DEMO_NETWORK);
    return 0;
}
bool carplay_usb_iap2_running(void)
{
    return true;
}
void carplay_usb_network_status(void)
{
}
int carplay_usb_close_session(void)
{
    ++closes;
    if (mode == 2 && closes == 1)
        return RTCP_ETIMEDOUT;
    session_live = false;
    quiesced = true;
    return 0;
}
int carplay_app_autostart_wired(void)
{
    assert(!session_live);
    if (++starts == 2 && mode != 1 && mode != 3)
        longjmp(finished, 1);
    receiver_running = true;
    return 0;
}
int carplay_app_autostart(void)
{
    assert(!session_live);
    receiver_running = true;
    return 0;
}
int carplay_iap2_start(void)
{
    ++radio_starts;
    return 0;
}
int carplay_iap2_stop(void)
{
    return 0;
}
void carplay_iap2_get_status(carplay_iap2_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->running = true;
    out->listening = radio_mode != 5 && now_ms >= 200;
    if (radio_mode == 0 && now_ms >= 600)
    {
        out->error = RTCP_EIO;
        out->running = false;
    }
    if (radio_mode == 2 || radio_mode == 4 || radio_mode == 6)
    {
        out->connected = now_ms >= 400 && now_ms < 800;
        out->identified = out->connected;
        out->authenticated = out->connected && now_ms >= 600;
    }
}
int carplay_app_iap2_info(carplay_app_iap2_info_t *out)
{
    memset(out, 0, sizeof(*out));
    strcpy(out->ssid, "test-carplay");
    out->ipv4[0] = 192;
    out->ipv4[1] = 168;
    out->ipv4[2] = 169;
    out->ipv4[3] = 1;
    out->airplay_connected = radio_mode == 2 && now_ms >= 800 && now_ms < 1400;
    return receiver_running ? RTCP_OK : RTCP_ESTATE;
}
void rtcp_bt_lubanlite_pairing_status(rtcp_bt_pairing_status_t *out)
{
    memset(out, 0, sizeof(*out));
}
int rtcp_bt_lubanlite_pairing_reply(uint32_t request, bool accept)
{
    (void)request;
    (void)accept;
    return RTCP_OK;
}
int rtcp_bt_lubanlite_forget_bonds(void)
{
    return RTCP_OK;
}
int carplay_app_session_state(rtcp_receiver_state_t *out)
{
    memset(out, 0, sizeof(*out));
    out->running = receiver_running;
    out->session_established = receiver_running;
    return receiver_running ? RTCP_OK : RTCP_ESTATE;
}
int carplay_app_display_info(carplay_app_display_info_t *out)
{
    memset(out, 0, sizeof(*out));
    strcpy(out->pairing_code, "3939");
    return RTCP_OK;
}
int carplay_app_wired_info(carplay_app_wired_info_t *info)
{
    (void)info;
    if (status.stage != DEMO_NETWORK)
        return RTCP_ENOENT;
    ++net_checks;
    return net_checks == 1 ? RTCP_ENOENT : RTCP_OK;
}
bool carplay_app_is_running(void)
{
    return receiver_running;
}
int carplay_app_stop(void)
{
    assert(quiesced && !session_live && closes >= (mode == 2 ? 2U : 1U));
    ++stops;
    receiver_running = false;
    return 0;
}
void carplay_video_sink_stats(unsigned *n, unsigned *f, unsigned *d, unsigned *e)
{
    (void)n;
    (void)d;
    (void)e;
    if (status.wireless)
    {
        if (f)
            *f = radio_mode == 2 && now_ms >= 1000 ? 1 : 0;
        if (d)
            *d = 0;
        if (e)
            *e = 0;
        return;
    }
    if (status.stage == DEMO_CONNECT)
        ++pictures;
    if (f)
        *f = pictures >= 2 ? 1 : 0;
    if (d)
        *d = 0;
    if (e)
        *e = 0;
}
bool carplay_video_sink_has_presented(void)
{
    return status.wireless ? radio_mode == 2 && now_ms >= 1000 : pictures >= 2;
}
bool carplay_video_sink_is_running(void)
{
    return receiver_running;
}
bool carplay_launcher_video_is_active(void)
{
    return pictures >= 2 && !failed_handoff;
}
int carplay_app_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y)
{
    (void)phase;
    (void)id;
    (void)x;
    (void)y;
    assert(receiver_running && (status.stage == DEMO_LIVE || status.stage == DEMO_CONNECT));
    ++reports;
    return RTCP_OK;
}
static unsigned test_starts, test_stops;
static int test_stop_error;
int carplay_audio_test_start(const audio_test_config_t *c)
{
    assert(c && !receiver_running && !session_live);
    ++test_starts;
    return 0;
}
int carplay_audio_test_stop(void)
{
    ++test_stops;
    return test_stop_error;
}
void carplay_audio_test_gain(unsigned v, bool m)
{
    (void)v;
    (void)m;
}
void carplay_audio_test_status(audio_test_status_t *s)
{
    memset(s, 0, sizeof(*s));
}
int main(void)
{
    automatic = false;
    requested_wireless = false;
    demo_status_t snapshot;
    assert(!carplay_connection_status(&snapshot));
    fail_mutex = true;
    assert(carplay_connection_manager_start() == RTCP_ENOMEM);
    fail_mutex = false;
    fail_worker = true;
    assert(carplay_connection_manager_start() == RTCP_ENOMEM);
    assert(carplay_connection_status(&snapshot) && snapshot.stage == DEMO_FAILED);
    fail_worker = false;
    fail_schedule = true;
    assert(carplay_connection_manager_start() == RTCP_EIO);
    assert(deleted == 1 && !worker_started);
    fail_schedule = false;
    assert(carplay_connection_manager_start() == RTCP_OK);
    assert(carplay_connection_manager_start() == RTCP_OK && scheduled == 2);
    for (mode = 0; mode < 4; ++mode)
    {
        starts = pairs = closes = stops = net_checks = pictures = failed_waits = 0;
        session_live = quiesced = receiver_running = false;
        memset(&status, 0, sizeof(status));
        if (setjmp(finished) == 0)
            demo_worker(NULL);
        assert(starts == 2 && pairs == 1 && stops == 1);
        assert(closes == (mode == 2 ? 2U : 1U));
        if (mode == 1)
            assert(failed_waits == 3 && net_checks == 0);
        else if (mode == 3)
            assert(failed_waits == 3 && pictures == 2);
        else
            assert(net_checks >= 2 && pictures >= 2);
    }
    /* Touch is delivered to LVGL before picture, and to CarPlay after picture. */
    status_lock = pointer_lock = (void *)1;
    status.stage = DEMO_WAIT;
    pictures = 0;
    receiver_running = true;
    lv_indev_data_t point = { 0 };
    assert(carplay_demo_touch(RTCP_TOUCH_DOWN, 7, 100, 200));
    assert(carplay_demo_touch(RTCP_TOUCH_DOWN, 9, 800, 500)); /* second finger ignored */
    pointer_read(NULL, &point);
    assert(point.state == LV_INDEV_STATE_PRESSED && point.point.x == 100 && point.point.y == 200);
    assert(carplay_demo_touch(RTCP_TOUCH_UP, 7, 120, 220));
    pointer_read(NULL, &point);
    assert(point.state == LV_INDEV_STATE_RELEASED && point.point.x == 120);
    for (unsigned i = 0; i < 80; ++i) assert(carplay_demo_touch(i ? RTCP_TOUCH_MOVE : RTCP_TOUCH_DOWN, 7, i, i));
    assert(carplay_demo_touch(RTCP_TOUCH_UP, 7, 99, 99));
    do
    {
        pointer_read(NULL, &point);
    } while (point.continue_reading);
    assert(point.state == LV_INDEV_STATE_RELEASED && point.point.x == 99);
    manager_open = true;
    assert(!claim_connection());
    manager_open = false;
    file_action = FILE_SCAN;
    assert(!claim_connection());
    file_action = FILE_NONE;
    assert(claim_connection() && status.stage == DEMO_PAIR);
    pictures = 2;
    status.stage = DEMO_LIVE;
    failed_handoff = true;
    launcher_panel_touch(RTCP_TOUCH_DOWN, 7, 10, 10, NULL);
    assert(reports == 0); /* a visible launcher retains touch on failed handoff */
    failed_handoff = false;
    launcher_panel_touch(RTCP_TOUCH_DOWN, 7, 10, 10, NULL);
    assert(reports == 1);
    status.stage = DEMO_CLOSING;
    launcher_panel_touch(RTCP_TOUCH_MOVE, 7, 20, 20, NULL);
    assert(reports == 1); /* closing gates reports before queue teardown */
    assert(!carplay_demo_touch(RTCP_TOUCH_DOWN, 7, 10, 10));
    receiver_running = false;
    assert(carplay_demo_touch(RTCP_TOUCH_DOWN, 7, 10, 10)); /* stopped sink counters can be stale */
    /* Real wireless owner: async BT failure, idle mode switch, Bluetooth
     * handover before video, file manager quiescence and bounded timeouts. */
    mode = 4;
    const int expected[] = { RTCP_EIO, RTCP_OK, RTCP_OK, RTCP_OK, RTCP_ETIMEDOUT, RTCP_ETIMEDOUT, RTCP_ETIMEDOUT };
    for (radio_mode = 0; radio_mode < 7; ++radio_mode)
    {
        memset(&status, 0, sizeof(status));
        status.wireless = requested_wireless = true;
        status.stage = DEMO_BOOT;
        now_ms = radio_starts = 0;
        manager_open = false;
        file_action = FILE_NONE;
        receiver_running = true;
        session_live = false;
        assert(wireless_connection() == expected[radio_mode]);
        assert(radio_starts == 1);
        if (radio_mode == 0)
            assert(status.stage != DEMO_LIVE);
        if (radio_mode == 1)
            assert(status.stage == DEMO_WAIT && mode_changed());
        if (radio_mode == 2)
            assert(status.stage == DEMO_LIVE && now_ms == 1400);
        if (radio_mode == 3)
            assert(!receiver_running && !manager_open);
    }
    /* Audio page reserves connection ownership, stops receiver before sound0,
     * and cannot resume the receiver while a test fails to stop. */
    mode = 4;
    status.wireless = requested_wireless = false;
    status.stage = DEMO_WAIT;
    receiver_running = true;
    session_live = false;
    audio_open = manager_open = true;
    audio_ready = false;
    audio_exit = false;
    audio_action = 1;
    assert(!claim_connection());
    assert(!idle_actions());
    assert(!receiver_running && audio_ready && test_starts == 1);
    audio_exit = true;
    test_stop_error = RTCP_ETIMEDOUT;
    assert(!idle_actions());
    assert(audio_open && manager_open && !receiver_running);
    test_stop_error = 0;
    assert(idle_actions());
    assert(!audio_open && !manager_open && receiver_running);
    assert(status.stage == DEMO_WAIT && test_stops >= 3);
    /* Public UI request/snapshot API reserves ownership under the manager
     * lock. Snapshots remain unchanged after later worker publications. */
    status.stage = DEMO_LIVE;
    carplay_connection_select_mode(true);
    assert(!requested_wireless);
    assert(!carplay_connection_files_open() && !carplay_connection_audio_open());
    status.stage = DEMO_WAIT;
    assert(carplay_connection_status(&snapshot));
    assert(carplay_connection_files_open());
    assert(!carplay_connection_audio_open());
    file_action = FILE_NONE;
    carplay_connection_files_forget("rt-carplay-pairings.bin");
    assert(file_action == FILE_DELETE && !strcmp(delete_name, "rt-carplay-pairings.bin"));
    file_action = FILE_NONE;
    file_count = 1;
    files_revision = 1;
    snprintf(pair_files[0].name, sizeof(pair_files[0].name), "test-pair");
    unsigned revision = 0, count = 0;
    bool more = true;
    int result = 1;
    carplay_pair_file_t files[CARPLAY_PAIR_FILES_MAX];
    assert(carplay_connection_files_snapshot(&revision, files, &count, &more, &result));
    assert(count == 1 && !strcmp(files[0].name, "test-pair"));
    assert(!carplay_connection_files_snapshot(&revision, files, &count, &more, &result));
    carplay_connection_files_close();
    assert(carplay_connection_audio_open());
    assert(!carplay_connection_files_open());
    audio_ready = true;
    carplay_connection_audio_select(40);
    assert(audio_action == 1);
    carplay_connection_audio_select(42);
    assert(audio_exit && audio_action == 2);
    status.stage = DEMO_FAILED;
    assert(snapshot.stage == DEMO_WAIT);
    puts("Actual demo worker: pairing/storage failure, replug, network/first-frame wait, quiescent close, pointer routing/overflow and manager exclusion passed");
    puts("Wireless owner: asynchronous failure, mode switch, BT-to-WiFi handover, first-frame gating, manager pause and timeouts passed");
}

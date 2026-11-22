/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

/* Compile the real composition. Linux ports supply actual entropy, sockets
 * and protocol dependencies; only RT-Thread scheduling/locks are modeled. */
#define LPKG_RT_CARPLAY_LUBANLITE_USB 1
#define RT_USING_LWIP                 1
#ifndef TEST_LEGACY_WIRED_IDENTITY
#define RTCARPLAY_NATIVE_NCM 1
#endif
#define RTCARPLAY_BT 1
#include "../../app/carplay_app.c"
#include "../../app/pairing_files.c"

struct test_mutex
{
    unsigned locked;
};
struct test_thread
{
    void (*entry)(void *);
    void *context;
};
static struct test_thread *pending;
static struct test_thread *current;
static unsigned live_mutexes, thread_deletes, delays;
static bool fail_create, fail_startup, hold_bt_worker;
static bool check_input_reentry;
static bool ipv6_valid = true;
static unsigned ipv6_queries;
static bool wired_mac_available = true;
static uint8_t wired_mac[6] = { 0x9e, 0xc3, 0x94, 0x0c, 0x19, 0xdd };
static pthread_barrier_t snapshot_barrier;
static atomic_bool release_snapshot = ATOMIC_VAR_INIT(false);
static unsigned snapshot_waits;

static void *hold_snapshot(void *unused)
{
    (void)unused;
    assert(snapshot_enter());
    carplay_app_display_info_t display;
    assert(carplay_app_display_info(&display) == RTCP_OK); /* Concurrent reader. */
    unsigned width = s_app.width;
    (void)pthread_barrier_wait(&snapshot_barrier);
    while (!atomic_load(&release_snapshot))
    {
        struct timespec pause = { 0, 1000000 };
        (void)nanosleep(&pause, NULL);
    }
    assert(s_app.width == width); /* Writer has not reset state before retirement. */
    assert(carplay_app_display_info(&display) == RTCP_ESTATE);
    snapshot_leave();
    return NULL;
}

bool rtcp_lubanlite_wired_ipv6(char *address, size_t capacity, bool *valid)
{
    ++ipv6_queries;
    snprintf(address, capacity, "fe80::1234");
    *valid = ipv6_valid;
    return true;
}

bool rtcp_lubanlite_wired_mac(uint8_t mac[6])
{
    if (!wired_mac_available)
    {
        memset(mac, 0, 6);
        return false;
    }
    memcpy(mac, wired_mac, 6);
    return true;
}

static void check_queries_rejected(void)
{
    carplay_app_iap2_info_t wireless;
    carplay_app_wired_info_t wired;
    carplay_app_display_info_t display;
    memset(&wireless, 0xa5, sizeof(wireless));
    memset(&wired, 0xa5, sizeof(wired));
    memset(&display, 0xa5, sizeof(display));
    unsigned before = ipv6_queries;
    assert(carplay_app_iap2_info(&wireless) == RTCP_ESTATE && !wireless.ssid[0]);
    assert(carplay_app_wired_info(&wired) == RTCP_ESTATE && !wired.device_id[0]);
    assert(ipv6_queries == before); /* No platform access while tearing down. */
    assert(carplay_app_display_info(&display) == RTCP_ESTATE && display.width == 0);
    unsigned frames = 42, sent = 42, failed = 42;
    carplay_app_debug_counters(&frames, &sent, &failed);
    assert(!frames && !sent && !failed);
    assert(cmd_carplay_status(0, NULL) == RTCP_ESTATE);
    char *ctrl[] = { "carplay_ctrl", "status" };
    assert(cmd_carplay_ctrl(2, ctrl) == RTCP_ESTATE);
}

static unsigned bt_enables, bt_disconnects, bt_listens;
static int bt_enable_result = RTCP_OK;
static void (*bt_callback)(int, void *);
static int bt_enable(const char *name, const uint8_t *address)
{
    (void)name;
    (void)address;
    ++bt_enables;
    char *stop[] = { "carplay_bt", "stop" };
    assert(cmd_carplay_bt(2, stop) == RTCP_ESTATE); /* Cannot join self. */
    return bt_enable_result;
}
static int bt_listen(uint8_t channel, const char *name, const char *uuid, uint8_t *out)
{
    (void)channel;
    (void)name;
    (void)uuid;
    ++bt_listens;
    *out = 1;
    return RTCP_OK;
}
const struct rtcp_bt rtcp_bt_lubanlite = { .enable = bt_enable, .rfcomm_listen = bt_listen };
void rtcp_bt_lubanlite_set_event_cb(void (*cb)(int, void *), void *user)
{
    (void)user;
    bt_callback = cb;
}
void rtcp_bt_lubanlite_disconnect(void)
{
    ++bt_disconnects;
}
int rtcp_bt_lubanlite_rfcomm_accept(int lfd, int *fd)
{
    (void)lfd;
    (void)fd;
    return RTCP_EIO;
}
int rtcp_bt_lubanlite_recv(int fd, void *buffer, size_t size)
{
    (void)fd;
    (void)buffer;
    (void)size;
    return RTCP_EIO;
}
int rtcp_bt_lubanlite_send(int fd, const void *buffer, size_t size)
{
    (void)fd;
    (void)buffer;
    (void)size;
    return RTCP_EIO;
}
bool rtcp_bt_lubanlite_is_up(void)
{
    return false;
}
bool rtcp_bt_lubanlite_link_up(void)
{
    return false;
}
uint8_t rtcp_bt_lubanlite_channel(void)
{
    return 0;
}
unsigned rtcp_bt_lubanlite_rx_dropped(void)
{
    return 0;
}
void rtcp_bt_lubanlite_pairing_status(rtcp_bt_pairing_status_t *out)
{
    memset(out, 0, sizeof(*out));
}

bool rtcp_lubanlite_random(uint8_t *data, size_t length)
{
    return rtcp_os_rtthread.random(data, length);
}
void rtcp_net_rtthread_multicast_interface(const uint8_t ipv4[4])
{
    (void)ipv4;
}
int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}
uint32_t rt_tick_get_millisecond(void)
{
    return (uint32_t)rtcp_os_rtthread.now_ms();
}
rt_mutex_t rt_mutex_create(const char *name, int flags)
{
    (void)name;
    (void)flags;
    struct test_mutex *mutex = calloc(1, sizeof(*mutex));
    assert(mutex);
    ++live_mutexes;
    return mutex;
}
int rt_mutex_take(rt_mutex_t mutex, int timeout)
{
    (void)timeout;
    assert(mutex && !mutex->locked);
    if (check_input_reentry)
    {
        carplay_app_display_info_t display;
        assert(carplay_app_display_info(&display) == RTCP_OK);
        assert(carplay_app_pairing_storage_error() == RTCP_OK);
        assert(carplay_app_stop() == RTCP_ESTATE);
        assert(carplay_app_touch(RTCP_TOUCH_DOWN, 0, 10, 20) == RTCP_ESTATE);
        assert(carplay_app_request_siri() == RTCP_ESTATE);
        char *args[] = { "carplay_ab", "single" };
        assert(cmd_carplay_ab(2, args) == RTCP_ESTATE);
        char *ctrl[] = { "carplay_ctrl", "off" };
        assert(cmd_carplay_ctrl(2, ctrl) == RTCP_ESTATE);
    }
    mutex->locked = 1;
    return RT_EOK;
}
int rt_mutex_release(rt_mutex_t mutex)
{
    assert(mutex && mutex->locked);
    mutex->locked = 0;
    return RT_EOK;
}
int rt_mutex_delete(rt_mutex_t mutex)
{
    assert(mutex && !mutex->locked && live_mutexes);
    check_queries_rejected();
    if (current)
        assert(s_receiver_running && s_receiver_worker_live);
    assert(carplay_app_stop() == RTCP_ESTATE); /* Teardown reentry. */
    assert(carplay_app_touch(RTCP_TOUCH_DOWN, 0, 10, 20) == RTCP_ESTATE);
    assert(carplay_app_request_siri() == RTCP_ESTATE);
    char *args[] = { "carplay_ab", "single" };
    assert(cmd_carplay_ab(2, args) == RTCP_ESTATE);
    --live_mutexes;
    free(mutex);
    return RT_EOK;
}
rt_thread_t rt_thread_create(const char *name, void (*entry)(void *), void *context,
                             unsigned stack, unsigned priority, unsigned tick)
{
    (void)name;
    (void)stack;
    (void)priority;
    (void)tick;
    if (fail_create)
        return NULL;
    struct test_thread *thread = malloc(sizeof(*thread));
    assert(thread);
    thread->entry = entry;
    thread->context = context;
    return thread;
}
int rt_thread_startup(rt_thread_t thread)
{
    assert(thread && !pending);
    if (thread->entry == poll_thread)
    {
        assert(s_receiver_worker_live);            /* Ownership precedes first scheduling. */
        assert(carplay_app_stop() == RTCP_ESTATE); /* Startup reentry. */
    }
    else
    {
        assert(thread->entry == bt_serve_thread && s_bt_thread_running);
        char *stop[] = { "carplay_bt", "stop" };
        assert(cmd_carplay_bt(2, stop) == RTCP_ESTATE); /* Start serialized. */
    }
    if (fail_startup)
        return -1;
    pending = thread;
    return RT_EOK;
}
int rt_thread_delete(rt_thread_t thread)
{
    assert(thread && thread != pending);
    ++thread_deletes;
    free(thread);
    return RT_EOK;
}
rt_thread_t rt_thread_self(void)
{
    return current;
}
void rt_thread_mdelay(int milliseconds)
{
    if (milliseconds == 1)
    {
        assert(atomic_load(&s_snapshot_readers) & SNAPSHOT_WRITER);
        ++snapshot_waits;
        atomic_store(&release_snapshot, true);
        struct timespec pause = { 0, 1000000 };
        (void)nanosleep(&pause, NULL);
        return;
    }
    assert(milliseconds > 0 && ++delays < 1000);
    if (hold_bt_worker && pending && pending->entry == bt_serve_thread)
        return;
    /* Stop before first scheduling: execute the real worker, which must
     * still own all its state/resources until its last access. */
    assert(pending);
    if (pending->entry == poll_thread)
        assert(s_receiver_stop && s_receiver_worker_live && s_receiver_running);
    else
        assert(!s_bt_serve && s_bt_thread_running);
    struct test_thread *thread = pending;
    pending = NULL;
    current = thread;
    thread->entry(thread->context);
    current = NULL;
    assert(!s_receiver_worker_live && !s_receiver_running);
    free(thread);
}

static pthread_barrier_t contenders;
static atomic_uint winners = ATOMIC_VAR_INIT(0);
static void *claim_bt(void *context)
{
    carplay_bt_owner_t owner = *(carplay_bt_owner_t *)context;
    for (unsigned round = 0; round < 64; ++round)
    {
        (void)pthread_barrier_wait(&contenders);
        bool won = carplay_bt_claim(owner);
        if (won)
            atomic_fetch_add(&winners, 1);
        (void)pthread_barrier_wait(&contenders);
        assert(atomic_load(&winners) == 1);
        if (won)
            carplay_bt_release(owner);
        (void)pthread_barrier_wait(&contenders);
        if (owner == CARPLAY_BT_OWNER_ECHO)
            atomic_store(&winners, 0);
        (void)pthread_barrier_wait(&contenders);
    }
    return NULL;
}

/* Actual DNS SRV packet, using the sender address fallback. This exercises
 * the real Browser parser and copied endpoint lifetime, not a success stub. */
static void test_control_commands(void)
{
    static const uint8_t reply[] = {
        0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 0,
        5, 'p', 'h', 'o', 'n', 'e', 13, '_', 'c', 'a', 'r', 'p', 'l', 'a', 'y', '-', 'c', 't', 'r', 'l',
        4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0,
        0, 33, 0, 1, 0, 0, 0, 120, 0, 20,
        0, 0, 0, 0, 0xc8, 0x22, 6, 'i', 'p', 'h', 'o', 'n', 'e', 5, 'l', 'o', 'c', 'a', 'l', 0
    };
    const uint8_t peer[] = { 192, 168, 43, 2 };
    uint32_t now = (uint32_t)s_app.os.now_ms();
    assert(s_app.browser == NULL); /* Wired configuration. */
    char *on[] = { "carplay_ctrl", "on" };
    char *off[] = { "carplay_ctrl", "off" };
    char *connect[] = { "carplay_ctrl", "connect" };
    char *status_args[] = { "carplay_ctrl", "status" };
    char *invalid[] = { "carplay_ctrl", "invalid" };
    assert(cmd_carplay_ctrl(2, status_args) == RTCP_OK);
    assert(cmd_carplay_ctrl(2, on) == RTCP_ESTATE);
    assert(cmd_carplay_ctrl(2, invalid) == RTCP_EINVAL);
    assert(cmd_carplay_ctrl(3, status_args) == RTCP_EINVAL);
    s_app.browser = mdns_browser_create(CARPLAY_CTRL_SERVICE);
    assert(s_app.browser);
    assert(mdns_browser_handle_packet(s_app.browser, reply, sizeof(reply), peer, now) > 0);
    publish_control_status(&s_app, now);
    assert(s_app.ctrl_status.available && s_app.ctrl_status.has_endpoint);
    assert(!strcmp(s_app.ctrl_status.endpoint.instance, "phone._carplay-ctrl._tcp.local"));
    assert(s_app.ctrl_status.endpoint.port == 51234);
    assert(cmd_carplay_ctrl(2, on) == RTCP_OK);
    assert(!s_app.ctrl_enabled && s_app.ctrl_enable_pending); /* Queue only. */
    drain_receiver_commands(&s_app);
    assert(s_app.ctrl_enabled && !s_app.ctrl_enable_pending);
    publish_control_status(&s_app, now);
    assert(s_app.ctrl_status.enabled);
    assert(cmd_carplay_ctrl(2, off) == RTCP_OK);
    assert(s_app.ctrl_enabled); /* Still applied, with a pending off request. */
    assert(cmd_carplay_ctrl(2, status_args) == RTCP_OK);
    assert(s_app.ctrl_enable_pending && !s_app.ctrl_requested_enabled);
    drain_receiver_commands(&s_app);
    assert(!s_app.ctrl_enabled);
    s_app.ctrl_last_attempt_ms = now;
    assert(cmd_carplay_ctrl(2, connect) == RTCP_OK);
    assert(s_app.ctrl_last_attempt_ms == now && s_app.ctrl_connect_pending);
    uint32_t before = (uint32_t)s_app.os.now_ms();
    drain_receiver_commands(&s_app);
    uint32_t after = (uint32_t)s_app.os.now_ms();
    assert(!s_app.ctrl_connect_pending);
    uint32_t requested_at = s_app.ctrl_last_attempt_ms + CARPLAY_CTRL_RETRY_MS;
    assert((uint32_t)(requested_at - before) <= (uint32_t)(after - before));
    assert(!s_app.ctrl_enabled); /* Connect does not implicitly enable. */
    control_status_t copy = s_app.ctrl_status;
    mdns_browser_destroy(s_app.browser);
    s_app.browser = NULL;
    assert(cmd_carplay_ctrl(2, status_args) == RTCP_OK); /* Copied, no Browser dereference. */
    assert(!memcmp(&copy, &s_app.ctrl_status, sizeof(copy)));
    publish_control_status(&s_app, now);
    assert(!s_app.ctrl_status.available && !s_app.ctrl_status.has_endpoint);
    assert(copy.has_endpoint && copy.endpoint.ipv4[3] == 2);
}

static void run_bt_worker(void)
{
    struct test_thread *thread = pending;
    assert(thread && thread->entry == bt_serve_thread);
    pending = NULL;
    current = thread;
    thread->entry(thread->context);
    current = NULL;
    free(thread);
    assert(!s_bt_thread_running && !s_bt_serve && !s_bt_thread && !bt_callback);
    assert(carplay_bt_owner() == CARPLAY_BT_OWNER_NONE);
}
static void test_bt_lifetime(void)
{
    char *start[] = { "carplay_bt", "start" };
    char *stop[] = { "carplay_bt", "stop" };
    fail_create = true;
    assert(cmd_carplay_bt(2, start) == RTCP_ENOMEM);
    assert(!s_bt_thread_running && !s_bt_thread && !bt_callback);
    assert(carplay_bt_owner() == CARPLAY_BT_OWNER_NONE);
    fail_create = false;
    fail_startup = true;
    assert(cmd_carplay_bt(2, start) == RTCP_EIO);
    assert(!s_bt_thread_running && !s_bt_thread && !bt_callback);
    assert(carplay_bt_owner() == CARPLAY_BT_OWNER_NONE);
    fail_startup = false;
    assert(cmd_carplay_bt(2, start) == RTCP_OK && pending);
    assert(s_bt_thread_running && carplay_bt_owner() == CARPLAY_BT_OWNER_ECHO);
    hold_bt_worker = true;
    assert(cmd_carplay_bt(2, stop) == RTCP_ETIMEDOUT);
    assert(pending && s_bt_thread_running && carplay_bt_owner() == CARPLAY_BT_OWNER_ECHO);
    hold_bt_worker = false;
    assert(cmd_carplay_bt(2, stop) == RTCP_OK); /* Before first scheduling. */
    assert(!pending && !s_bt_thread_running && !s_bt_thread && !bt_callback);
    assert(!bt_enables && !bt_listens); /* Stop cannot create a late listener. */
    unsigned disconnected = bt_disconnects;
    assert(carplay_bt_claim(CARPLAY_BT_OWNER_IAP2));
    assert(cmd_carplay_bt(2, stop) == RTCP_OK && bt_disconnects == disconnected);
    carplay_bt_release(CARPLAY_BT_OWNER_IAP2);
    bt_enable_result = RTCP_EIO;
    assert(cmd_carplay_bt(2, start) == RTCP_OK);
    run_bt_worker();
    assert(bt_enables == 1 && !bt_listens);
    bt_enable_result = RTCP_OK;
    assert(cmd_carplay_bt(2, start) == RTCP_OK);
    run_bt_worker();
    assert(bt_enables == 2 && bt_listens == 1);
    delays = 0;
}

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static void test_viewport_metadata(void)
{
    app_state_t *app = calloc(1, sizeof(*app));
    assert(app);
    strcpy(app->device_id, "02:00:00:00:00:01");
    strcpy(app->bluetooth_id, "02:00:00:00:00:02");
    strcpy(app->name, "Host display test");
    strcpy(app->model, "Host test");
    for (unsigned i = 0; i < 2; ++i)
    {
        app->width = i ? 1024 : 768;
        app->height = 600;
        app->max_fps = i ? 60 : 30;
        assert(build_announcement_data(app) == RTCP_OK);
        airplay_bplist_doc_t doc;
        airplay_bplist_object_t root, displays, display, object;
        uint64_t value;
        assert(airplay_bplist_open(app->info_plist, app->info_plist_len, &doc) == AIRPLAY_BPLIST_OK);
        assert(airplay_bplist_root(&doc, &root) == AIRPLAY_BPLIST_OK);
        assert(airplay_bplist_dict_get(&doc, root, "displays", &displays) == AIRPLAY_BPLIST_OK);
        assert(airplay_bplist_array_get(&doc, displays, 0, &display) == AIRPLAY_BPLIST_OK);
        const char *keys[] = { "widthPixels", "heightPixels", "widthPhysical", "heightPhysical", "maxFPS" };
        const uint64_t expected[] = { app->width, 600, i ? 200 : 150, 120, app->max_fps };
        for (unsigned k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k)
        {
            assert(airplay_bplist_dict_get(&doc, display, keys[k], &object) == AIRPLAY_BPLIST_OK);
            assert(airplay_bplist_get_uint(&doc, object, &value) == AIRPLAY_BPLIST_OK && value == expected[k]);
        }
    }
    free(app);
}
#endif
int main(void)
{
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    test_viewport_metadata();
#endif
    test_bt_lifetime();
    thread_deletes = 0;
    char directory[] = "/tmp/rtcp-rtthread-lifecycle-XXXXXX";
    assert(mkdtemp(directory));
    char seed[256], store[256];
    assert(snprintf(seed, sizeof(seed), "%s/identity", directory) > 0);
    assert(snprintf(store, sizeof(store), "%s/pairs", directory) > 0);
    carplay_app_config_t config;
    carplay_app_config_defaults(&config);
    config.wired = true;
    config.seed_path = seed;
    config.store_path = store;
    config.width_pixels = 1024;
    config.height_pixels = 600;

#ifdef RTCARPLAY_NATIVE_NCM
    wired_mac_available = false;
    assert(carplay_app_start(&config) == RTCP_ENOENT);
    assert(!carplay_app_is_running() && !pending && !live_mutexes);
    assert(access(seed, F_OK) != 0); /* No key generation before net readiness. */
    wired_mac_available = true;
#endif

    fail_startup = true;
    assert(carplay_app_start(&config) == RTCP_EIO);
    assert(!carplay_app_is_running() && !pending && live_mutexes == 0);
    assert(thread_deletes == 1);
    assert(carplay_app_stop() == RTCP_OK);

    fail_startup = false;
    fail_create = true;
    assert(carplay_app_start(&config) == RTCP_ENOMEM);
    assert(!carplay_app_is_running() && !pending && live_mutexes == 0);
    fail_create = false;

    app_state_t wireless_identity = { 0 };
    wireless_identity.os = rtcp_os_rtthread;
    assert(build_identity(&wireless_identity, "3939", seed) == RTCP_OK);
    assert(!strncmp(wireless_identity.device_id, "02:", 3));

    for (unsigned cycle = 0; cycle < 32; ++cycle)
    {
        assert(carplay_app_start(&config) == RTCP_OK);
        assert(carplay_app_is_running() && pending && live_mutexes == 1);
#ifdef RTCARPLAY_NATIVE_NCM
        char expected_id[18];
        snprintf(expected_id, sizeof(expected_id), "%02X:%02X:%02X:%02X:%02X:%02X",
                 wired_mac[0], wired_mac[1], wired_mac[2], wired_mac[3],
                 wired_mac[4], wired_mac[5]);
        assert(!strcmp(s_app.device_id, expected_id));
#else
        assert(!strncmp(s_app.device_id, "02:", 3));
#endif
        /* Check the bytes exposed by /info and both pairing methods, not
         * only the local string subsequently copied into StartSession. */
        airplay_bplist_doc_t doc;
        airplay_bplist_object_t root, object;
        char encoded_id[32];
        size_t encoded_id_len;
        assert(airplay_bplist_open(s_app.info_plist, s_app.info_plist_len, &doc) == AIRPLAY_BPLIST_OK);
        assert(airplay_bplist_root(&doc, &root) == AIRPLAY_BPLIST_OK);
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
        assert(s_app.vehicle_demo.model);
        assert(airplay_info_plist_matches_vehicle(s_app.info_plist, s_app.info_plist_len, s_app.vehicle_demo.model));
        rtcp_vehicle_delivery_status_t vehicle_delivery;
        assert(rtcp_receiver_get_vehicle_delivery_status(s_app.receiver, &vehicle_delivery) == RTCP_OK);
        assert(vehicle_delivery.attached);
        rtcp_data_stream_status_t vehicle_lane;
        assert(rtcp_receiver_get_data_stream_status(s_app.receiver, RTCP_DATA_STREAM_VEHICLE, &vehicle_lane) == RTCP_OK);
        assert(vehicle_lane.registered && !vehicle_lane.offered && !vehicle_lane.enabled);
        assert(rtcp_receiver_get_data_stream_status(s_app.receiver, RTCP_DATA_STREAM_UI_SYNC, &vehicle_lane) == RTCP_OK);
        assert(!vehicle_lane.registered && !vehicle_lane.enabled);
        assert(carplay_vehicle_protocol_demo_tick(&s_app.vehicle_demo, 1250) == RTCP_OK);
        assert(!airplay_info_plist_matches_vehicle(s_app.info_plist, s_app.info_plist_len, s_app.vehicle_demo.model));
        const uint8_t *refreshed;
        size_t refreshed_length;
        assert(refresh_info_plist(&refreshed, &refreshed_length, &s_app));
        assert(airplay_info_plist_matches_vehicle(refreshed, refreshed_length, s_app.vehicle_demo.model));
        assert(airplay_bplist_open(s_app.info_plist, s_app.info_plist_len, &doc) == AIRPLAY_BPLIST_OK);
        assert(airplay_bplist_root(&doc, &root) == AIRPLAY_BPLIST_OK);
#else
        assert(airplay_bplist_dict_get(&doc, root, "vehicleStateProtocolInfo", &object) == AIRPLAY_BPLIST_NOT_FOUND);
#endif
        assert(airplay_bplist_dict_get(&doc, root, "uiSyncInfo", &object) == AIRPLAY_BPLIST_NOT_FOUND);
        assert(airplay_bplist_dict_get(&doc, root, "displayPanels", &object) == AIRPLAY_BPLIST_NOT_FOUND);
        assert(airplay_bplist_dict_get(&doc, root, "deviceID", &object) == AIRPLAY_BPLIST_OK);
        assert(airplay_bplist_get_string(&doc, object, encoded_id, sizeof(encoded_id), &encoded_id_len) == AIRPLAY_BPLIST_OK);
        assert(!strcmp(encoded_id, s_app.device_id));
        assert(encoded_id_len == strlen(encoded_id));
        assert(!memcmp(&s_app.keypair, &wireless_identity.keypair, sizeof(s_app.keypair)));
        rtcp_pairing_provider_t pairing;
        fill_pairing(&s_app, &pairing);
        assert(pairing.pair_setup.accessory_identifier_len == strlen(encoded_id));
        assert(!memcmp(pairing.pair_setup.accessory_identifier, encoded_id, strlen(encoded_id)));
        assert(pairing.pair_verify.accessory_identifier_len == strlen(encoded_id));
        assert(!memcmp(pairing.pair_verify.accessory_identifier, encoded_id, strlen(encoded_id)));
        airplay_ed25519_keypair_t persisted_key = s_app.keypair;
        wired_mac[5] ^= 1U; /* A later platform change cannot rewrite live identity. */
        assert(carplay_app_start(&config) == RTCP_ESTATE);
        carplay_app_display_info_t display;
        assert(carplay_app_display_info(&display) == RTCP_OK);
        assert(display.width == 1024 && display.height == 600);
        assert(!strcmp(display.pairing_code, "3939"));
        rtcp_receiver_state_t published;
        assert(carplay_app_session_state(NULL) == RTCP_EINVAL);
        s_app.receiver_state = (rtcp_receiver_state_t){ .running = true,
                                                        .transport_connected = true,
                                                        .authenticated = true,
                                                        .generation = cycle + 1 };
        assert(carplay_app_session_state(&published) == RTCP_OK);
        assert(published.running && published.authenticated && !published.session_established);
        memset(&s_app.receiver_state, 0, sizeof(s_app.receiver_state));
        assert(published.generation == cycle + 1 && published.transport_connected);

        carplay_app_wired_info_t wired;
        ipv6_valid = false;
        assert(carplay_app_wired_info(&wired) == RTCP_ENOENT);
        ipv6_valid = true;
        assert(carplay_app_wired_info(&wired) == RTCP_OK);
        assert(!strcmp(wired.ipv6, "fe80::1234"));
        assert(!strcmp(wired.device_id, s_app.device_id));
        assert(!strcmp(wired.device_id, encoded_id));
        assert(!memcmp(&s_app.keypair, &persisted_key, sizeof(persisted_key)));
        assert(wired.device_id != s_app.device_id);
        /* Exercise wireless snapshot ownership without claiming radio behavior. */
        s_app.wired = false;
        carplay_app_iap2_info_t wireless;
        assert(carplay_app_iap2_info(&wireless) == RTCP_OK);
        assert(!strcmp(wireless.ssid, s_app.ssid) && wireless.ssid != s_app.ssid);
        assert(!strcmp(wireless.public_key_hex, wired.public_key_hex));
        s_app.wired = true;
        if (cycle == 0)
            test_control_commands();
        check_input_reentry = true;
        assert(carplay_app_touch(RTCP_TOUCH_DOWN, 0, 10, 20) == RTCP_OK);
        assert(carplay_app_request_siri() == RTCP_OK);
        check_input_reentry = false;
        char *single[] = { "carplay_ab", "single" };
        char *chunked[] = { "carplay_ab", "chunked" };
        assert(cmd_carplay_ab(2, single) == RTCP_OK);
        rtcp_receiver_diagnostics_t diagnostics;
        assert(rtcp_receiver_get_diagnostics(s_app.receiver, &diagnostics) == RTCP_OK);
        assert(!diagnostics.single_cipher_frame); /* Shell only queued it. */
        drain_receiver_commands(&s_app);          /* Execute on the modeled owner. */
        assert(rtcp_receiver_get_diagnostics(s_app.receiver, &diagnostics) == RTCP_OK);
        assert(diagnostics.single_cipher_frame && s_app.cipher_applied && !s_app.cipher_pending);
        assert(cmd_carplay_ab(2, chunked) == RTCP_OK);
        drain_receiver_commands(&s_app);
        assert(rtcp_receiver_get_diagnostics(s_app.receiver, &diagnostics) == RTCP_OK);
        assert(!diagnostics.single_cipher_frame && !s_app.cipher_applied);
        assert(cmd_carplay_ab(2, single) == RTCP_OK); /* Pending request lost at stop. */
        current = pending;
        assert(carplay_app_stop() == RTCP_ESTATE); /* Cannot join self. */
        current = NULL;
        pthread_t reader;
        if (cycle == 0)
        {
            assert(pthread_barrier_init(&snapshot_barrier, NULL, 2) == 0);
            assert(pthread_create(&reader, NULL, hold_snapshot, NULL) == 0);
            (void)pthread_barrier_wait(&snapshot_barrier);
        }
        assert(carplay_app_stop() == RTCP_OK);
        if (cycle == 0)
        {
            assert(pthread_join(reader, NULL) == 0);
            assert(snapshot_waits > 0);
            assert(pthread_barrier_destroy(&snapshot_barrier) == 0);
        }
        assert(!carplay_app_is_running() && !pending && live_mutexes == 0);
        assert(s_app.thread == NULL);
#if defined(RTCARPLAY_VEHICLE_PROTOCOL_DEMO)
        assert(!s_app.vehicle_demo.model); /* Destroyed after borrowed Receiver. */
#endif
        assert(cmd_carplay_ab(2, single) == RTCP_ESTATE);
        memset(s_app.device_id, 0, sizeof(s_app.device_id));
        memset(s_app.ssid, 0, sizeof(s_app.ssid));
        memset(s_app.pairing_code, 0, sizeof(s_app.pairing_code));
        assert(wired.device_id[0] && wireless.device_id[0] && wireless.ssid[0]);
        assert(!strcmp(display.pairing_code, "3939"));
        rtcp_receiver_state_t state = published;
        assert(carplay_app_session_state(&state) == RTCP_ESTATE);
        assert(!state.running && !state.transport_connected && !state.generation);

        assert(carplay_app_touch(RTCP_TOUCH_UP, 0, 10, 20) == RTCP_ESTATE);
        assert(carplay_app_request_siri() == RTCP_ESTATE);
        assert(carplay_app_stop() == RTCP_OK);
    }
    assert(unlink(seed) == 0);
    assert(rmdir(directory) == 0);
    carplay_bt_owner_t owners[] = { CARPLAY_BT_OWNER_ECHO, CARPLAY_BT_OWNER_IAP2 };
    assert(!carplay_bt_claim(CARPLAY_BT_OWNER_NONE));
    assert(carplay_bt_claim(CARPLAY_BT_OWNER_ECHO));
    carplay_bt_release(CARPLAY_BT_OWNER_IAP2);
    assert(carplay_bt_owner() == CARPLAY_BT_OWNER_ECHO);
    carplay_bt_release(CARPLAY_BT_OWNER_ECHO);
    pthread_t threads[2];
    assert(pthread_barrier_init(&contenders, NULL, 2) == 0);
    for (unsigned i = 0; i < 2; ++i)
        assert(pthread_create(&threads[i], NULL, claim_bt, &owners[i]) == 0);
    for (unsigned i = 0; i < 2; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(pthread_barrier_destroy(&contenders) == 0);
    assert(carplay_bt_owner() == CARPLAY_BT_OWNER_NONE);
    puts("actual RT-Thread receiver startup/stop rollback passed");
    return 0;
}

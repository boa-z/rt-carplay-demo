/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* Compile the real composition. Linux ports supply actual entropy, sockets
 * and protocol dependencies; only RT-Thread scheduling/locks are modeled. */
#include "../../app/carplay_app.c"
#include "../../app/pairing_files.c"

struct test_mutex { unsigned locked; };
struct test_thread { void (*entry)(void *); void *context; };
static struct test_thread *pending;
static struct test_thread *current;
static unsigned live_mutexes, thread_deletes, delays;
static bool fail_create, fail_startup;
static bool check_input_reentry;

bool rtcp_lubanlite_random(uint8_t *data, size_t length)
{
    return rtcp_os_rtthread.random(data, length);
}
void rtcp_net_rtthread_multicast_interface(const uint8_t ipv4[4]) { (void)ipv4; }
int rt_kprintf(const char *format, ...) { (void)format; return 0; }
uint32_t rt_tick_get_millisecond(void) { return (uint32_t)rtcp_os_rtthread.now_ms(); }
rt_mutex_t rt_mutex_create(const char *name, int flags)
{
    (void)name; (void)flags;
    struct test_mutex *mutex = calloc(1, sizeof(*mutex));
    assert(mutex); ++live_mutexes; return mutex;
}
int rt_mutex_take(rt_mutex_t mutex, int timeout)
{
    (void)timeout; assert(mutex && !mutex->locked);
    if (check_input_reentry) {
        assert(carplay_app_stop() == RTCP_ESTATE);
        assert(carplay_app_touch(RTCP_TOUCH_DOWN, 0, 10, 20) == RTCP_ESTATE);
        assert(carplay_app_request_siri() == RTCP_ESTATE);
        char *args[] = {"carplay_ab", "single"};
        assert(cmd_carplay_ab(2, args) == RTCP_ESTATE);
    }
    mutex->locked = 1; return RT_EOK;
}
int rt_mutex_release(rt_mutex_t mutex)
{
    assert(mutex && mutex->locked); mutex->locked = 0; return RT_EOK;
}
int rt_mutex_delete(rt_mutex_t mutex)
{
    assert(mutex && !mutex->locked && live_mutexes);
    if (current) assert(s_receiver_running && s_receiver_worker_live);
    assert(carplay_app_stop() == RTCP_ESTATE); /* Teardown reentry. */
    assert(carplay_app_touch(RTCP_TOUCH_DOWN, 0, 10, 20) == RTCP_ESTATE);
    assert(carplay_app_request_siri() == RTCP_ESTATE);
    char *args[] = {"carplay_ab", "single"};
    assert(cmd_carplay_ab(2, args) == RTCP_ESTATE);
    --live_mutexes; free(mutex); return RT_EOK;
}
rt_thread_t rt_thread_create(const char *name, void (*entry)(void *), void *context,
                            unsigned stack, unsigned priority, unsigned tick)
{
    (void)name; (void)stack; (void)priority; (void)tick;
    if (fail_create) return NULL;
    struct test_thread *thread = malloc(sizeof(*thread));
    assert(thread); thread->entry = entry; thread->context = context; return thread;
}
int rt_thread_startup(rt_thread_t thread)
{
    assert(thread && !pending);
    assert(s_receiver_worker_live); /* Ownership precedes first scheduling. */
    assert(carplay_app_stop() == RTCP_ESTATE); /* Startup reentry. */
    if (fail_startup) return -1;
    pending = thread; return RT_EOK;
}
int rt_thread_delete(rt_thread_t thread)
{
    assert(thread && thread != pending); ++thread_deletes;
    free(thread); return RT_EOK;
}
rt_thread_t rt_thread_self(void) { return current; }
void rt_thread_mdelay(int milliseconds)
{
    assert(milliseconds > 0 && ++delays < 100);
    /* Stop before first scheduling: execute the real worker, which must
     * still own all its state/resources until its last access. */
    assert(pending);
    assert(s_receiver_stop && s_receiver_worker_live && s_receiver_running);
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
    for (unsigned round = 0; round < 64; ++round) {
        (void)pthread_barrier_wait(&contenders);
        bool won = carplay_bt_claim(owner);
        if (won) atomic_fetch_add(&winners, 1);
        (void)pthread_barrier_wait(&contenders);
        assert(atomic_load(&winners) == 1);
        if (won) carplay_bt_release(owner);
        (void)pthread_barrier_wait(&contenders);
        if (owner == CARPLAY_BT_OWNER_ECHO) atomic_store(&winners, 0);
        (void)pthread_barrier_wait(&contenders);
    }
    return NULL;
}

int main(void)
{
    char directory[] = "/tmp/rtcp-rtthread-lifecycle-XXXXXX";
    assert(mkdtemp(directory));
    char seed[256], store[256];
    assert(snprintf(seed, sizeof(seed), "%s/identity", directory) > 0);
    assert(snprintf(store, sizeof(store), "%s/pairs", directory) > 0);
    carplay_app_config_t config;
    carplay_app_config_defaults(&config);
    config.wired = true; config.seed_path = seed; config.store_path = store;
    config.width_pixels = 1024; config.height_pixels = 600;

    fail_startup = true;
    assert(carplay_app_start(&config) == RTCP_EIO);
    assert(!carplay_app_is_running() && !pending && live_mutexes == 0);
    assert(thread_deletes == 1);
    assert(carplay_app_stop() == RTCP_OK);

    fail_startup = false; fail_create = true;
    assert(carplay_app_start(&config) == RTCP_ENOMEM);
    assert(!carplay_app_is_running() && !pending && live_mutexes == 0);
    fail_create = false;

    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        assert(carplay_app_start(&config) == RTCP_OK);
        assert(carplay_app_is_running() && pending && live_mutexes == 1);
        assert(carplay_app_start(&config) == RTCP_ESTATE);
        check_input_reentry = true;
        assert(carplay_app_touch(RTCP_TOUCH_DOWN, 0, 10, 20) == RTCP_OK);
        assert(carplay_app_request_siri() == RTCP_OK);
        check_input_reentry = false;
        char *single[] = {"carplay_ab", "single"};
        char *chunked[] = {"carplay_ab", "chunked"};
        assert(cmd_carplay_ab(2, single) == RTCP_OK);
        rtcp_receiver_diagnostics_t diagnostics;
        assert(rtcp_receiver_get_diagnostics(s_app.receiver, &diagnostics) == RTCP_OK);
        assert(!diagnostics.single_cipher_frame); /* Shell only queued it. */
        drain_receiver_commands(&s_app); /* Execute on the modeled owner. */
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
        assert(carplay_app_stop() == RTCP_OK);
        assert(!carplay_app_is_running() && !pending && live_mutexes == 0);
        assert(s_app.thread == NULL);
        assert(cmd_carplay_ab(2, single) == RTCP_ESTATE);
        assert(carplay_app_touch(RTCP_TOUCH_UP, 0, 10, 20) == RTCP_ESTATE);
        assert(carplay_app_request_siri() == RTCP_ESTATE);
        assert(carplay_app_stop() == RTCP_OK);
    }
    assert(unlink(seed) == 0);
    assert(rmdir(directory) == 0);
    carplay_bt_owner_t owners[] = {CARPLAY_BT_OWNER_ECHO, CARPLAY_BT_OWNER_IAP2};
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

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual USB iAP2 worker with real SDK sessions and delayed scheduling."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
rtcp = Path(os.environ.get("RTCP_ROOT", root.parents[2] / "packages/third-party/rt-carplay"))
text = (root / "app/carplay_usb_cmd.c").read_text(encoding="utf-8")


def function(signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


source = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "rtcp/rtcp.h"
#include "rtcp/mfi.h"
#include "carplay_app.h"
#include "iap2_session.h"
#include "iap2_stream.h"
#include "rtthread.h"
#define IAP2_USB_STACK_BYTES 12288
#define IAP2_USB_PRIORITY 20
#define IAP2_USB_TICK 20
#define IAP2_USB_RX_TIMEOUT_MS 100
'''
a = text.index("typedef struct {\n    iap2_session_t *session;")
b = text.index("} iap2_usb_link_t;", a) + len("} iap2_usb_link_t;")
source += text[a:b]
source += r'''
static iap2_usb_link_t s_iap2_usb;
static atomic_flag s_usb_call = ATOMIC_FLAG_INIT;
static atomic_uint s_usb_stop_generation;
struct test_thread { void (*entry)(void *); void *context; };
static rt_thread_t pending;
static unsigned releases, deletes, writes;
static bool fail_create, fail_startup, hold_worker, detached_in_create;
static rt_thread_t self;
static struct rtcp_mfi provider;
static void iap2_usb_request_stop(void);
static uint32_t iap2_usb_now_ms(void) { return 1234; }
int rt_kprintf(const char *format, ...) {
    if (strstr(format, "stopped (events=")) assert(s_iap2_usb.running);
    return 0;
}
void carplay_mfi_provider_release(const struct rtcp_mfi *mfi) {
    assert(mfi == &provider);
    assert(s_iap2_usb.session == NULL); ++releases;
}
rt_thread_t rt_thread_create(const char *name, void (*entry)(void *), void *context,
                            unsigned stack, unsigned priority, unsigned tick) {
    (void)name; (void)stack; (void)priority; (void)tick;
    assert(s_iap2_usb.running);
    if (detached_in_create) iap2_usb_request_stop();
    if (fail_create) return NULL;
    rt_thread_t thread = malloc(sizeof(*thread)); assert(thread);
    thread->entry = entry; thread->context = context; return thread;
}
int rt_thread_startup(rt_thread_t thread) {
    assert(thread && !pending && s_iap2_usb.running);
    if (fail_startup) return -1;
    pending = thread; return RT_EOK;
}
int rt_thread_delete(rt_thread_t thread) { ++deletes; free(thread); return RT_EOK; }
void rt_thread_mdelay(int ms) {
    assert(ms == 10 && pending && !s_iap2_usb.run && s_iap2_usb.running);
    if (hold_worker) return;
    rt_thread_t thread = pending; pending = NULL;
    thread->entry(thread->context); free(thread);
}
rt_thread_t rt_thread_self(void) { return self; }
'''
source += function("static bool usb_call_enter(")
source += function("static void usb_call_leave(")
source += function("static void iap2_usb_release_provider(")
source += function("static void iap2_usb_reset(")
source += function("static void iap2_usb_thread(")
source += function("static int iap2_usb_start_worker(")
source += function("static void iap2_usb_request_stop(void)\n{")
source += function("static bool cmd_iap2_stop(")
source += r'''
/* The actual public/shell wrappers own the gate. Model their blocking I/O
 * boundaries, not the gate implementation or the worker cleanup. */
static atomic_bool open_entered, open_continue;
static int open_count, closes, pairs, starts, shell_calls;
static int fake_open(void *context, void (*callback)(void), void *user) {
    (void)context; (void)callback; (void)user;
    ++open_count; open_entered = true;
    while (!atomic_load(&open_continue)) {}
    return RTCP_EIO;
}
typedef struct { int (*open)(void *, void (*)(void), void *); } fake_usb_ops_t;
static const fake_usb_ops_t usb_ops = {fake_open};
static const struct { void *context; const fake_usb_ops_t *ops; }
    rtcp_usb_lubanlite = {NULL, &usb_ops};
static void usb_event(void) {}
typedef struct { int closed; } fake_session_t;
static fake_session_t s_lockdown_session;
static uint32_t s_network_generation;
static unsigned activation_revoked;
static bool rtcp_usb_lubanlite_host_ready(uint32_t generation, bool ready) {
    assert(generation == s_network_generation && !ready);
    ++activation_revoked; return true;
}
static void rtcp_lockdown_session_close(fake_session_t *session) {
    assert(!s_iap2_usb.running); ++closes; session->closed = 1;
}
static rtcp_stream_t rtcp_lockdown_session_service(fake_session_t *session) {
    (void)session; return (rtcp_stream_t){0};
}
static int cmd_pair(void) { ++pairs; return 0; }
static int cmd_iap2(void) { ++starts; return RTCP_ENOMEM; }
static int cmd_carplay_usb_owned(int argc, char **argv) {
    (void)argc; (void)argv; ++shell_calls; return RTCP_EINVAL;
}
'''
for signature in ("int carplay_usb_open(", "int carplay_usb_pair(",
                  "int carplay_usb_start_iap2(", "int carplay_usb_close_session(",
                  "static int cmd_carplay_usb(int"):
    source += function(signature)
source += r'''
static bool tx(const uint8_t *data, size_t length, void *context) {
    (void)data; (void)length; (void)context; ++writes; return true;
}
static void prepare(void) {
    assert(!s_iap2_usb.running);
    iap2_usb_reset(atomic_load(&s_usb_stop_generation));
    iap2_session_config_t config = {0}; config.tx = tx;
    config.identity.name = "public-worker-fixture";
    config.transport = IAP2_SESSION_TRANSPORT_WIRED_USB;
    s_iap2_usb.session = iap2_session_create(&config);
    assert(s_iap2_usb.session); s_iap2_usb.mfi = &provider;
    s_iap2_usb.provider_installed = true;
}
static void *open_command(void *unused) {
    (void)unused; assert(carplay_usb_open() == RTCP_EIO); return NULL;
}
int main(void) {
    prepare(); fail_create = true;
    assert(iap2_usb_start_worker() == RTCP_ENOMEM);
    assert(!s_iap2_usb.running && !s_iap2_usb.run && !s_iap2_usb.session);
    assert(!s_iap2_usb.mfi && !pending && releases == 1);
    prepare(); fail_create = false; fail_startup = true;
    assert(iap2_usb_start_worker() == RTCP_EIO);
    assert(!s_iap2_usb.running && !s_iap2_usb.run && !s_iap2_usb.session);
    assert(!s_iap2_usb.thread && !s_iap2_usb.mfi && !pending && releases == 2 && deletes == 1);
    prepare(); fail_startup = false;
    assert(iap2_usb_start_worker() == RTCP_OK);
    assert(pending && s_iap2_usb.running);
    assert(cmd_iap2_stop()); /* Before first scheduling: no late protocol TX. */
    assert(!s_iap2_usb.running && !s_iap2_usb.run && !s_iap2_usb.session);
    assert(!s_iap2_usb.mfi && !pending && releases == 3 && !writes);
    assert(cmd_iap2_stop());

    /* A failed provider install must not decrement another owner's ref. */
    prepare(); s_iap2_usb.provider_installed = false;
    assert(iap2_usb_start_worker() == RTCP_OK);
    assert(cmd_iap2_stop()); assert(releases == 3 && !s_iap2_usb.mfi);
    prepare(); s_iap2_usb.provider_installed = false; fail_create = true;
    assert(iap2_usb_start_worker() == RTCP_ENOMEM);
    assert(releases == 3 && !s_iap2_usb.mfi); fail_create = false;

    /* Detach during bootstrap and scheduler creation remains stopped. */
    prepare(); iap2_usb_request_stop();
    assert(iap2_usb_start_worker() == RTCP_OK && !s_iap2_usb.run);
    assert(cmd_iap2_stop()); assert(!writes && releases == 4);
    prepare(); detached_in_create = true;
    assert(iap2_usb_start_worker() == RTCP_OK && !s_iap2_usb.run);
    detached_in_create = false;
    assert(cmd_iap2_stop()); assert(!writes && releases == 5);

    prepare(); assert(iap2_usb_start_worker() == RTCP_OK);
    self = pending; assert(!cmd_iap2_stop());
    assert(s_iap2_usb.run && s_iap2_usb.running && releases == 5); self = NULL;
    hold_worker = true;
    assert(carplay_usb_close_session() == RTCP_ETIMEDOUT);
    assert(s_iap2_usb.running && s_iap2_usb.session && releases == 5 && !closes);
    assert(!activation_revoked);
    assert(usb_call_enter()); usb_call_leave(); /* timeout releases gate only */
    hold_worker = false;
    assert(carplay_usb_close_session() == RTCP_OK);
    assert(!s_iap2_usb.running && releases == 6 && closes == 1);
    assert(activation_revoked == 1);

    /* Real competing caller, while the first is blocked in modeled I/O. */
    pthread_t caller; assert(pthread_create(&caller, NULL, open_command, NULL) == 0);
    while (!atomic_load(&open_entered)) {}
    assert(carplay_usb_open() == RTCP_ESTATE);
    assert(carplay_usb_pair() == RTCP_ESTATE);
    assert(carplay_usb_start_iap2() == RTCP_ESTATE);
    assert(carplay_usb_close_session() == RTCP_ESTATE);
    assert(cmd_carplay_usb(0, NULL) == RTCP_ESTATE);
    assert(open_count == 1 && !pairs && !starts && !shell_calls && closes == 1);
    open_continue = true; assert(pthread_join(caller, NULL) == 0);
    assert(carplay_usb_pair() == RTCP_EIO && pairs == 1);
    assert(carplay_usb_start_iap2() == RTCP_ENOMEM && starts == 1);
    assert(cmd_carplay_usb(0, NULL) == RTCP_EINVAL && shell_calls == 1);
    assert(usb_call_enter()); usb_call_leave();
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        prepare(); assert(iap2_usb_start_worker() == RTCP_OK);
        assert(cmd_iap2_stop()); assert(!s_iap2_usb.session && !s_iap2_usb.mfi);
    }
    assert(releases == 38 && !writes);
    puts("Actual USB iAP2: command contention, rollback, provider ownership, detach generation, self-join, timeout retention and 32 restarts passed");
}
'''
with tempfile.TemporaryDirectory(prefix="usb-iap2-worker-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source, encoding="utf-8")
    include = [rtcp / part for part in ("include", "core/iap2/include", "core/usb/include")]
    include += [root / "app", root / "tests/support/rtthread_app"]
    libraries = [rtcp / "build/librtcp_core.a", rtcp / "build/mbedtls/libmbedcrypto.a"]
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                    *["-I" + str(item) for item in include], str(path / "test.c"),
                    *map(str, libraries), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)

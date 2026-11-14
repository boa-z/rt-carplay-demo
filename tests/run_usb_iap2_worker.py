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
struct test_thread { void (*entry)(void *); void *context; };
static rt_thread_t pending;
static unsigned releases, deletes, writes;
static bool fail_create, fail_startup;
static struct rtcp_mfi provider;
static uint32_t iap2_usb_now_ms(void) { return 1234; }
int rt_kprintf(const char *format, ...) {
    if (strstr(format, "stopped (events=")) assert(s_iap2_usb.running);
    return 0;
}
void carplay_mfi_provider_release(const struct rtcp_mfi *mfi) {
    assert(mfi == &provider && s_iap2_usb.running);
    assert(s_iap2_usb.session == NULL); ++releases;
}
rt_thread_t rt_thread_create(const char *name, void (*entry)(void *), void *context,
                            unsigned stack, unsigned priority, unsigned tick) {
    (void)name; (void)stack; (void)priority; (void)tick;
    assert(s_iap2_usb.running && s_iap2_usb.run);
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
    rt_thread_t thread = pending; pending = NULL;
    thread->entry(thread->context); free(thread);
}
'''
source += function("static void iap2_usb_thread(")
source += function("static int iap2_usb_start_worker(")
source += function("static bool cmd_iap2_stop(")
source += r'''
static bool tx(const uint8_t *data, size_t length, void *context) {
    (void)data; (void)length; (void)context; ++writes; return true;
}
static void prepare(void) {
    memset(&s_iap2_usb, 0, sizeof(s_iap2_usb));
    atomic_init(&s_iap2_usb.run, false); atomic_init(&s_iap2_usb.running, false);
    iap2_session_config_t config = {0}; config.tx = tx;
    config.identity.name = "public-worker-fixture";
    config.transport = IAP2_SESSION_TRANSPORT_WIRED_USB;
    s_iap2_usb.session = iap2_session_create(&config);
    assert(s_iap2_usb.session); s_iap2_usb.mfi = &provider;
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
    puts("Actual USB iAP2 worker: create/start failure rollback, stop before scheduling and final quiescence passed");
}
'''
with tempfile.TemporaryDirectory(prefix="usb-iap2-worker-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source, encoding="utf-8")
    include = [rtcp / part for part in ("include", "core/iap2/include", "core/usb/include")]
    include += [root / "app", root / "tests/support/rtthread_app"]
    libraries = [rtcp / "build/librtcp_core.a", rtcp / "build/mbedtls/libmbedcrypto.a"]
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                    *["-I" + str(item) for item in include], str(path / "test.c"),
                    *map(str, libraries), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual wireless owner/session code with modeled Bluetooth and scheduling."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
rtcp = Path(os.environ.get("RTCP_ROOT", root.parents[2] / "packages/third-party/rt-carplay"))
text = (root / "app/iap2_link_lubanlite.c").read_text(encoding="utf-8")


def function(signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


owner = text[text.index("typedef struct {"):text.index("/* ------------------------------------------------------------------ */")]
# MFi probing is a hardware boundary. Everything from state publication,
# actual iAP2 callbacks/configuration/session through start/stop is retained.
owner = owner.replace(function("static const struct rtcp_mfi *link_mfi(void)"),
                      "static const struct rtcp_mfi *link_mfi(void);")
source = r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtthread.h"
#include "iap2_link_lubanlite.h"
#include "carplay_app.h"
#include "carplay_mfi_provider.h"
#include "iap2_session.h"
#include "rtcp_lubanlite.h"
#include "rtcp_rtthread.h"
#include "rtcp/rtcp.h"
#define IAP2_LOG(...) rt_kprintf(__VA_ARGS__)
#define IAP2_LINK_UUID "00000000-deca-fade-deca-deafdecacaff"
#define IAP2_LINK_RX_MS 100
#define IAP2_LINK_RX_BYTES 1024U
#define IAP2_LINK_STACK_BYTES (12U * 1024U)
#define IAP2_LINK_PRIORITY 20U
#define IAP2_LINK_TICK 20U
#define RTCARPLAY_IAP2_MODEL "public-fixture"
#define RTCARPLAY_IAP2_FIRMWARE_VERSION "test"
#define RTCARPLAY_IAP2_HARDWARE_VERSION "test"
#define IAP2_LINK_MANUFACTURER "test"
#define IAP2_LINK_LANGUAGE "en"
'''
source += owner
source += text[text.index("bool carplay_iap2_tunnel_bytes("):]
source += r'''
struct test_thread { void (*entry)(void *); void *argument; };
static rt_thread_t pending, current;
static bool receiver_up = true, claimed, fail_create, fail_startup, drain_on_wait = true;
static bool fail_install, stop_in_probe, stop_in_enable, stop_in_address, check_self;
static bool accept_failure, stop_in_accept, stop_in_accept_success;
static bool stop_in_claim;
static unsigned creates, deletes, claims, releases, probes, installs, provider_releases;
static unsigned enables, listens, disconnects, writes, waits, receives;
static struct rtcp_mfi provider;
static atomic_bool reader_done;
static atomic_uint reads;

int rt_kprintf(const char *format, ...) {
    if (!strcmp(format, "service stopped")) {
        assert(s_link.running && !claimed && !s_link.session && !s_link.link_up);
        assert(s_link_thread == current); /* no early quiescence publication */
    }
    return 0;
}
static const struct rtcp_mfi *link_mfi(void) {
    ++probes;
    if (stop_in_probe) carplay_iap2_request_stop();
    return &provider;
}
int carplay_mfi_provider_install(const struct rtcp_mfi *mfi) {
    assert(mfi == &provider && s_link.running); ++installs;
    return fail_install ? RTCP_EIO : RTCP_OK;
}
void carplay_mfi_provider_release(const struct rtcp_mfi *mfi) {
    assert(mfi == &provider && !fail_install && s_link.running && !s_link.session);
    ++provider_releases;
}
bool carplay_bt_claim(carplay_bt_owner_t owner) {
    assert(owner == CARPLAY_BT_OWNER_IAP2);
    if (claimed) return false;
    claimed = true; ++claims;
    if (stop_in_claim) carplay_iap2_request_stop();
    return true;
}
void carplay_bt_release(carplay_bt_owner_t owner) {
    assert(owner == CARPLAY_BT_OWNER_IAP2 && claimed && s_link.running);
    claimed = false; ++releases;
}
bool carplay_app_is_running(void) { return receiver_up; }
int carplay_app_iap2_info(carplay_app_iap2_info_t *out) {
    memset(out, 0, sizeof(*out));
    strcpy(out->name, "public-fixture"); strcpy(out->device_id, "02:00:00:00:00:01");
    strcpy(out->ssid, "public-fixture"); strcpy(out->public_key_hex, "public-key");
    out->source_version = "test"; out->airplay_port = 7000;
    return receiver_up ? RTCP_OK : RTCP_ESTATE;
}
bool carplay_app_send_iap(const uint8_t *data, size_t length) {
    (void)data; (void)length; return true;
}
static uint64_t now_ms(void) { return 100; }
const struct rtcp_os rtcp_os_rtthread = {.now_ms = now_ms};
static int enable(const char *name, const uint8_t address[6]) {
    assert(!strcmp(name, "rt-carplay") && !address && s_link.running); ++enables;
    if (stop_in_enable) carplay_iap2_request_stop();
    return RTCP_OK;
}
const struct rtcp_bt rtcp_bt_lubanlite = {.enable = enable};
int rtcp_bt_lubanlite_local_bdaddr(uint8_t address[6]) {
    memset(address, 0, 6); address[0] = 2;
    if (stop_in_address) carplay_iap2_request_stop();
    return RTCP_OK;
}
int rtcp_bt_lubanlite_rfcomm_listen(uint8_t channel, const char *name,
                                  const char *uuid, uint8_t *out) {
    (void)name; assert(!channel && !strcmp(uuid, IAP2_LINK_UUID) && s_serve);
    ++listens; *out = 6; return RTCP_OK;
}
int rtcp_bt_lubanlite_reconnect_tick(bool allowed) { (void)allowed; return RTCP_OK; }
int rtcp_bt_lubanlite_rfcomm_accept_timeout(int lfd, int *fd, int timeout) {
    assert(lfd == 1 && timeout == 500 && s_link.running);
    if (accept_failure) return RTCP_EIO;
    if (stop_in_accept) { carplay_iap2_request_stop(); return RTCP_ETIMEDOUT; }
    if (stop_in_accept_success) carplay_iap2_request_stop();
    *fd = 1; return RTCP_OK;
}
int rtcp_bt_lubanlite_send(int fd, const void *data, size_t length) {
    assert(fd == 1 && data && length); ++writes; return (int)length;
}
int rtcp_bt_lubanlite_recv_timeout(int fd, void *data, size_t capacity, int timeout) {
    (void)data; assert(fd == 1 && capacity == 1024 && timeout == 100); ++receives;
    if (check_self) {
        unsigned previous = waits;
        assert(carplay_iap2_stop() == RTCP_ESTATE && waits == previous && s_serve);
    }
    link_event(IAP2_SESSION_EVENT_IDENTIFICATION_ACCEPTED, &s_link);
    link_event(IAP2_SESSION_EVENT_MFI_AUTHENTICATED, &s_link);
    for (unsigned n = 0; n < 100; ++n) {
        link_event(IAP2_SESSION_EVENT_WIFI_CONFIG_SENT, &s_link);
        carplay_iap2_status_t status; carplay_iap2_get_status(&status);
        assert(status.connected && status.identified && status.authenticated && status.mfi_completed);
    }
    carplay_iap2_request_stop(); return 0;
}
void rtcp_bt_lubanlite_drop_link(int fd) { assert(fd == 1); }
void rtcp_bt_lubanlite_disconnect(void) {
    assert(current && s_link.running); ++disconnects;
}
unsigned rtcp_bt_lubanlite_bootstrap_generation(void) { return 1; }
int rtcp_bt_lubanlite_handoff(unsigned generation) { assert(generation == 1); return RTCP_OK; }
rt_thread_t rt_thread_create(const char *name, void (*entry)(void *), void *argument,
                            unsigned stack, unsigned priority, unsigned tick) {
    (void)name; assert(stack == IAP2_LINK_STACK_BYTES && priority == 20 && tick == 20);
    assert(s_link.running && claimed); ++creates;
    /* Reentrant/concurrent lifecycle calls must not alter this startup. */
    assert(carplay_iap2_start() == RTCP_ESTATE && carplay_iap2_stop() == RTCP_ESTATE);
    if (fail_create) return NULL;
    rt_thread_t result = malloc(sizeof(*result)); assert(result);
    result->entry = entry; result->argument = argument; return result;
}
int rt_thread_startup(rt_thread_t thread) {
    assert(thread && s_link.running && !pending && s_link_thread == thread);
    if (fail_startup) return -1;
    pending = thread; return RT_EOK;
}
int rt_thread_delete(rt_thread_t thread) { ++deletes; free(thread); return RT_EOK; }
rt_thread_t rt_thread_self(void) { return current; }
static void run_pending(void) {
    assert(pending && s_link.running && claimed);
    current = pending; pending = NULL; current->entry(current->argument);
    free(current); current = NULL;
    assert(!s_link.running && !s_link_thread && !s_link.session && !claimed);
}
void rt_thread_mdelay(int ms) {
    assert(ms == 10 && s_link.running && !s_serve); ++waits;
    if (drain_on_wait) run_pending();
}
static void *reader(void *unused) {
    (void)unused;
    while (!atomic_load(&reader_done)) {
        carplay_iap2_status_t status; carplay_iap2_get_status(&status);
        (void)carplay_iap2_is_running(); (void)carplay_iap2_authenticated();
        (void)carplay_iap2_identification_accepted(); atomic_fetch_add(&reads, 1);
    }
    return NULL;
}
int main(void) {
    receiver_up = false;
    assert(carplay_iap2_start() == RTCP_ESTATE && !claims && !creates);
    receiver_up = true; claimed = true;
    assert(carplay_iap2_start() == RTCP_ESTATE && !claims && !creates);
    claimed = false; fail_create = true;
    assert(carplay_iap2_start() == RTCP_ENOMEM && !claimed && !s_link.running);
    assert(s_link.error == RTCP_ENOMEM && releases == 1);
    fail_create = false; fail_startup = true;
    assert(carplay_iap2_start() == RTCP_EIO && !claimed && !s_link.running && !s_link_thread);
    assert(s_link.error == RTCP_EIO && deletes == 1 && releases == 2);
    fail_startup = false;
    assert(carplay_iap2_start() == RTCP_OK && pending && claimed);
    assert(carplay_iap2_start() == RTCP_ESTATE);
    assert(carplay_iap2_stop() == RTCP_OK && !probes && !enables && !disconnects && !writes);
    unsigned old = disconnects;
    assert(carplay_iap2_stop() == RTCP_OK && disconnects == old);

    assert(carplay_iap2_start() == RTCP_OK); drain_on_wait = false;
    assert(carplay_iap2_stop() == RTCP_ETIMEDOUT && s_link.running && pending && claimed);
    assert(carplay_iap2_start() == RTCP_ESTATE); run_pending(); drain_on_wait = true;
    assert(!enables && !probes);

    stop_in_claim = true;
    assert(carplay_iap2_start() == RTCP_OK && !s_serve); run_pending();
    assert(!enables && !probes); stop_in_claim = false;

    stop_in_probe = true;
    assert(carplay_iap2_start() == RTCP_OK); run_pending();
    assert(probes == 1 && !installs && !provider_releases && !enables); stop_in_probe = false;
    stop_in_enable = true;
    assert(carplay_iap2_start() == RTCP_OK); run_pending();
    assert(enables == 1 && !listens && installs == 1 && provider_releases == 1 && disconnects == 1);
    stop_in_enable = false; stop_in_address = true;
    assert(carplay_iap2_start() == RTCP_OK); run_pending(); assert(!listens && !writes);
    stop_in_address = false; fail_install = true; stop_in_accept = true;
    old = provider_releases;
    assert(carplay_iap2_start() == RTCP_OK); run_pending(); assert(provider_releases == old);
    fail_install = false; stop_in_accept = false; stop_in_accept_success = true;
    old = writes;
    assert(carplay_iap2_start() == RTCP_OK); run_pending(); assert(writes == old);
    stop_in_accept_success = false; accept_failure = true;
    assert(carplay_iap2_start() == RTCP_OK); run_pending(); assert(s_link.error == RTCP_EIO);
    accept_failure = false; check_self = true;

    pthread_t observer; assert(!pthread_create(&observer, NULL, reader, NULL));
    while (!atomic_load(&reads)) {} /* ensure the reader actually executes */
    for (unsigned n = 0; n < 32; ++n) {
        assert(carplay_iap2_start() == RTCP_OK); run_pending();
        carplay_iap2_status_t status; carplay_iap2_get_status(&status);
        assert(!status.running && !status.connected && !status.identified && !status.authenticated);
        assert(status.mfi_completed && status.wifi_configs == 100);
        assert(carplay_iap2_tunnel_bytes(NULL, 0, NULL));
        assert(carplay_iap2_request_handoff("phone", NULL));
        link_event(IAP2_SESSION_EVENT_IDENTIFICATION_ACCEPTED, &s_tunnel);
        link_event(IAP2_SESSION_EVENT_MFI_AUTHENTICATED, &s_tunnel);
        assert(carplay_iap2_tunnel_tick(NULL));
        carplay_iap2_get_status(&status);
        assert(status.tunnel_connected && status.tunnel_identified && status.tunnel_authenticated);
        assert(status.handoff_complete);
        carplay_iap2_tunnel_stop(NULL); carplay_iap2_get_status(&status);
        assert(!status.tunnel_connected && !status.tunnel_identified && !status.tunnel_authenticated);
        assert(!status.handoff_complete);
    }
    atomic_store(&reader_done, true); assert(!pthread_join(observer, NULL));
    assert(receives == 32 && writes && claims == releases && !claimed && !pending);
    puts("Actual wireless iAP2 owner: rollback, command gate, pre-scheduling stop, startup cancellation, provider ownership, self-join, timeout retention and 32 restarts with concurrent atomic readers passed");
}
'''
with tempfile.TemporaryDirectory(prefix="wireless-iap2-worker-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source, encoding="utf-8")
    includes = [rtcp / part for part in ("include", "core/iap2/include", "port/rtthread/include")]
    includes += [root / part for part in ("app", "platform", "tests/support/rtthread_app")]
    libraries = [rtcp / "build/librtcp_core.a", rtcp / "build/mbedtls/libmbedcrypto.a"]
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-pthread",
                    *["-I" + str(item) for item in includes], str(path / "test.c"),
                    *map(str, libraries), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=30)

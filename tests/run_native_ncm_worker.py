#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise actual async NCM worker/cancellation functions with fake HCD events."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root / "platform/native_ncm_lubanlite.c").read_text()


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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RT_WAITING_FOREVER -1
#define rt_kprintf(...) ((void)0)
struct usbh_urb { void *hcpriv; unsigned timeout, size; void (*callback)(void *,int); };
static struct {
    int lock, ready, exited, in, notification;
    struct { int *netif; } eth;
    void *hport;
    struct { unsigned input; } limits;
    struct { int notification, control; } function;
    atomic_uint stopping, rx_done, note_done, carrier;
    atomic_int interface, rx_result, note_result;
    atomic_uint rx_errors, notifications;
    struct usbh_urb rx, note;
} net;
static struct { uint8_t rx[16384], notification[64]; } dma;
static unsigned submits, kills, waits, delivered, exits, downs, acknowledgements;
static int scenario;
static void rt_mutex_take(int *lock, int timeout) { (void)lock; (void)timeout; }
static void rt_mutex_release(int *lock) { (void)lock; }
static void usbh_bulk_urb_fill(struct usbh_urb *u, void *h, int *ep, uint8_t *data,
    unsigned size, unsigned timeout, void (*fn)(void *,int), void *arg) {
    (void)h; (void)ep; (void)data; (void)arg;
    assert(!u->hcpriv); u->size = size; u->timeout = timeout; u->callback = fn;
}
#define usbh_int_urb_fill usbh_bulk_urb_fill
static int usbh_submit_urb(struct usbh_urb *u) {
    ++submits; assert(!u->timeout); /* async request survives idle waits */
    if (scenario == 1) return -1;
    u->hcpriv = u; return 0;
}
static int usbh_kill_urb(struct usbh_urb *u) {
    assert(u->hcpriv); ++kills;
    if (scenario == 3) return -1;
    u->hcpriv = NULL;
    u->callback(NULL, -1); /* shutdown callback cannot trigger rearm */
    return 0;
}
static int usb_osal_sem_give(int sem) {
    if (sem == net.exited) ++acknowledgements;
    return 0;
}
static int usb_osal_sem_take(int sem, int timeout) {
    assert(sem == net.ready && timeout == 100);
    switch (waits++) {
    case 0: assert(submits == 2); break; /* idle: neither request is cancelled */
    case 1:
        net.rx.hcpriv = NULL;
        net.rx.callback(NULL, scenario == 2 ? -1 : 28);
        dma.notification[0] = 0xa1; dma.notification[1] = 0;
        dma.notification[2] = 1; dma.notification[4] = 3;
        net.note.hcpriv = NULL; net.note.callback(NULL, 8);
        break;
    default: atomic_store(&net.stopping, true); break;
    }
    return 0;
}
static int input_frame(void *arg, const uint8_t *data, size_t size) {
    (void)arg; (void)data; (void)size; return 0;
}
static int carplay_ncm_decode(const uint8_t *data, size_t size,
    int (*fn)(void *, const uint8_t *,size_t), void *arg) {
    (void)data; (void)fn; (void)arg; assert(size == 28); ++delivered; return 0;
}
static int netifapi_netif_set_link_down(int *n) { (void)n; ++downs; return 0; }
static void usb_osal_thread_delete(void *t) { assert(!t); ++exits; }
'''
for name in ("static void rx_completed(", "static void note_completed(",
             "static bool arm(", "static bool cancel(", "static void rx_worker("):
    source += function(name)
source += r'''
static void reset(int mode) {
    memset(&net, 0, sizeof(net)); memset(&dma, 0, sizeof(dma));
    net.ready = 1; net.exited = 2; net.interface = 3;
    net.function.notification = 0x87; net.function.control = 3;
    net.limits.input = 16384;
    submits = kills = waits = delivered = exits = downs = acknowledgements = 0;
    scenario = mode;
}
int main(void) {
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        reset(0); rx_worker(NULL);
        assert(submits == 4 && kills == 2 && waits == 3 && delivered == 1);
        assert(exits == 1 && downs == 1 && acknowledgements == 1);
        assert(!net.rx.hcpriv && !net.note.hcpriv && net.interface == -1);
        assert(net.notifications == 1 && net.carrier);
        assert(!arm(false)); assert(submits == 4);
        reset(1); rx_worker(NULL); /* failed first submits have no HCD owners */
        assert(!waits && !kills && acknowledgements == 1 && net.interface == -1);
        reset(2); rx_worker(NULL); /* terminal RX error: no unbounded submit loop */
        assert(net.rx_errors == 1 && !delivered && kills == 1 && acknowledgements == 1);
        reset(3); rx_worker(NULL); /* keep owners on failed HCD cancellation */
        assert(kills == 2 && !acknowledgements && net.interface == -1);
        assert(net.rx.hcpriv && net.note.hcpriv && !arm(false));
    }
    puts("actual NCM worker: idle retention, RX/notification rearm, shutdown, submit/error/cancel failure passed (32 cycles)");
}
'''
with tempfile.TemporaryDirectory(prefix="native-ncm-worker-") as tmp:
    path = Path(tmp)
    (path / "worker.c").write_text(source)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-g",
                    str(path / "worker.c"), "-o", str(path / "worker")], check=True)
    subprocess.run([str(path / "worker")], check=True)

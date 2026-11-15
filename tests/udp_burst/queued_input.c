/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual lwIP input/dispatch code; only the OS mailbox is modeled. */
#include LWIP_TCPIP_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

static bool in_rx, reject_packet;
static unsigned calls, live_pbufs;
err_t sys_mbox_trypost(sys_mbox_t *m, void *p)
{
    if (m->count == m->capacity) return ERR_MEM;
    m->items[m->count++] = p;
    return ERR_OK;
}
void sys_mutex_lock(sys_mutex_t *m) { (void)m; }
void sys_mutex_unlock(sys_mutex_t *m) { (void)m; }
u32_t sys_now(void) { return 0; }

static err_t input(struct pbuf *p, struct netif *netif)
{
    assert(netif);
    /* TCP input can generate a synchronous USB ACK. It must execute in
     * the TCP/IP consumer, never inline in the producer's RX call. */
    assert(!in_rx);
    ++calls;
    if (reject_packet) return ERR_VAL;
    assert(pbuf_free(p) == 1);
    --live_pbufs;
    return ERR_OK;
}
static struct pbuf *packet(void)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, 64, PBUF_RAM);
    assert(p);
    ++live_pbufs;
    return p;
}
#if !LWIP_TCPIP_CORE_LOCKING_INPUT
static void drain(void)
{
    for (unsigned i = 0; i < tcpip_mbox.count; ++i)
        tcpip_thread_handle_msg(tcpip_mbox.items[i]);
    tcpip_mbox.count = 0;
}
#endif
int main(void)
{
    struct netif netif = {0};
    mem_init(); memp_init();
    tcpip_mbox.capacity = 2;
    in_rx = true;
    assert(tcpip_inpkt(packet(), &netif, input) == ERR_OK);
    assert(!calls);
    assert(tcpip_inpkt(packet(), &netif, input) == ERR_OK);
    struct pbuf *rejected = packet();
    assert(tcpip_inpkt(rejected, &netif, input) == ERR_MEM);
    /* A failed enqueue leaves ownership with the adapter. */
    assert(pbuf_free(rejected) == 1); --live_pbufs;
    assert(!calls && live_pbufs == 2);
    in_rx = false;
#if !LWIP_TCPIP_CORE_LOCKING_INPUT
    drain();
    assert(calls == 2 && !live_pbufs);
    /* A protocol input error is freed by the real tcpip dispatcher. */
    reject_packet = true;
    assert(tcpip_inpkt(packet(), &netif, input) == ERR_OK);
    drain(); --live_pbufs;
    assert(calls == 3 && !live_pbufs);
    reject_packet = false;
    assert(tcpip_inpkt(packet(), &netif, input) == ERR_OK);
    drain();
    assert(calls == 4 && !live_pbufs);
#endif
    puts("actual lwIP input: queued dispatch, bounded rejection, pbuf ownership and recovery passed");
    return 0;
}

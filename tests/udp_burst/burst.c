/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual lwIP netconn UDP callback, with only the OS mailbox modeled. */
#include LWIP_API_MSG_SOURCE
#include "lwip/init.h"
#include <assert.h>
#include <stdio.h>
err_t sys_mbox_trypost(sys_mbox_t *m, void *p)
{
    if (m->count == m->capacity) return ERR_MEM;
    m->items[m->count++] = p; return ERR_OK;
}
u32_t sys_now(void) { return 0; }
static unsigned run(unsigned slots, int bytes, unsigned count, unsigned size)
{
    struct netconn conn = {0}; struct udp_pcb pcb = {0};
    conn.type = NETCONN_UDP; conn.pcb.udp = &pcb;
    conn.recvmbox.capacity = slots; conn.recv_bufsize = bytes;
    ip_addr_t source; IP_ADDR4(&source,192,168,169,2);
    for (unsigned i=0; i<count; ++i) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW,(u16_t)size,PBUF_RAM); assert(p);
        recv_udp(&conn,&pcb,p,&source,5000);
    }
    unsigned received = conn.recvmbox.count;
    assert(conn.recv_avail == (int)(received * size));
    for (unsigned i=0; i<received; ++i) netbuf_delete(conn.recvmbox.items[i]);
    return received;
}
int main(void)
{
    mem_init(); memp_init();
    /* A 17-frame burst with a 700-byte AAC AU + 36-byte wire envelope.
     * Old one-slot configuration reproduces exactly 16 missing frames. */
    assert(run(1,8192,17,736)==1);
    assert(run(64,8192,17,736)==11); /* slots alone do not fix byte drops */
    assert(run(64,65536,17,736)==17);
    assert(run(64,65536,64,736)==64);
    assert(run(64,65536,65,736)==64); /* memory remains bounded */
    assert(run(64,65536,32,2084)==31); /* PCM-sized byte bound */
    puts("actual lwIP recv_udp: old 17-frame burst loses 16; fixed 64-frame burst retained; slot and byte bounds passed");
    return 0;
}

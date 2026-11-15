/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <string.h>
#define RT_USING_LWIP 1
#define RTCARPLAY_WIRED_NETIF "n0"
static unsigned lock_depth, lock_calls;
#define LOCK_TCPIP_CORE() do { assert(!lock_depth); ++lock_depth; ++lock_calls; } while (0)
#define UNLOCK_TCPIP_CORE() do { assert(lock_depth == 1); --lock_depth; } while (0)
/* Use the actual seam and lwIP netif layout. Unused IPv6 text conversion is
 * discarded at link time; these checks exercise MAC selection/validation. */
#include "../../platform/rtcp_net_lubanlite.c"
struct netif *netif_list;

static void rejected(void)
{
    uint8_t mac[6];
    memset(mac, 0xa5, sizeof(mac));
    assert(!rtcp_lubanlite_wired_mac(mac));
    const uint8_t zero[6] = {0};
    assert(!memcmp(mac, zero, 6) && !lock_depth);
}

int main(void)
{
    assert(!rtcp_lubanlite_wired_mac(NULL) && !lock_calls);
    rejected();
    struct netif wan = {.name = {'e', '0'}, .hwaddr_len = 6,
        .hwaddr = {2, 1, 2, 3, 4, 5}, .flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP};
    struct netif ipheth = wan;
    ipheth.name[0] = 'i';
    ipheth.next = &wan;
    netif_list = &ipheth;
    rejected(); /* No fallback to IPHETH/WAN. */
    struct netif native = wan;
    native.name[0] = 'n';
    native.next = &ipheth;
    netif_list = &native;
    uint8_t mac[6];
    assert(rtcp_lubanlite_wired_mac(mac) && !memcmp(mac, native.hwaddr, 6));
    native.hwaddr[5] ^= 1;
    assert(mac[5] != native.hwaddr[5]); /* The result is owned by the caller. */
    native.flags = NETIF_FLAG_UP;
    rejected(); /* Detached/stale interface is unavailable. */
    native.flags = NETIF_FLAG_LINK_UP;
    rejected();
    native.flags |= NETIF_FLAG_UP;
    native.hwaddr_len = 5;
    rejected();
    native.hwaddr_len = 6;
    memset(native.hwaddr, 0, 6);
    rejected();
    native.hwaddr[0] = 1;
    rejected(); /* Multicast/broadcast identities cannot identify a device. */
    memset(native.hwaddr, 0xff, 6);
    rejected();
    native.hwaddr[0] = 0x9e;
    assert(rtcp_lubanlite_wired_mac(mac) && !lock_depth);
    assert(lock_calls >= 10);
    return 0;
}

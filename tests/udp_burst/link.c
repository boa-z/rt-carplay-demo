/* SPDX-License-Identifier: GPL-3.0-or-later */
#include IPHETH_RTTHREAD_SOURCE
#include <assert.h>
#include <stdio.h>
static unsigned mailbox_calls, api_calls;
/* Model the full shared Ethernet RX mailbox. Old adapter loses this event. */
int eth_device_linkchange(struct eth_device *eth, int up)
{
    (void)eth;
    (void)up;
    ++mailbox_calls;
    return -1;
}
void netif_set_link_up(struct netif *n)
{
    n->flags |= NETIF_FLAG_LINK_UP;
}
void netif_set_link_down(struct netif *n)
{
    n->flags &= ~NETIF_FLAG_LINK_UP;
}
err_t netifapi_netif_common(struct netif *n, netifapi_void_fn fn, netifapi_errt_fn errfn)
{
    ++api_calls;
    assert(fn && !errfn);
    fn(n);
    return ERR_OK;
}
int rt_kprintf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}
int main(void)
{
    struct ipheth_traffic t = { 0 };
    uint8_t frame[86] = { 0 };
    frame[12] = 0x86;
    frame[13] = 0xdd;
    frame[14] = 0x60;
    frame[19] = 24;
    frame[20] = 58;
    frame[54] = 135;
    ipheth_traffic_observe(&t, frame, sizeof(frame));
    frame[54] = 136;
    ipheth_traffic_observe(&t, frame, sizeof(frame));
    frame[20] = 6;
    frame[54] = 0xc0;
    frame[55] = 0x01;
    frame[56] = 0x1b;
    frame[57] = 0x58;
    frame[66] = 0x50;
    frame[67] = 2;
    ipheth_traffic_observe(&t, frame, sizeof(frame));
    frame[67] = 0x12;
    ipheth_traffic_observe(&t, frame, sizeof(frame));
    frame[67] = 4;
    ipheth_traffic_observe(&t, frame, sizeof(frame));
    assert(t.ipv6 == 5 && t.ns == 1 && t.na == 1 && t.syn == 1 && t.syn_ack == 1 && t.rst == 1);
    assert(t.source_port == 49153 && t.destination_port == 7000);
    /* Truncated headers/payloads and IPv6 extension headers must not
     * manufacture SYN/NA evidence or read beyond the captured frame. */
    for (size_t n = 0; n < 54; ++n) ipheth_traffic_observe(&t, frame, n);
    frame[19] = 33;
    ipheth_traffic_observe(&t, frame, sizeof(frame));
    frame[19] = 24;
    frame[20] = 44;
    ipheth_traffic_observe(&t, frame, sizeof(frame));
    assert(t.syn == 1 && t.syn_ack == 1 && t.rst == 1 && t.na == 1);
    struct netif netif = { 0 };
    nets[0].eth.netif = &netif;
    usbh_ipheth_net_link(&nets[0], true);
    assert(netif_is_link_up(&netif));
    usbh_ipheth_net_link(&nets[0], false);
    assert(!netif_is_link_up(&netif));
    assert(api_calls == 2 && mailbox_calls == 0);
    puts("actual IPHETH RT-Thread adapter: link up/down bypass a full Ethernet RX mailbox");
    return 0;
}

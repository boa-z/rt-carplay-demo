/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lwip/init.h"
#include "lwip/ip4.h"
#include "lwip/napt_policy.h"
#include "lwip/inet_chksum.h"
#include "lwip/mem.h"
#include "lwip/prot/udp.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static u32_t now;
static int fail_allocation;
void *__real_mem_calloc(mem_size_t count, mem_size_t size);
void *__wrap_mem_calloc(mem_size_t count, mem_size_t size)
{
    return fail_allocation ? NULL : __real_mem_calloc(count, size);
}
u32_t sys_now(void)
{
    return now;
}
static struct netif lan, wan, usb;
static unsigned wan_packets, lan_packets, usb_packets;
static u8_t captured[1600];
static unsigned captured_len;
static FILE *evidence;
static void record(struct pbuf *p)
{
    u8_t bytes[1600];
    unsigned size = pbuf_copy_partial(p, bytes, sizeof(bytes), 0);
    u32_t header[4] = { now / 1000, (now % 1000) * 1000, size, size };
    assert(fwrite(header, sizeof(header), 1, evidence) == 1);
    assert(fwrite(bytes, size, 1, evidence) == 1);
}
static err_t capture(struct netif *n, struct pbuf *p, const ip4_addr_t *dest)
{
    (void)dest;
    if (n == &wan)
        wan_packets++;
    else if (n == &lan)
        lan_packets++;
    else
        usb_packets++;
    captured_len = pbuf_copy_partial(p, captured, sizeof(captured), 0);
    record(p);
    return ERR_OK;
}
static err_t setup(struct netif *n)
{
    n->output = capture;
    n->mtu = 1500;
    n->flags = NETIF_FLAG_BROADCAST;
    return ERR_OK;
}
static ip4_addr_t addr(const char *text)
{
    ip4_addr_t a;
    assert(ip4addr_aton(text, &a));
    return a;
}
static void add(struct netif *n, const char *ip, const char *gw)
{
    ip4_addr_t a = addr(ip), mask = addr("255.255.255.0"), g = addr(gw);
    assert(netif_add(n, &a, &mask, &g, NULL, setup, ip4_input));
    netif_set_up(n);
    netif_set_link_up(n);
}
static struct pbuf *packet(const char *src, const char *dst, u8_t proto, u16_t sport, u16_t dport)
{
    unsigned len = proto == IP_PROTO_TCP ? 40 : 28;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
    assert(p);
    memset(p->payload, 0, len);
    struct ip_hdr *ip = p->payload;
    IPH_VHL_SET(ip, 4, 5);
    IPH_LEN_SET(ip, lwip_htons(len));
    IPH_TTL_SET(ip, 64);
    IPH_PROTO_SET(ip, proto);
    ip->src.addr = addr(src).addr;
    ip->dest.addr = addr(dst).addr;
    if (proto == IP_PROTO_TCP)
    {
        struct tcp_hdr *tcp = (void *)((u8_t *)p->payload + 20);
        tcp->src = lwip_htons(sport);
        tcp->dest = lwip_htons(dport);
        TCPH_HDRLEN_FLAGS_SET(tcp, 5, TCP_SYN);
    }
    else if (proto == IP_PROTO_UDP)
    {
        struct udp_hdr *udp = (void *)((u8_t *)p->payload + 20);
        udp->src = lwip_htons(sport);
        udp->dest = lwip_htons(dport);
        udp->len = lwip_htons(8);
    }
    else
    {
        struct icmp_echo_hdr *icmp = (void *)((u8_t *)p->payload + 20);
        icmp->type = ICMP_ECHO;
        icmp->id = lwip_htons(sport);
        icmp->chksum = inet_chksum(icmp, 8);
    }
    if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP)
    {
        ip4_addr_t s = addr(src), d = addr(dst);
        assert(pbuf_remove_header(p, 20) == 0);
        u16_t checksum = inet_chksum_pseudo(p, proto, len - 20, &s, &d);
        if (proto == IP_PROTO_TCP)
            ((struct tcp_hdr *)p->payload)->chksum = checksum;
        else
            ((struct udp_hdr *)p->payload)->chksum = checksum ? checksum : 0xffff;
        assert(pbuf_add_header(p, 20) == 0);
    }
    IPH_CHKSUM_SET(ip, inet_chksum(ip, 20));
    return p;
}
static void input(struct pbuf *p, struct netif *n)
{
    record(p);
    assert(ip4_input(p, n) == ERR_OK);
}
static struct pbuf *split(struct pbuf *original)
{
    struct pbuf *head = pbuf_alloc(PBUF_RAW, 22, PBUF_RAM);
    struct pbuf *tail = pbuf_alloc(PBUF_RAW, original->tot_len - 22, PBUF_RAM);
    assert(head && tail);
    assert(pbuf_copy_partial(original, head->payload, head->len, 0) == head->len);
    assert(pbuf_copy_partial(original, tail->payload, tail->len, 22) == tail->len);
    pbuf_cat(head, tail);
    pbuf_free(original);
    return head;
}
static void check_transport(void)
{
    struct ip_hdr *ip = (void *)captured;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, captured_len - 20, PBUF_RAM);
    assert(p);
    pbuf_take(p, captured + 20, captured_len - 20);
    ip4_addr_t s = { ip->src.addr }, d = { ip->dest.addr };
    if (IPH_PROTO(ip) == IP_PROTO_ICMP)
        assert(inet_chksum(p->payload, p->len) == 0);
    else
        assert(inet_chksum_pseudo(p, IPH_PROTO(ip), p->tot_len, &s, &d) == 0);
    pbuf_free(p);
}
int main(void)
{
    lwip_init();
    evidence = fopen("internet-routing.pcap", "wb");
    assert(evidence);
    u32_t file_header[6] = { 0xa1b2c3d4, 0x00040002, 0, 0, 65535, 101 };
    assert(fwrite(file_header, sizeof(file_header), 1, evidence) == 1);
    add(&lan, "192.168.4.1", "0.0.0.0");
    add(&wan, "10.2.0.9", "10.2.0.1");
    add(&usb, "172.16.0.1", "0.0.0.0");
    netif_set_default(&wan);
    input(packet("192.168.4.2", "8.8.8.8", IP_PROTO_UDP, 1234, 53), &lan);
    assert(wan_packets == 0); /* disabled feature policy is closed */
    fail_allocation = 1;
    assert(!napt_policy_set(&lan, &wan, 1) && !napt_policy_active());
    fail_allocation = 0;
    assert(napt_policy_set(&lan, &wan, 1));
    for (int protocol = 0; protocol < 3; ++protocol)
    {
        u8_t proto = protocol == 0 ? IP_PROTO_UDP : protocol == 1 ? IP_PROTO_TCP
                                                                  : IP_PROTO_ICMP;
        unsigned before = wan_packets;
        input(packet("192.168.4.2", "8.8.8.8", proto, 1234, 53), &lan);
        assert(wan_packets == before + 1 && captured_len >= 28);
        struct ip_hdr *ip = (void *)captured;
        assert(ip->src.addr == addr("10.2.0.9").addr && IPH_TTL(ip) == 63 && inet_chksum(ip, 20) == 0);
        check_transport();
        u16_t mapped;
        memcpy(&mapped, captured + (proto == IP_PROTO_ICMP ? 24 : 20), 2);
        mapped = lwip_ntohs(mapped);
        struct pbuf *reply = packet("8.8.8.8", "10.2.0.9", proto, 53, mapped);
        if (proto == IP_PROTO_ICMP)
        {
            struct icmp_echo_hdr *icmp = (void *)((u8_t *)reply->payload + 20);
            icmp->type = ICMP_ER;
            icmp->id = lwip_htons(mapped);
            icmp->chksum = 0;
            icmp->chksum = inet_chksum(icmp, 8);
        }
        if (proto == IP_PROTO_TCP)
            reply = split(reply);
        before = lan_packets;
        input(reply, &wan);
        assert(lan_packets == before + 1);
        ip = (void *)captured;
        assert(ip->dest.addr == addr("192.168.4.2").addr && inet_chksum(ip, 20) == 0);
        check_transport();
    }
    unsigned before = wan_packets;
    input(packet("172.16.0.2", "8.8.8.8", IP_PROTO_UDP, 1234, 53), &usb);
    input(packet("10.8.8.8", "8.8.8.8", IP_PROTO_UDP, 1234, 53), &lan);
    assert(wan_packets == before); /* USB and spoofed source cannot use NAT */
    ip4_addr_t phone = addr("192.168.4.2"), external = addr("8.8.8.8"), usb_peer = addr("172.16.0.2");
    assert(napt_policy_route(NULL, &phone) == &lan);
    assert(napt_policy_route(NULL, &usb_peer) == &usb);
    assert(napt_policy_route(NULL, &external) == &wan);
    assert(napt_policy_route(netif_ip4_addr(&lan), &phone) == &lan);
    for (int malformed = 0; malformed < 4; ++malformed)
    {
        struct pbuf *p = packet("192.168.4.2", "8.8.8.8", IP_PROTO_UDP, 5000, 53);
        struct ip_hdr *h = p->payload;
        if (malformed == 0)
            IPH_OFFSET_SET(h, PP_HTONS(IP_MF));
        if (malformed == 1)
            ((struct udp_hdr *)((u8_t *)p->payload + 20))->len = lwip_htons(900);
        if (malformed == 2)
        {
            IPH_LEN_SET(h, lwip_htons(21));
            pbuf_realloc(p, 21);
        }
        if (malformed == 3)
            IPH_VHL_SET(h, 4, 6);
        IPH_CHKSUM_SET(h, 0);
        IPH_CHKSUM_SET(h, inet_chksum(h, IPH_HL_BYTES(h)));
        input(p, &lan);
    }
    assert(wan_packets == before);
    before = lan_packets;
    input(packet("8.8.8.8", "192.168.4.2", IP_PROTO_UDP, 53, 1234), &wan);
    assert(lan_packets == before); /* unsolicited routed WAN traffic blocked */
    netif_set_link_down(&wan);
    before = wan_packets;
    input(packet("192.168.4.2", "8.8.8.8", IP_PROTO_UDP, 1234, 53), &lan);
    assert(wan_packets == before);
    napt_policy_set(NULL, NULL, 0);
    netif_set_link_up(&wan);
    assert(napt_policy_set(&lan, &wan, 1));
    before = lan_packets;
    input(packet("8.8.8.8", "10.2.0.9", IP_PROTO_UDP, 53, 1234), &wan);
    assert(lan_packets == before);
    ip4_addr_t overlap = addr("192.168.4.9");
    netif_set_ipaddr(&wan, &overlap);
    assert(!napt_policy_set(&lan, &wan, 1));
    napt_policy_set(NULL, NULL, 0);
    struct napt_policy_counters c;
    napt_policy_stats(&c);
    assert(c.outbound == 3 && c.inbound == 3 && c.rejected >= 3 && usb_packets == 0);
    assert(fclose(evidence) == 0);
    puts("PASS actual lwIP IPv4 TCP/UDP/ICMP NAT round trips, TTL/checksum, isolated interfaces, spoof rejection, link loss, mapping reset and subnet conflict");
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Network policy belongs to the host, never to the portable Receiver SDK. */
#include <rtthread.h>
#include "internet_share_lubanlite.h"
#include <wlan_mgnt.h>
#include <netif/ethernetif.h>
#include <lwip/tcpip.h>
#include <lwip/dhcp.h>
#include <lwip/prot/dhcp.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/napt_policy.h>
#include <lwip/ip4.h>
#include <lwip/timeouts.h>
#include <lwip/sys.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* The share structure and lwIP operations are owned by the TCP/IP core.
 * The probe command carries a copied address and completes before its stack
 * storage is reused. No socket wait occurs on a CarPlay or TCP/IP worker. */
static struct
{
    struct netif *wan, *lan;
    ip4_addr_t address, lan_address, lan_mask, wan_mask, gateway;
    ip_addr_t resolver;
    int dhcp_started, probe_ok, sharing, initialized, checked, link;
    u32_t checked_at, generation;
} share;

/* Single-core platform: only a small copied snapshot crosses into LVGL.
 * Socket operations and TCP/IP callbacks always stay outside this section. */
static carplay_internet_status_t cached_status;
static struct rt_semaphore probe_wake;
static int service_ready, check_pending, check_running;

void carplay_internet_status(carplay_internet_status_t *out)
{
    rt_base_t level = rt_hw_interrupt_disable();
    *out = cached_status;
    out->available = service_ready;
    out->checking = check_pending || check_running;
    rt_hw_interrupt_enable(level);
}

bool carplay_internet_check(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    int accepted = service_ready && !check_pending && !check_running;
    if (accepted)
    {
        check_pending = 1;
        rt_sem_release(&probe_wake);
    }
    rt_hw_interrupt_enable(level);
    return accepted != 0;
}

static void publish_status(void)
{
    carplay_internet_status_t next = { 0 };
    next.device = share.wan != NULL;
    next.link = share.wan && netif_is_link_up(share.wan);
    next.addressed = share.wan && netif_is_up(share.wan) && next.link &&
                     !ip4_addr_isany_val(*netif_ip4_addr(share.wan));
    next.route_ready = next.addressed && !ip4_addr_isany_val(share.gateway);
    snprintf(next.probe_url, sizeof(next.probe_url), "http://%s%s", RTCARPLAY_INTERNET_PROBE_HOST, RTCARPLAY_INTERNET_PROBE_PATH);
    next.checked = share.checked;
    next.reachable = share.probe_ok;
    next.sharing = share.sharing;
    next.age_ms = share.checked ? (u32_t)(sys_now() - share.checked_at) : 0;
    ip4addr_ntoa_r(&share.address, next.address, sizeof(next.address));
    ip4addr_ntoa_r(&share.wan_mask, next.mask, sizeof(next.mask));
    ip4addr_ntoa_r(&share.gateway, next.gateway, sizeof(next.gateway));
    if (IP_IS_V4(&share.resolver))
        ip4addr_ntoa_r(ip_2_ip4(&share.resolver), next.dns, sizeof(next.dns));
    rt_base_t level = rt_hw_interrupt_disable();
    cached_status = next;
    rt_hw_interrupt_enable(level);
}

static struct netif *device_netif(const char *name, int ap)
{
    rt_device_t device = rt_device_find(name);
    if (!device)
        return NULL;
    struct netif *entry;
    NETIF_FOREACH(entry)
    {
        struct eth_device *eth = entry->state;
        if (eth && (ap ? entry->name[0] == 'w' && eth->parent.user_data == device : (rt_device_t)eth == device))
            return entry;
    }
    return NULL;
}

static int addressed(struct netif *netif)
{
    return netif && netif_is_up(netif) && netif_is_link_up(netif) &&
           !ip4_addr_isany_val(*netif_ip4_addr(netif));
}

/* DHCP server calls this on its core thread. Offer only a checked route.
 * Existing clients may retain a route until their short lease is renewed. */
unsigned carplay_dhcp_internet_options(struct netif *netif, u8_t *out)
{
    if (!share.sharing || netif != share.lan || !napt_policy_active() || (u32_t)(sys_now() - share.checked_at) > 45000U)
        return 0;
    out[0] = 3;
    out[1] = 4;
    memcpy(out + 2, &netif_ip4_addr(netif)->addr, 4);
    out[6] = 6;
    out[7] = 4;
    memcpy(out + 8, &ip_2_ip4(&share.resolver)->addr, 4);
    return 12;
}

static void close_route(void)
{
    napt_policy_set(NULL, NULL, 0);
    share.sharing = 0;
}

static void invalidate_probe(void)
{
    close_route();
    share.probe_ok = 0;
    share.checked = 0;
    share.generation++;
}

static void network_refresh(void)
{
    struct netif *wan = device_netif(RTCARPLAY_INTERNET_WAN, 0);
    struct netif *lan = device_netif(RT_WLAN_DEVICE_AP_NAME, 1);
    int link = wan && netif_is_up(wan) && netif_is_link_up(wan);
    const ip_addr_t *resolver = dns_getserver(0);
    if (wan != share.wan || link != share.link ||
        (wan && (!ip4_addr_cmp(&share.address, netif_ip4_addr(wan)) ||
                 !ip4_addr_cmp(&share.wan_mask, netif_ip4_netmask(wan)) ||
                 !ip4_addr_cmp(&share.gateway, netif_ip4_gw(wan)))) ||
        !ip_addr_cmp(&share.resolver, resolver))
        invalidate_probe();
    if (wan != share.wan)
        share.dhcp_started = 0;
    if (lan != share.lan || !addressed(lan) ||
        (lan && (!ip4_addr_cmp(&share.lan_address, netif_ip4_addr(lan)) ||
                 !ip4_addr_cmp(&share.lan_mask, netif_ip4_netmask(lan)))))
        close_route();
    share.wan = wan;
    share.lan = lan;
    share.link = link;
    share.resolver = *resolver;
    ip4_addr_set_zero(&share.address);
    ip4_addr_set_zero(&share.wan_mask);
    ip4_addr_set_zero(&share.gateway);
    if (wan)
    {
        share.address = *netif_ip4_addr(wan);
        share.wan_mask = *netif_ip4_netmask(wan);
        share.gateway = *netif_ip4_gw(wan);
        if (!share.dhcp_started)
        {
            struct dhcp *client = netif_dhcp_data(wan);
            if ((client && client->state != DHCP_STATE_OFF) || dhcp_start(wan) == ERR_OK)
                share.dhcp_started = 1;
        }
        if (netif_default != wan)
            netif_set_default(wan);
    }
    if (lan)
    {
        share.lan_address = *netif_ip4_addr(lan);
        share.lan_mask = *netif_ip4_netmask(lan);
    }
    if (!addressed(wan) || (share.probe_ok && (u32_t)(sys_now() - share.checked_at) > 45000U))
    {
        close_route();
        share.probe_ok = 0;
    }
    int can_share = share.probe_ok && addressed(lan) && IP_IS_V4(&share.resolver) &&
                    !ip_addr_isany(&share.resolver) && !ip_addr_ismulticast(&share.resolver) &&
                    !ip4_addr_isloopback(ip_2_ip4(&share.resolver)) && ip4_route(ip_2_ip4(&share.resolver)) == wan;
    if (!can_share)
        close_route();
    else if (!share.sharing || !napt_policy_active())
        share.sharing = napt_policy_set(lan, wan, 1) != 0;
    publish_status();
}

static void network_tick(void *unused)
{
    (void)unused;
    network_refresh();
    sys_timeout(1000, network_tick, NULL);
}

struct probe_command
{
    struct rt_semaphore done;
    ip4_addr_t source;
    u32_t generation;
    int result, apply, status;
};
static void probe_command(void *opaque)
{
    struct probe_command *command = opaque;
    if (!share.initialized)
    {
        share.initialized = 1;
        network_tick(NULL);
    }
    else
        network_refresh();
    if (command->status)
    {
        struct napt_policy_counters c;
        napt_policy_stats(&c);
        rt_kprintf("internet_share: WAN=%s link=%d address=%s probe=%d sharing=%d\n",
                   RTCARPLAY_INTERNET_WAN, share.wan ? netif_is_link_up(share.wan) : 0,
                   ip4addr_ntoa(&share.address), share.probe_ok, share.sharing);
        rt_kprintf("internet_share: NAT out=%u in=%u rejected=%u; DNS via upstream; iPhone route NOT_VERIFIED\n",
                   (unsigned)c.outbound, (unsigned)c.inbound, (unsigned)c.rejected);
    }
    else if (!command->apply)
    {
        command->generation = share.generation;
        ip4_addr_set_zero(&command->source);
        if (addressed(share.wan) && !ip4_addr_isany_val(*netif_ip4_gw(share.wan)))
            command->source = *netif_ip4_addr(share.wan);
    }
    else if (command->generation == share.generation && addressed(share.wan) && ip4_addr_cmp(&command->source, netif_ip4_addr(share.wan)))
    {
        share.checked_at = sys_now();
        share.checked = 1;
        share.probe_ok = command->result != 0;
        network_refresh();
    }
    publish_status();
    rt_sem_release(&command->done);
}

static int run_command(struct probe_command *command)
{
    if (rt_sem_init(&command->done, "inetcmd", 0, RT_IPC_FLAG_FIFO) != RT_EOK)
        return 0;
    err_t error = tcpip_callback(probe_command, command);
    if (error == ERR_OK)
        rt_sem_take(&command->done, RT_WAITING_FOREVER);
    rt_sem_detach(&command->done);
    return error == ERR_OK;
}

static int probe_http(const ip4_addr_t *source)
{
    struct addrinfo hints, *resolved = NULL;
    struct sockaddr_in local;
    char response[512];
    static const char request[] = "GET " RTCARPLAY_INTERNET_PROBE_PATH " HTTP/1.0\r\nHost: " RTCARPLAY_INTERNET_PROBE_HOST "\r\n\r\n";
    int fd = -1, ok = 0, size = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    /* A genuine HTTP 204 is required; DNS resolution, gateway or link alone
     * never opens sharing. Captive portals/redirects fail this check. */
    if (lwip_getaddrinfo(RTCARPLAY_INTERNET_PROBE_HOST, "80", &hints, &resolved) != 0)
        return 0;
    fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto done;
#if LWIP_SO_SNDRCVTIMEO_NONSTANDARD
    int timeout = 3000;
#else
    struct timeval timeout = { 3, 0 };
#endif
    if (lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
        goto done;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = source->addr;
    if (lwip_bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0)
        goto done;
    u32_t nonblocking = 1;
    if (lwip_ioctl(fd, FIONBIO, &nonblocking) != 0)
        goto done;
    int connected = lwip_connect(fd, resolved->ai_addr, resolved->ai_addrlen);
    if (connected != 0)
    {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK)
            goto done;
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        struct timeval limit = { 3, 0 };
        int error = 0;
        socklen_t error_len = sizeof(error);
        if (lwip_select(fd + 1, NULL, &writable, NULL, &limit) != 1 ||
            lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_len) != 0 || error != 0)
            goto done;
    }
    nonblocking = 0;
    if (lwip_ioctl(fd, FIONBIO, &nonblocking) != 0 ||
        lwip_send(fd, request, sizeof(request) - 1, 0) != sizeof(request) - 1)
        goto done;
    u32_t started = sys_now();
    while (size < (int)sizeof(response) - 1 && (u32_t)(sys_now() - started) < 6000U)
    {
        int count = lwip_recv(fd, response + size, sizeof(response) - 1 - size, 0);
        if (count <= 0)
            break;
        size += count;
        response[size] = '\0';
        if (strstr(response, "\r\n\r\n"))
            break;
    }
    if (size >= 16 && (!memcmp(response, "HTTP/1.1 204 ", 13) || !memcmp(response, "HTTP/1.0 204 ", 13)) && strstr(response, "\r\n\r\n"))
        ok = 1;
done:
    if (fd >= 0)
        lwip_close(fd);
    if (resolved)
        lwip_freeaddrinfo(resolved);
    return ok;
}

static void probe_worker(void *unused)
{
    (void)unused;
    for (;;)
    {
        rt_base_t level = rt_hw_interrupt_disable();
        check_pending = 0;
        check_running = 1;
        rt_hw_interrupt_enable(level);
        struct probe_command command;
        memset(&command, 0, sizeof(command));
        if (run_command(&command) && !ip4_addr_isany_val(command.source))
        {
            command.result = probe_http(&command.source);
            command.apply = 1;
            run_command(&command);
        }
        level = rt_hw_interrupt_disable();
        check_running = 0;
        rt_hw_interrupt_enable(level);
        rt_sem_take(&probe_wake, rt_tick_from_millisecond(15000));
    }
}

static int internet_share_init(void)
{
    if (rt_sem_init(&probe_wake, "inetwake", 0, RT_IPC_FLAG_FIFO) != RT_EOK)
        return -RT_ENOMEM;
    rt_thread_t worker = rt_thread_create("inetshare", probe_worker, NULL, 6144, 24, 10);
    if (!worker)
    {
        rt_sem_detach(&probe_wake);
        return -RT_ENOMEM;
    }
    service_ready = 1;
    if (rt_thread_startup(worker) != RT_EOK)
    {
        service_ready = 0;
        rt_thread_delete(worker);
        rt_sem_detach(&probe_wake);
        return -RT_ERROR;
    }
    return RT_EOK;
}
INIT_APP_EXPORT(internet_share_init);

static void internet_share(int argc, char **argv)
{
    struct probe_command command;
    memset(&command, 0, sizeof(command));
    (void)argc;
    (void)argv;
    command.status = 1;
    if (!run_command(&command))
        rt_kprintf("internet_share: TCP/IP core unavailable\n");
}
MSH_CMD_EXPORT(internet_share, Ethernet to CarPlay AP sharing status);

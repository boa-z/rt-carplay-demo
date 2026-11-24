#include <rtthread.h>
#include <rtdevice.h>
#include <lwip/tcpip.h>
#include <lwip/dhcp.h>
#include <lwip/prot/dhcp.h>
#include <lwip/dns.h>
#include <lwip/timeouts.h>
#include <netif/ethernetif.h>
#include <wlan_mgnt.h>
#include <string.h>
#include <stdio.h>
#include "network_manager.h"

#ifndef RTCARPLAY_INTERNET_WAN
#define RTCARPLAY_INTERNET_WAN "e0"
#endif

static struct
{
    struct netif *device;
    ip4_addr_t ip, gateway, mask;
    ip_addr_t dns;
    char name[8];
} lease;
static bool changing;
static carplay_wan_t preference;

static struct netif *device_netif(const char *name, bool wireless)
{
    rt_device_t device = rt_device_find(name);
    if (!device)
        return NULL;
    struct netif *entry;
    NETIF_FOREACH(entry)
    {
        struct eth_device *eth = entry->state;
        if (eth && (wireless ? entry->name[0] == 'w' && eth->parent.user_data == device : (rt_device_t)eth == device))
            return entry;
    }
    return NULL;
}

static bool addressed(struct netif *entry)
{
    return entry && netif_is_up(entry) && netif_is_link_up(entry) &&
           !ip4_addr_isany_val(*netif_ip4_addr(entry));
}

static bool eligible(struct netif *entry, bool allow_local)
{
    return addressed(entry) && (allow_local || !ip4_addr_isany_val(*netif_ip4_gw(entry)));
}

static carplay_wan_t refresh(bool allow_local)
{
    struct netif *ethernet = device_netif(RTCARPLAY_INTERNET_WAN, false);
    struct netif *sta = device_netif(RT_WLAN_DEVICE_STA_NAME, true);
    if (ethernet && netif_is_up(ethernet) && !addressed(ethernet))
    {
        struct dhcp *client = netif_dhcp_data(ethernet);
        if (!client || client->state == DHCP_STATE_OFF)
            dhcp_start(ethernet);
    }
    carplay_wan_t choice = CARPLAY_WAN_NONE;
    struct netif *selected = NULL;
    if (preference != CARPLAY_WAN_STA && eligible(ethernet, allow_local))
    {
        selected = ethernet;
        choice = CARPLAY_WAN_ETHERNET;
    }
    else if (preference != CARPLAY_WAN_ETHERNET && eligible(sta, allow_local))
    {
        selected = sta;
        choice = CARPLAY_WAN_STA;
    }
    if (!lease.device && netif_default != selected)
        netif_set_default(selected);
    return choice;
}

bool carplay_network_acquire(bool allow_local)
{
    LOCK_TCPIP_CORE();
    bool ready = !changing && !lease.device && refresh(allow_local) != CARPLAY_WAN_NONE;
    if (ready)
    {
        lease.device = netif_default;
        lease.ip = *netif_ip4_addr(lease.device);
        lease.mask = *netif_ip4_netmask(lease.device);
        lease.gateway = *netif_ip4_gw(lease.device);
        lease.dns = *dns_getserver(0);
        snprintf(lease.name, sizeof(lease.name), "%c%c%u", lease.device->name[0],
                 lease.device->name[1], (unsigned)lease.device->num);
    }
    UNLOCK_TCPIP_CORE();
    return ready;
}

const char *carplay_network_lease_name(void)
{
    return lease.name;
}

bool carplay_network_change_begin(void)
{
    LOCK_TCPIP_CORE();
    bool ready = !changing && !lease.device;
    if (ready)
        changing = true;
    UNLOCK_TCPIP_CORE();
    return ready;
}

void carplay_network_change_end(void)
{
    LOCK_TCPIP_CORE();
    changing = false;
    refresh(false);
    UNLOCK_TCPIP_CORE();
}

bool carplay_network_valid(void)
{
    LOCK_TCPIP_CORE();
    bool valid = lease.device && lease.device == netif_default && addressed(netif_default) &&
                 ip4_addr_cmp(&lease.ip, netif_ip4_addr(netif_default)) &&
                 ip4_addr_cmp(&lease.mask, netif_ip4_netmask(netif_default)) &&
                 ip4_addr_cmp(&lease.gateway, netif_ip4_gw(netif_default)) &&
                 ip_addr_cmp(&lease.dns, dns_getserver(0));
    UNLOCK_TCPIP_CORE();
    return valid;
}

void carplay_network_release(void)
{
    LOCK_TCPIP_CORE();
    lease.device = NULL;
    refresh(false);
    UNLOCK_TCPIP_CORE();
}

carplay_wan_t carplay_network_wan(void)
{
    LOCK_TCPIP_CORE();
    carplay_wan_t choice = refresh(false);
    UNLOCK_TCPIP_CORE();
    return choice;
}

static void route_tick(void *unused)
{
    (void)unused;
    refresh(false);
    sys_timeout(1000, route_tick, NULL);
}

static int network_manager_init(void)
{
    return tcpip_callback(route_tick, NULL) == ERR_OK ? RT_EOK : -RT_ERROR;
}
INIT_APP_EXPORT(network_manager_init);

static int network_command(int argc, char **argv)
{
    const char *names[] = { "auto", "ethernet", "wifi" };
    if (argc == 3 && !strcmp(argv[1], "route"))
    {
        for (unsigned index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        {
            if (strcmp(argv[2], names[index]))
                continue;
            LOCK_TCPIP_CORE();
            bool allowed = !changing && !lease.device;
            if (allowed)
            {
                preference = (carplay_wan_t)index;
                refresh(false);
            }
            UNLOCK_TCPIP_CORE();
            return allowed ? RT_EOK : -RT_EBUSY;
        }
    }
    else if (argc == 1 || (argc == 2 && !strcmp(argv[1], "status")))
    {
        LOCK_TCPIP_CORE();
        carplay_wan_t selected = refresh(false);
        carplay_wan_t policy = preference;
        bool dns = !ip_addr_isany(dns_getserver(0));
        UNLOCK_TCPIP_CORE();
        rt_kprintf("Policy %s, IPv4 route %s, DNS configured %s\n", names[policy],
                   selected == CARPLAY_WAN_NONE ? "none" : names[selected], dns ? "yes" : "no");
        return RT_EOK;
    }
    rt_kprintf("usage: carplay_net status | route auto|ethernet|wifi\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(network_command, carplay_net, Select or inspect the CarPlay board WAN);

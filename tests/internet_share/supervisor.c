/* SPDX-License-Identifier: GPL-3.0-or-later */
#define RTCARPLAY_INTERNET_SHARE      1
#define RTCARPLAY_INTERNET_WAN        "e0"
#define RTCARPLAY_INTERNET_PROBE_HOST "probe.example"
#define RTCARPLAY_INTERNET_PROBE_PATH "/generate_204"
#include "../../platform/internet_share_lubanlite.c"
#include "lwip/init.h"
#include "lwip/ip4.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "network_manager.h"
#include <assert.h>
static struct netif lan, wan, sta, ncm;
static struct eth_device lan_device, wan_device, sta_device, ncm_device;
static struct rt_device radio, sta_radio;
static ip_addr_t resolver;
static u32_t clock_ms;
static const char *http_response;
static size_t response_offset;
static int socket_fail, connect_timeout, connect_error, bound, closed;
static struct dhcp existing_client;
static int dhcp_starts;
u32_t sys_now(void)
{
    return clock_ms;
}
const ip_addr_t *dns_getserver(u8_t index)
{
    (void)index;
    return &resolver;
}
struct dhcp *test_dhcp_data(struct netif *n)
{
    assert(n == &wan);
    return &existing_client;
}
err_t dhcp_start(struct netif *n)
{
    assert(n == &wan);
    dhcp_starts++;
    return ERR_OK;
}
rt_device_t rt_device_find(const char *name)
{
    if (!strcmp(name, "e0"))
        return &wan_device.parent;
    if (!strcmp(name, RT_WLAN_DEVICE_AP_NAME))
        return &radio;
    if (!strcmp(name, RT_WLAN_DEVICE_STA_NAME))
        return &sta_radio;
    return NULL;
}
rt_base_t rt_hw_interrupt_disable(void)
{
    return 0;
}
void rt_hw_interrupt_enable(rt_base_t level)
{
    (void)level;
}
int rt_tick_from_millisecond(int ms)
{
    return ms;
}
int rt_sem_init(struct rt_semaphore *s, const char *n, int value, int flags)
{
    (void)n;
    (void)flags;
    s->count = value;
    return 0;
}
int rt_sem_release(struct rt_semaphore *s)
{
    s->count++;
    return 0;
}
int rt_sem_take(struct rt_semaphore *s, int wait)
{
    (void)wait;
    assert(s->count);
    s->count--;
    return 0;
}
int rt_sem_detach(struct rt_semaphore *s)
{
    assert(!s->count);
    return 0;
}
int rt_kprintf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}
rt_thread_t rt_thread_create(const char *n, void (*f)(void *), void *c, unsigned stack, unsigned priority, unsigned ticks)
{
    (void)n;
    (void)f;
    (void)c;
    (void)stack;
    (void)priority;
    (void)ticks;
    return NULL;
}
int rt_thread_startup(rt_thread_t t)
{
    (void)t;
    return 0;
}
int rt_thread_delete(rt_thread_t t)
{
    (void)t;
    return 0;
}
void rt_thread_mdelay(int ms)
{
    clock_ms += ms;
}
err_t tcpip_callback(void (*fn)(void *), void *ctx)
{
    fn(ctx);
    return ERR_OK;
}
int test_socket(int a, int b, int c)
{
    (void)a;
    (void)b;
    (void)c;
    response_offset = 0;
    return socket_fail ? -1 : 3;
}
int test_setsockopt(int fd, int level, int opt, const void *value, socklen_t length)
{
    (void)fd;
    (void)level;
    (void)opt;
    (void)value;
    (void)length;
    return 0;
}
int test_getsockopt(int fd, int level, int opt, void *value, socklen_t *length)
{
    (void)fd;
    (void)level;
    (void)opt;
    (void)length;
    *(int *)value = connect_error;
    return 0;
}
int test_bind(int fd, const struct sockaddr *a, socklen_t len)
{
    (void)fd;
    (void)len;
    assert(((const struct sockaddr_in *)a)->sin_addr.s_addr == netif_ip4_addr(&wan)->addr);
    bound++;
    return 0;
}
int test_ioctl(int fd, long request, void *value)
{
    (void)fd;
    (void)request;
    (void)value;
    return 0;
}
int test_select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *limit)
{
    (void)nfds;
    (void)r;
    (void)w;
    (void)e;
    assert(limit->tv_sec == 3);
    return connect_timeout ? 0 : 1;
}
int test_connect(int fd, const struct sockaddr *a, socklen_t len)
{
    (void)fd;
    (void)a;
    (void)len;
    errno = EINPROGRESS;
    return -1;
}
ssize_t test_send(int fd, const void *bytes, size_t length, int flags)
{
    (void)fd;
    (void)flags;
    assert(strstr(bytes, "Host: probe.example"));
    return length;
}
ssize_t test_recv(int fd, void *bytes, size_t max, int flags)
{
    (void)fd;
    (void)flags;
    size_t left = strlen(http_response) - response_offset;
    size_t n = left > 7 ? 7 : left;
    if (n > max)
        n = max;
    memcpy(bytes, http_response + response_offset, n);
    response_offset += n;
    return n;
}
int test_close(int fd)
{
    (void)fd;
    closed++;
    return 0;
}
int test_getaddrinfo(const char *host, const char *service, const struct addrinfo *hints, struct addrinfo **out)
{
    static struct addrinfo result;
    static struct sockaddr_in remote;
    (void)hints;
    assert(!strcmp(host, "probe.example") && !strcmp(service, "80"));
    result.ai_addr = (void *)&remote;
    result.ai_addrlen = sizeof(remote);
    *out = &result;
    return 0;
}
void test_freeaddrinfo(struct addrinfo *p)
{
    (void)p;
}
static ip4_addr_t address(const char *s)
{
    ip4_addr_t a;
    assert(ip4addr_aton(s, &a));
    return a;
}
static err_t setup(struct netif *n)
{
    n->flags = NETIF_FLAG_BROADCAST;
    n->mtu = 1500;
    return ERR_OK;
}
static void add(struct netif *n, void *state, const char *ip, const char *gateway, char prefix)
{
    ip4_addr_t a = address(ip), m = address("255.255.255.0"), g = address(gateway);
    assert(netif_add(n, &a, &m, &g, state, setup, ip4_input));
    n->name[0] = prefix;
    netif_set_up(n);
    netif_set_link_up(n);
}
static void result(int ok)
{
    struct probe_command c;
    memset(&c, 0, sizeof(c));
    assert(run_command(&c));
    c.apply = 1;
    c.result = ok;
    assert(run_command(&c));
}
int main(void)
{
    lwip_init();
    lan_device.parent.user_data = &radio;
    sta_device.parent.user_data = &sta_radio;
    add(&lan, &lan_device, "192.168.4.1", "0.0.0.0", 'w');
    add(&wan, &wan_device, "10.2.0.9", "10.2.0.1", 'e');
    add(&sta, &sta_device, "192.168.5.2", "192.168.5.1", 'w');
    add(&ncm, &ncm_device, "192.168.6.1", "192.168.6.254", 'u');
    ip_addr_set_ip4_u32(&resolver, address("8.8.8.8").addr);
    existing_client.state = DHCP_STATE_BOUND;
    netif_set_default(&ncm);
    assert(carplay_network_wan() == CARPLAY_WAN_ETHERNET && netif_default == &wan);
    netif_set_link_down(&wan);
    assert(carplay_network_wan() == CARPLAY_WAN_STA && netif_default == &sta);
    netif_set_link_down(&sta);
    netif_set_gw(&lan, netif_ip4_gw(&sta));
    assert(carplay_network_wan() == CARPLAY_WAN_NONE && netif_default == NULL);
    netif_set_gw(&lan, IP4_ADDR_ANY4);
    netif_set_link_up(&wan);
    ip4_addr_t wan_ip = *netif_ip4_addr(&wan);
    netif_set_ipaddr(&wan, IP4_ADDR_ANY4);
    assert(carplay_network_wan() == CARPLAY_WAN_NONE);
    u8_t options[12];
    result(0);
    assert(!share.sharing && !carplay_dhcp_internet_options(&lan, options));
    assert(!dhcp_starts); /* Do not restart an SDK-owned lease or discovery. */
    existing_client.state = DHCP_STATE_OFF;
    assert(carplay_network_wan() == CARPLAY_WAN_NONE);
    assert(dhcp_starts == 1);
    netif_set_ipaddr(&wan, &wan_ip);
    assert(carplay_network_wan() == CARPLAY_WAN_ETHERNET && netif_default == &wan);
    ip4_addr_t wan_gateway = *netif_ip4_gw(&wan);
    netif_set_gw(&wan, IP4_ADDR_ANY4);
    assert(!carplay_network_acquire(false));
    assert(carplay_network_acquire(true) && carplay_network_valid() && netif_default == &wan);
    carplay_network_release();
    assert(netif_default == NULL);
    netif_set_gw(&wan, &wan_gateway);
    assert(carplay_network_wan() == CARPLAY_WAN_ETHERNET);
    result(1);
    assert(share.sharing && carplay_dhcp_internet_options(&lan, options) == 12);
    assert(options[0] == 3 && options[6] == 6 && !memcmp(options + 2, &netif_ip4_addr(&lan)->addr, 4) && !memcmp(options + 8, &ip_2_ip4(&resolver)->addr, 4));
    assert(!carplay_dhcp_internet_options(&wan, options));
    netif_set_link_down(&wan);
    assert(!napt_policy_active() && !carplay_dhcp_internet_options(&lan, options));
    network_tick(NULL);
    assert(!share.sharing && !share.probe_ok);
    netif_set_link_up(&wan);
    network_tick(NULL);
    assert(!share.sharing);
    result(1);
    assert(share.sharing);
    struct probe_command stale;
    memset(&stale, 0, sizeof(stale));
    assert(run_command(&stale));
    netif_set_link_down(&wan);
    network_tick(NULL);
    netif_set_link_up(&wan);
    network_tick(NULL);
    stale.apply = 1;
    stale.result = 1;
    assert(run_command(&stale));
    assert(!share.sharing);
    result(1);
    assert(share.sharing);
    clock_ms += 46000;
    network_tick(NULL);
    assert(!share.sharing);
    result(1);
    assert(share.sharing);
    ip4_addr_t replacement = address("10.2.0.10");
    netif_set_ipaddr(&wan, &replacement);
    network_tick(NULL);
    assert(!share.sharing);
    result(1);
    assert(share.sharing);
    netif_set_down(&lan);
    network_tick(NULL);
    assert(!share.sharing && !napt_policy_active());
    result(1);
    assert(share.probe_ok && !share.sharing); /* Board connectivity without AP. */
    carplay_internet_status_t status;
    carplay_internet_status(&status);
    assert(status.link && status.addressed && status.checked && status.reachable && !status.sharing);
    assert(!strcmp(status.address, "10.2.0.10"));
    netif_set_up(&lan);
    network_tick(NULL);
    assert(share.sharing);
    ip_addr_set_ip4_u32(&resolver, netif_ip4_addr(&lan)->addr);
    network_tick(NULL);
    result(1);
    assert(!share.sharing);
    ip_addr_set_ip4_u32(&resolver, address("8.8.8.8").addr);
    network_tick(NULL);
    result(1);
    assert(share.sharing);
    clock_ms = 0xfffffff0U;
    result(1);
    clock_ms += 100;
    network_tick(NULL);
    assert(share.sharing);
    clock_ms += 46000;
    network_tick(NULL);
    assert(!share.sharing);
    result(1);
    assert(share.sharing);
    http_response = "HTTP/1.1 204 No Content\r\nX-Test: valid\r\n\r\n";
    assert(probe_http(netif_ip4_addr(&wan)));
    http_response = "HTTP/1.1 302 Found\r\nLocation: http://login/\r\n\r\n";
    assert(!probe_http(netif_ip4_addr(&wan)));
    http_response = "HTTP/1.1 200 OK\r\n\r\nportal";
    assert(!probe_http(netif_ip4_addr(&wan)));
    http_response = "HTTP/1.1 204 No Content\r\n";
    assert(!probe_http(netif_ip4_addr(&wan)));
    connect_timeout = 1;
    assert(!probe_http(netif_ip4_addr(&wan)));
    connect_timeout = 0;
    connect_error = 1;
    assert(!probe_http(netif_ip4_addr(&wan)));
    connect_error = 0;
    socket_fail = 1;
    assert(!probe_http(netif_ip4_addr(&wan)));
    socket_fail = 0;
    assert(bound == closed && bound == 6);
    assert(internet_share_init() == -RT_ENOMEM);
    netif_remove(&wan);
    assert(!napt_policy_active());
    struct probe_command removed;
    memset(&removed, 0, sizeof(removed));
    assert(run_command(&removed));
    assert(!share.wan && !share.sharing && ip4_addr_isany_val(removed.source));
    carplay_internet_status_t final;
    carplay_internet_status(&final);
    assert(!final.device && !final.reachable && !final.checked);
    assert(!carplay_internet_check());
    service_ready = 1;
    assert(carplay_internet_check());
    assert(!carplay_internet_check());
    carplay_internet_status(&final);
    assert(final.checking);
    assert(probe_wake.count == 1);
    assert(rt_sem_take(&probe_wake, 0) == 0);
    check_pending = 0;
    check_running = 1;
    assert(!carplay_internet_check());
    check_running = 0;
    service_ready = 0;
    close_route();
    puts("PASS actual sharing supervisor: checked DHCP options, WAN binding, captive rejection, partial HTTP, timeouts, stale-result rejection, link/address changes, expiry, AP stop and allocation failure");
    return 0;
}

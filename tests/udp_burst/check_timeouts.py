# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real static timeout pool with the actual board port budget."""
from pathlib import Path
import os
import resource
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[4]
lwip = root / 'kernel/rt-thread/components/net/lwip/lwip-2.1.2'
port = root / 'kernel/rt-thread/components/net/lwip/port/lwipopts.h'
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work / 'sys').mkdir()
    (work / 'sys/errno.h').write_text('#include <errno.h>\n')
    (work / 'rtthread.h').write_text('#include <string.h>\n#define rt_memcpy memcpy\n')
    (work / 'rtconfig.h').write_text('''
#define RT_USING_LWIP_IPV6 1
#define RT_LWIP_TCP 1
#define RT_LWIP_UDP 1
#define RT_LWIP_DHCP 1
#define RT_LWIP_DNS 1
''')
    # Keep the actual port's protocol options and budget; only remove RTOS
    # transport and locking to run its static allocator/scheduler on the host.
    (work / 'lwipopts.h').write_text('#include "' + port.as_posix() + '"\n' + '''
#undef NO_SYS
#define NO_SYS 1
#undef SYS_LIGHTWEIGHT_PROT
#define SYS_LIGHTWEIGHT_PROT 0
#undef LWIP_NETCONN
#define LWIP_NETCONN 0
#undef LWIP_SOCKET
#define LWIP_SOCKET 0
#undef LWIP_TCPIP_CORE_LOCKING
#define LWIP_TCPIP_CORE_LOCKING 0
#undef LWIP_TCPIP_CORE_LOCKING_INPUT
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0
#undef LWIP_STATS
#define LWIP_STATS 0
#undef LWIP_NETIF_API
#define LWIP_NETIF_API 0
#undef MEM_ALIGNMENT
#define MEM_ALIGNMENT 8
#define PBUF_POOL_FREE_OOSEQ 0
#undef LWIP_MDNS_RESPONDER
#define LWIP_MDNS_RESPONDER 0
#include <string.h>
#undef MEMCPY
#define MEMCPY(dst, src, len) memcpy(dst, src, len)
''')
    (work / 'budget.c').write_text('''
#include "lwip/memp.h"
#include "lwip/timeouts.h"
#include "netif/ppp/ppp_opts.h"
#include <assert.h>
#include <stdio.h>
static u32_t now;
static unsigned calls;
/* Protocol processing is outside this allocator/scheduler harness. */
#define UNUSED_TIMER(name) void name(void) { assert(!"unexpected protocol timer"); }
UNUSED_TIMER(tcp_tmr)
UNUSED_TIMER(etharp_tmr)
UNUSED_TIMER(dhcp_coarse_tmr)
UNUSED_TIMER(dhcp_fine_tmr)
UNUSED_TIMER(dns_tmr)
UNUSED_TIMER(nd6_tmr)
UNUSED_TIMER(ip6_reass_tmr)
UNUSED_TIMER(mld6_tmr)
u32_t sys_now(void) { return now; }
static void tick(void *arg) {
    ++calls;
    sys_timeout(1000, tick, arg);
}
int main(void) {
    memp_init();
    const unsigned owners = LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8;
    /* Core timers plus three mDNS netifs, NAPT, the monitor and headroom. */
    for (unsigned i = 0; i < owners; ++i) sys_timeout(1000, tick, (void *)(uintptr_t)(i + 1));
    for (unsigned cycle = 0; cycle < 200; ++cycle) {
        now += 1000;
        sys_check_timeouts();
        assert(calls == (cycle + 1) * owners);
    }
    for (unsigned i = 0; i < owners; ++i) sys_untimeout(tick, (void *)(uintptr_t)(i + 1));
    void *slots[MEMP_NUM_SYS_TIMEOUT];
    for (unsigned i = 0; i < MEMP_NUM_SYS_TIMEOUT; ++i) {
        slots[i] = memp_malloc(MEMP_SYS_TIMEOUT); assert(slots[i]);
    }
    assert(!memp_malloc(MEMP_SYS_TIMEOUT));
    for (unsigned i = 0; i < MEMP_NUM_SYS_TIMEOUT; ++i) memp_free(MEMP_SYS_TIMEOUT, slots[i]);
    printf("actual timeout pool: internal=%u total=%u, 200 rearm cycles and full reclamation passed\\n",
           (unsigned)LWIP_NUM_SYS_TIMEOUT_INTERNAL, (unsigned)MEMP_NUM_SYS_TIMEOUT);
}
''')
    for extra in (8, 0):
        executable = work / ('budget-' + str(extra))
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-g', '-fno-pie', '-no-pie',
                   '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
                   '-DRT_LWIP_SYS_TIMEOUT_EXTRA=' + str(extra),
                   '-I' + str(work), '-I' + str(here), '-I' + str(lwip / 'src/include'),
                   str(work / 'budget.c'), str(lwip / 'src/core/memp.c'),
                   str(lwip / 'src/core/mem.c'),
                   str(lwip / 'src/core/timeouts.c'), '-Wl,--gc-sections', '-o', str(executable)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        if extra:
            assert result.returncode == 0, result.stdout + result.stderr
            print(result.stdout.strip())
        else:
            assert result.returncode != 0 and 'pool MEMP_SYS_TIMEOUT is empty' in result.stdout + result.stderr, result
            print('negative control: core-only timeout budget reproduces pool exhaustion')
    # Exercise the actual DAD gate separately: link-down cannot initiate it;
    # enabling the transport emits a real NS before the address becomes valid.
    (work / 'dad.c').write_text('''
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/nd6.h"
#include "lwip/ethip6.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned ns;
u32_t sys_now(void) { return 0; }
static err_t output(struct netif *n, struct pbuf *p) {
    (void)n;
    unsigned char bytes[128];
    unsigned length = pbuf_copy_partial(p, bytes, sizeof(bytes), 0);
    if (length > 54 && bytes[12] == 0x86 && bytes[13] == 0xdd &&
        bytes[20] == 58 && bytes[54] == 135) ++ns;
    return ERR_OK;
}
static err_t setup(struct netif *n) {
    static const unsigned char mac[6] = {2,0,0,0,0,1};
    n->name[0] = 'i'; n->name[1] = '0'; n->mtu = 1500;
    n->hwaddr_len = 6; memcpy(n->hwaddr, mac, 6);
    n->flags = NETIF_FLAG_ETHERNET | NETIF_FLAG_ETHARP | NETIF_FLAG_BROADCAST | NETIF_FLAG_MLD6;
    n->linkoutput = output; n->output_ip6 = ethip6_output;
    return ERR_OK;
}
int main(void) {
    struct netif n = {0};
    lwip_init();
    assert(netif_add_noaddr(&n, NULL, setup, ethernet_input));
    netif_create_ip6_linklocal_address(&n, 1); netif_set_up(&n);
    for (unsigned i = 0; i < 4; ++i) nd6_tmr();
    assert(!netif_is_link_up(&n) && !ns && ip6_addr_istentative(netif_ip6_addr_state(&n, 0)));
    netif_set_link_up(&n);
    assert(!ip6_addr_isvalid(netif_ip6_addr_state(&n, 0)));
    nd6_tmr();
    assert(ns && !ip6_addr_isvalid(netif_ip6_addr_state(&n, 0)));
    for (unsigned i = 0; i < 4; ++i) nd6_tmr();
    assert(ip6_addr_isvalid(netif_ip6_addr_state(&n, 0)));
    netif_set_link_down(&n); assert(!netif_is_link_up(&n)); netif_remove(&n);
    puts("actual lwIP IPv6: down link blocks DAD; enabled link sends NS before valid address");
}
''')
    sources = sorted((lwip / 'src/core').glob('*.c'))
    sources += sorted((lwip / 'src/core/ipv4').glob('*.c'))
    sources += sorted((lwip / 'src/core/ipv6').glob('*.c'))
    executable = work / 'dad'
    command = [os.environ.get('CC', 'cc'), '-std=c11', '-g', '-fno-pie', '-no-pie',
               '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
               '-I' + str(work), '-I' + str(here), '-I' + str(lwip / 'src/include'),
               str(work / 'dad.c')] + [str(p) for p in sources] + [
               str(lwip / 'src/netif/ethernet.c'), '-Wl,--gc-sections', '-o', str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)

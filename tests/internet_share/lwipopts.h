#define NO_SYS               1
#define SYS_LIGHTWEIGHT_PROT 0
#define MEM_LIBC_MALLOC      1
#define MEMP_MEM_MALLOC      1
#define LWIP_IPV4            1
#define LWIP_IPV6            0
#define LWIP_ARP             0
#define LWIP_ETHERNET        0
#define LWIP_NETCONN         0
#define LWIP_SOCKET          0
#define LWIP_TCP             1
#define LWIP_UDP             1
#define LWIP_ICMP            1
#define LWIP_RAW             1
#define LWIP_DHCP            0
#define LWIP_DNS             0
#define IP_FORWARD           1
#define IP_NAPT              1
#define IP_NAPT_MAX          128
#define IP_NAPT_PORTMAP      0
#define IP_REASSEMBLY        0
#define IP_FRAG              0
#define LWIP_STATS           0
#define LWIP_NETIF_LOOPBACK  0
#define LWIP_RAND()          ((u32_t)rand())

#define LWIP_HOOK_FILENAME                 "lwip/napt_policy.h"
#define LWIP_HOOK_IP4_ROUTE_SRC(src, dest) napt_policy_route(src, dest)

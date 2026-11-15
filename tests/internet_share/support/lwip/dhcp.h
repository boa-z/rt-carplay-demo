#include "lwip/netif.h"
struct dhcp { u8_t state; };
struct dhcp *test_dhcp_data(struct netif *);
#define netif_dhcp_data(n) test_dhcp_data(n)
err_t dhcp_start(struct netif *);

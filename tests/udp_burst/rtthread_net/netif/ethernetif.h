/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <rtthread.h>
struct pbuf;
struct eth_device
{
    struct rt_device parent;
    struct netif *netif;
    struct pbuf *(*eth_rx)(rt_device_t);
    int (*eth_tx)(rt_device_t, struct pbuf *);
};
int eth_device_init(struct eth_device *, const char *);
int eth_device_linkchange(struct eth_device *, int);

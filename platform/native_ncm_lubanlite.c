/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Application-owned CDC-NCM adapter. Upstream CherryUSB remains unmodified.
 * Bind only the first Apple NCM function, matching iAP2 identification. */
#include "native_ncm.h"
#include <rtthread.h>
#include <rtdevice.h>
#include <netif/ethernetif.h>
#include <lwip/netifapi.h>
#include <lwip/dhcp.h>
#include <lwip/pbuf.h>
#include <stdatomic.h>
#include <string.h>
#include "usbh_core.h"

static USB_NOCACHE_RAM_SECTION struct {
    USB_MEM_ALIGNX uint8_t rx[16384];
    USB_MEM_ALIGNX uint8_t tx[8192];
    USB_MEM_ALIGNX uint8_t control[256];
    USB_MEM_ALIGNX uint8_t setup[USB_ALIGN_UP(8, CONFIG_USB_ALIGN_SIZE)];
    USB_MEM_ALIGNX uint8_t notification[USB_ALIGN_UP(64, CONFIG_USB_ALIGN_SIZE)];
} dma;
static struct {
    struct eth_device eth; /* RT-Thread device is the first member. */
    struct rt_mutex lock;
    bool initialized, resources;
    /* D213 supports word atomics; byte exchange would require libatomic. */
    atomic_uint stopping, rx_done, note_done;
    atomic_int interface, rx_result, note_result;
    struct usbh_hubport *hport;
    struct carplay_ncm_function function;
    struct carplay_ncm_limits limits;
    struct usb_endpoint_descriptor in, out, notification;
    struct usbh_urb rx, tx, note;
    usb_osal_sem_t ready, exited;
    uint8_t mac[6], frame[1514];
    uint16_t sequence;
    atomic_uint received, transmitted, rx_errors, tx_errors, input_errors;
    atomic_uint notifications;
    atomic_uint carrier;
} net = {.interface = ATOMIC_VAR_INIT(-1), .stopping = ATOMIC_VAR_INIT(true)};

#ifdef RTCARPLAY_NCM_TRACE
static struct {
    struct rt_mutex lock;
    struct {
        struct carplay_ncm_packet packet;
        unsigned size, direction;
        int result;
    } records[24];
    unsigned head, count, dropped, samples[2];
    /* Per direction: IPv6, NS, NA, TCP, UDP, other EtherType, malformed. */
    unsigned totals[2][7];
} trace;
static void trace_frame(const uint8_t *data, unsigned size, unsigned direction, int result)
{
    struct carplay_ncm_packet p;
    int rc = carplay_ncm_packet(data, size, &p);
    rt_mutex_take(&trace.lock, RT_WAITING_FOREVER);
    unsigned *counts = trace.totals[direction];
    if (rc < 0) ++counts[6];
    else if (!rc) ++counts[5];
    else {
        ++counts[0];
        if (p.neighbor) ++counts[p.icmp_type == 135 ? 1 : 2];
        if (p.transport && p.protocol == 6) ++counts[3];
        if (p.transport && p.protocol == 17) ++counts[4];
    }
    bool record = rc == 1 && (p.neighbor || (p.transport && p.protocol == 6));
    if (!record && trace.samples[direction] < 2) { ++trace.samples[direction]; record = true; }
    if (record) {
        if (trace.count == 24) ++trace.dropped;
        else {
            unsigned at = (trace.head + trace.count++) % 24;
            trace.records[at].packet = p; trace.records[at].size = size;
            trace.records[at].direction = direction; trace.records[at].result = result;
        }
    }
    rt_mutex_release(&trace.lock);
}
#define IP6_FMT "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x"
#define IP6_ARGS(p) (p)[0],(p)[1],(p)[2],(p)[3],(p)[4],(p)[5],(p)[6],(p)[7],(p)[8],(p)[9],(p)[10],(p)[11],(p)[12],(p)[13],(p)[14],(p)[15]
static void trace_status(void)
{
    for (unsigned d = 0; d < 2; ++d) {
        unsigned counts[7], dropped;
        rt_mutex_take(&trace.lock, RT_WAITING_FOREVER);
        memcpy(counts, trace.totals[d], sizeof(counts)); dropped = trace.dropped;
        rt_mutex_release(&trace.lock);
        rt_kprintf("[ncm-net] %s ipv6=%u NS=%u NA=%u TCP=%u UDP=%u other=%u malformed=%u omitted=%u\n",
            d ? "TX" : "RX", counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], dropped);
    }
    /* Print outside the mutex. Drain at most one bounded queue per request. */
    for (unsigned i = 0; i < 24; ++i) {
        struct carplay_ncm_packet p;
        unsigned size, d; int result;
        rt_mutex_take(&trace.lock, RT_WAITING_FOREVER);
        if (!trace.count) { rt_mutex_release(&trace.lock); break; }
        unsigned at = trace.head;
        p = trace.records[at].packet; size = trace.records[at].size;
        d = trace.records[at].direction; result = trace.records[at].result;
        trace.head = (at + 1) % 24; --trace.count;
        rt_mutex_release(&trace.lock);
        rt_kprintf("[ncm-net] %s bytes=%u rc=%d eth=%04x next=%u hop=%u icmp=%u ports=%u->%u flags=%02x frag=%u\n",
            d ? "TX" : "RX", size, result, p.ether_type, p.protocol, p.hop_limit,
            p.icmp_type, p.source_port, p.destination_port, p.tcp_flags, p.fragmented);
        rt_kprintf("[ncm-net] src=" IP6_FMT " dst=" IP6_FMT "\n", IP6_ARGS(p.source), IP6_ARGS(p.destination));
        rt_kprintf("[ncm-net] mac=%02x:%02x:%02x:%02x:%02x:%02x->%02x:%02x:%02x:%02x:%02x:%02x\n",
            p.source_mac[0],p.source_mac[1],p.source_mac[2],p.source_mac[3],p.source_mac[4],p.source_mac[5],
            p.destination_mac[0],p.destination_mac[1],p.destination_mac[2],p.destination_mac[3],p.destination_mac[4],p.destination_mac[5]);
        if (p.neighbor) rt_kprintf("[ncm-net] target=" IP6_FMT "\n", IP6_ARGS(p.target));
    }
}
#else
#define trace_frame(data, size, direction, result) ((void)0)
#define trace_status() ((void)0)
#endif

int carplay_native_ncm_interface(void) { return atomic_load(&net.interface); }
void carplay_native_ncm_status(void)
{
    rt_kprintf("[carplay-ncm] interface=%d RX=%u TX=%u errors_rx/tx/input=%u/%u/%u\n",
        carplay_native_ncm_interface(), atomic_load(&net.received), atomic_load(&net.transmitted),
        atomic_load(&net.rx_errors), atomic_load(&net.tx_errors), atomic_load(&net.input_errors));
    rt_kprintf("[carplay-ncm] notifications=%u carrier_observed=%u local_ipv6_link=%u\n",
        atomic_load(&net.notifications), atomic_load(&net.carrier), !atomic_load(&net.stopping));
    if (net.initialized) {
        const struct netif *n = net.eth.netif;
        rt_kprintf("[carplay-ncm] n0 index=%u flags=%02x ipv6_state=%02x mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
            netif_get_index(n), n->flags, netif_ip6_addr_state(n, 0),
            n->hwaddr[0],n->hwaddr[1],n->hwaddr[2],n->hwaddr[3],n->hwaddr[4],n->hwaddr[5]);
        trace_status();
    }
}
static rt_err_t device_control(rt_device_t dev, int cmd, void *arg)
{
    (void)dev;
    if (cmd != NIOCTL_GADDR || !arg) return -RT_EINVAL;
    memcpy(arg, net.mac, 6);
    return RT_EOK;
}
static struct pbuf *receive_unused(rt_device_t dev) { (void)dev; return NULL; }
static rt_err_t device_transmit(rt_device_t dev, struct pbuf *p)
{
    (void)dev;
    if (!p || p->tot_len > sizeof(net.frame)) return -RT_EINVAL;
    rt_mutex_take(&net.lock, RT_WAITING_FOREVER);
    int rc = -1;
    if (!atomic_load(&net.stopping) && net.hport &&
        pbuf_copy_partial(p, net.frame, p->tot_len, 0) == p->tot_len) {
        int size = carplay_ncm_encode(&net.limits, net.sequence++, net.frame, p->tot_len,
                                      dma.tx, sizeof(dma.tx));
        if (size > 0) {
            /* Force a short packet when the NTB is packet aligned, without
             * lying about its wBlockLength or reading past DMA storage. */
            if ((unsigned)size % net.out.wMaxPacketSize == 0 &&
                (unsigned)size < sizeof(dma.tx) && (unsigned)size < net.limits.output)
                dma.tx[size++] = 0;
            usbh_bulk_urb_fill(&net.tx, net.hport, &net.out, dma.tx, size, 100, NULL, NULL);
            rc = usbh_submit_urb(&net.tx);
            if (!rc && net.tx.actual_length != (unsigned)size) rc = -1;
        }
    }
    if (rc) atomic_fetch_add(&net.tx_errors, 1);
    else atomic_fetch_add(&net.transmitted, 1);
    if (p && p->tot_len <= sizeof(net.frame)) {
        /* Copy again only in diagnostic builds: failures may precede encode. */
#ifdef RTCARPLAY_NCM_TRACE
        if (pbuf_copy_partial(p, net.frame, p->tot_len, 0) == p->tot_len)
            trace_frame(net.frame, p->tot_len, 1, rc);
#endif
    }
    rt_mutex_release(&net.lock);
    return rc ? -RT_ERROR : RT_EOK;
}
#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops device_ops = {.control = device_control};
#endif
static void address_setup(struct netif *n)
{
#if LWIP_DHCP
    dhcp_stop(n); /* CarPlay link-local service is not a WAN/DHCP client. */
#endif
    memcpy(n->hwaddr, net.mac, 6);
    for (unsigned i = 0; i < LWIP_IPV6_NUM_ADDRESSES; ++i)
        netif_ip6_addr_set_state(n, i, IP6_ADDR_INVALID);
    netif_create_ip6_linklocal_address(n, 1);
    netif_set_ip6_autoconfig_enabled(n, 0);
    /* Local link readiness permits DAD and 0x4301 before the phone opens its
     * NCM pipe. It is not a claim that NETWORK_CONNECTION was received. */
    netif_set_up(n);
    netif_set_link_up(n);
}
static int input_frame(void *arg, const uint8_t *data, size_t length)
{
    (void)arg;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
    if (!p) goto fail;
    if (pbuf_take(p, data, length) != ERR_OK) { pbuf_free(p); goto fail; }
    if (net.eth.netif->input(p, net.eth.netif) != ERR_OK) { pbuf_free(p); goto fail; }
    trace_frame(data, length, 0, 0); /* Queued to lwIP, not proof of IP acceptance. */
    atomic_fetch_add(&net.received, 1);
    return 0;
fail:
    atomic_fetch_add(&net.input_errors, 1);
    return -1;
}
static void rx_completed(void *arg, int result)
{
    (void)arg;
    atomic_store(&net.rx_result, result);
    atomic_store(&net.rx_done, true);
    usb_osal_sem_give(net.ready);
}
static void note_completed(void *arg, int result)
{
    (void)arg;
    atomic_store(&net.note_result, result);
    atomic_store(&net.note_done, true);
    usb_osal_sem_give(net.ready);
}
static bool arm(bool notification)
{
    rt_mutex_take(&net.lock, RT_WAITING_FOREVER);
    int rc = -1;
    if (!atomic_load(&net.stopping)) {
        if (notification) {
            usbh_int_urb_fill(&net.note, net.hport, &net.notification,
                             dma.notification, 64, 0, note_completed, NULL);
            rc = usbh_submit_urb(&net.note);
        } else {
            usbh_bulk_urb_fill(&net.rx, net.hport, &net.in, dma.rx,
                              net.limits.input, 0, rx_completed, NULL);
            rc = usbh_submit_urb(&net.rx);
        }
    }
    rt_mutex_release(&net.lock);
    return rc == 0;
}
static bool cancel(struct usbh_urb *urb)
{
    /* A completed/rejected submit has no HCD owner. kill_urb reports INVAL
     * in that normal case, including completion racing the cancellation. */
    if (!urb->hcpriv) return true;
    int rc = usbh_kill_urb(urb);
    return rc == 0 || urb->hcpriv == NULL;
}
static void rx_worker(void *arg)
{
    (void)arg;
    bool receive = arm(false);
    bool notification = net.function.notification && arm(true);
    while (!atomic_load(&net.stopping) && receive) {
        usb_osal_sem_take(net.ready, 100);
        if (atomic_load(&net.stopping)) break;
        if (atomic_exchange(&net.note_done, false)) {
            int size = atomic_load(&net.note_result);
            if (size >= 8 && dma.notification[0] == 0xa1 &&
                dma.notification[4] == net.function.control && dma.notification[5] == 0) {
                atomic_fetch_add(&net.notifications, 1);
                if (dma.notification[1] == 0 && dma.notification[6] == 0 && dma.notification[7] == 0)
                    atomic_store(&net.carrier, dma.notification[2] != 0);
            }
            /* An unsupported/stalled optional notification never gates RX. */
            notification = size >= 0 && arm(true);
        }
        if (atomic_exchange(&net.rx_done, false)) {
            int size = atomic_load(&net.rx_result);
            if (size < 0) { atomic_fetch_add(&net.rx_errors, 1); break; }
            if (size && carplay_ncm_decode(dma.rx, size, input_frame, NULL) < 0)
                atomic_fetch_add(&net.rx_errors, 1);
            receive = arm(false);
        }
    }
    /* Retain every descriptor/buffer until HCD cancellation has drained it.
     * Failed cancellation quarantines the static instance until reboot. */
    rt_mutex_take(&net.lock, RT_WAITING_FOREVER);
    atomic_store(&net.stopping, true);
    atomic_store(&net.interface, -1);
    bool drained = cancel(&net.rx);
    if (notification) drained = cancel(&net.note) && drained;
    rt_mutex_release(&net.lock);
    netifapi_netif_set_link_down(net.eth.netif);
    if (drained) usb_osal_sem_give(net.exited);
    else rt_kprintf("[carplay-ncm] cancellation failed: retaining USB owner; reboot required\n");
    usb_osal_thread_delete(NULL);
}
/* EP0 setup and buffers are private; CherryUSB serializes the control pipe. */
static int request(uint8_t type, uint8_t code, uint16_t value, uint16_t index, uint16_t size)
{
    struct usb_setup_packet *s = (void *)dma.setup;
    memset(s, 0, sizeof(*s));
    s->bmRequestType = type; s->bRequest = code; s->wValue = value;
    s->wIndex = index; s->wLength = size;
    return usbh_control_transfer(net.hport, s, size ? dma.control : NULL);
}
static void endpoint(struct usb_endpoint_descriptor *d, uint8_t address, uint16_t mps, uint8_t type)
{
    memset(d, 0, sizeof(*d)); d->bLength = 7; d->bDescriptorType = 5;
    d->bEndpointAddress = address; d->wMaxPacketSize = mps; d->bmAttributes = type;
}
static int native_connect(struct usbh_hubport *h, uint8_t slot)
{
    struct carplay_ncm_function f;
    if (h->device_desc.idVendor != 0x05ac ||
        carplay_ncm_find(h->raw_config_desc, h->config.config_desc.wTotalLength, &f) ||
        h->config.intf[slot].altsetting[0].intf_desc.bInterfaceNumber != f.control)
        return 0; /* Second NCM function is intentionally left unbound. */
    if (net.hport) return 0;
    if (!net.resources) {
        if (rt_mutex_init(&net.lock, "cp-ncm", RT_IPC_FLAG_PRIO) != RT_EOK) return 0;
#ifdef RTCARPLAY_NCM_TRACE
        if (rt_mutex_init(&trace.lock, "ncm-trace", RT_IPC_FLAG_PRIO) != RT_EOK) {
            rt_mutex_detach(&net.lock); return 0;
        }
#endif
        net.ready = usb_osal_sem_create(0); net.exited = usb_osal_sem_create(0);
        if (!net.ready || !net.exited) {
            if (net.ready) usb_osal_sem_delete(net.ready);
            if (net.exited) usb_osal_sem_delete(net.exited);
#ifdef RTCARPLAY_NCM_TRACE
            rt_mutex_detach(&trace.lock);
#endif
            rt_mutex_detach(&net.lock); return 0;
        }
        net.resources = true;
    }
    net.hport = h; net.function = f;
    const char *step = "GET_NTB_PARAMETERS";
    int rc = request(0xa1, 0x80, 0, f.control, 28);
    if (rc != 28 || carplay_ncm_parameters(dma.control, rc, &net.limits)) goto fail;
    bool multiple_formats = (dma.control[2] & 2) != 0;
    step = "GET_MAC_STRING";
    rc = request(0x80, 6, 0x300 | f.mac_string, 0x0409, 256);
    if (rc < 0 || carplay_ncm_mac(dma.control, rc, net.mac)) goto fail;
    if (multiple_formats) {
        step = "SET_NTB_FORMAT";
        if ((rc = request(0x21, 0x84, 0, f.control, 0)) < 0) goto fail;
    }
    if (f.capabilities & 0x10) {
        step = "SET_CRC_MODE";
        /* USB CDC-NCM SET_CRC_MODE = 0x8a, value 0 means NCM0. */
        if ((rc = request(0x21, 0x8a, 0, f.control, 0)) < 0) goto fail;
    }
    unsigned input = net.limits.input;
    dma.control[0] = input; dma.control[1] = input >> 8;
    dma.control[2] = 0; dma.control[3] = 0;
    step = "SET_NTB_INPUT_SIZE";
    if ((rc = request(0x21, 0x86, 0, f.control, 4)) < 0) goto fail;
    step = "SET_INTERFACE";
    if ((rc = request(1, 11, f.alternate, f.data, 0)) < 0) goto fail;
    /* Packet-filter control is optional in the NCM functional descriptor. */
    if (f.capabilities & 1) {
        step = "SET_ETHERNET_PACKET_FILTER";
        if ((rc = request(0x21, 0x43, 0x0e, f.control, 0)) < 0) goto fail;
    }
    endpoint(&net.in, f.in, f.in_mps, 2); endpoint(&net.out, f.out, f.out_mps, 2);
    if (f.notification) {
        bool found = false;
        for (unsigned i = 0; i < h->config.intf[slot].altsetting[0].intf_desc.bNumEndpoints; ++i) {
            const struct usb_endpoint_descriptor *d = &h->config.intf[slot].altsetting[0].ep[i].ep_desc;
            if (d->bEndpointAddress == f.notification) { net.notification = *d; found = true; break; }
        }
        if (!found) net.function.notification = 0;
    }
    /* Validate that an ordinary MTU-sized frame fits negotiated constraints. */
    step = "NTB_OUTPUT_LIMITS";
    if (carplay_ncm_encode(&net.limits, 0, net.frame, sizeof(net.frame), dma.tx, sizeof(dma.tx)) < 0)
        goto fail;
    if (!net.initialized) {
        step = "NETWORK_REGISTRATION";
        if (rt_device_find("n0")) goto fail;
#ifdef RT_USING_DEVICE_OPS
        net.eth.parent.ops = &device_ops;
#else
        net.eth.parent.control = device_control;
#endif
        net.eth.eth_rx = receive_unused; net.eth.eth_tx = device_transmit;
        if (eth_device_init(&net.eth, "n0") != RT_EOK) goto fail;
        net.initialized = true;
    }
    usb_osal_sem_reset(net.ready); usb_osal_sem_reset(net.exited);
    atomic_store(&net.rx_done, false); atomic_store(&net.note_done, false);
    memset(&net.rx, 0, sizeof(net.rx)); memset(&net.tx, 0, sizeof(net.tx));
    memset(&net.note, 0, sizeof(net.note)); net.sequence = 0;
    atomic_store(&net.received, 0); atomic_store(&net.transmitted, 0);
    atomic_store(&net.rx_errors, 0); atomic_store(&net.tx_errors, 0);
    atomic_store(&net.input_errors, 0); atomic_store(&net.notifications, 0);
    atomic_store(&net.carrier, false); atomic_store(&net.stopping, false);
#ifdef RTCARPLAY_NCM_TRACE
    rt_mutex_take(&trace.lock, RT_WAITING_FOREVER);
    trace.head = trace.count = trace.dropped = 0;
    memset(trace.samples, 0, sizeof(trace.samples)); memset(trace.totals, 0, sizeof(trace.totals));
    rt_mutex_release(&trace.lock);
#endif
    step = "NETWORK_ADDRESS";
    if (netifapi_netif_common(net.eth.netif, address_setup, NULL) != ERR_OK) goto fail;
    step = "WORKER";
    atomic_store(&net.interface, f.control);
    if (!usb_osal_thread_create("cp-ncm", 4096, 12, rx_worker, NULL)) goto fail;
    h->config.intf[slot].priv = &net;
    rt_kprintf("[carplay-ncm] n0 control=%u data=%u alt=%u IN=%02x OUT=%02x RX=%u\n",
        f.control, f.data, f.alternate, f.in, f.out, input);
    return 0;
fail:
    atomic_store(&net.stopping, true); atomic_store(&net.interface, -1);
    if (net.initialized) netifapi_netif_set_link_down(net.eth.netif);
    rt_kprintf("[carplay-ncm] inactive: %s rc=%d; USBMUX retained\n", step, rc);
    if (h->connected) request(1, 11, 0, f.data, 0);
    net.hport = NULL;
    return 0; /* Optional network failure must not tear down the composite. */
}
static int native_disconnect(struct usbh_hubport *h, uint8_t slot)
{
    if (h->config.intf[slot].priv != &net) return 0;
    rt_mutex_take(&net.lock, RT_WAITING_FOREVER);
    atomic_store(&net.stopping, true); atomic_store(&net.interface, -1);
    rt_mutex_release(&net.lock);
    /* RX wait is bounded; worker cancels/acknowledges before hport release. */
    usb_osal_sem_take(net.exited, USB_OSAL_WAITING_FOREVER);
    rt_mutex_take(&net.lock, RT_WAITING_FOREVER);
    net.hport = NULL;
    rt_mutex_release(&net.lock);
    h->config.intf[slot].priv = NULL;
    return 0;
}
static const struct usbh_class_driver native_driver = {
    .driver_name = "carplay_ncm", .connect = native_connect, .disconnect = native_disconnect
};
CLASS_INFO_DEFINE const struct usbh_class_info carplay_native_ncm_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .bInterfaceClass = 2, .bInterfaceSubClass = 13, .bInterfaceProtocol = 0,
    .id_table = NULL, .class_driver = &native_driver
};

/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE            200809L
#define RTCP_USB_TRACE_IN_TIMEOUTS 1
/* Compile the actual application port, including its DMA submission helpers. */
#include "../../platform/rtcp_usb_lubanlite.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}
static int submit_bulk(struct usbh_hubport *hport,
                       struct usb_endpoint_descriptor *endpoint, void *data,
                       size_t length, uint32_t timeout_ms,
                       struct usb_bulk_state *state)
{
    return dma_transfer(hport, endpoint, NULL, data, length, timeout_ms, state,
                        state ? state->generation : 0U);
}
static uint32_t probe_clock, lock_delay;
uint32_t rt_tick_get_millisecond(void)
{
    return probe_clock;
}
static int allocations, semaphores, locked, fail_alloc, fail_sem;
static usb_osal_mutex_t last_mutex;
static int submit_error, timeout, kill_error, short_by, extra, kills;
static struct usbh_urb *pending;
static struct usb_dma_transfer *allocation;
static struct usb_bulk_state pipe;
static unsigned peer_toggle[32], delivered[32];
static int simulate_pid, disconnect_in_submit;
static unsigned last_pid;
static int diagnostic_completes, diagnostic_length, diagnostic_error, diagnostic_disconnect;
static int kill_completes;
static bool enumeration_test, enum_expanded, enum_initial_expanded;
static int enum_activation_result = 1;
static unsigned enum_activations;
static bool enum_publish, enum_reads_fail;
static const uint8_t enum_ethernet[] = {
    9, 2, 64, 0, 2, 4, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0xfe, 2, 0,
    7, 5, 4, 2, 0, 2, 0, 7, 5, 0x85, 2, 0, 2, 0,
    9, 4, 2, 0, 0, 0xff, 0xfd, 1, 0,
    9, 4, 2, 1, 2, 0xff, 0xfd, 1, 0,
    7, 5, 5, 2, 0, 2, 0, 7, 5, 0x86, 2, 0, 2, 0
};
/* The post-transition fixture is synthetic: it represents a complete standard
 * NCM function plus Apple Ethernet, not a captured/official Apple descriptor. */
static const uint8_t enum_carplay[] = {
    9, 2, 103, 0, 4, 6, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0xfe, 2, 0,
    7, 5, 4, 2, 0, 2, 0, 7, 5, 0x85, 2, 0, 2, 0,
    9, 4, 1, 0, 1, 2, 0x0d, 0, 0,
    7, 5, 0x86, 3, 16, 0, 9,
    9, 4, 2, 0, 0, 0x0a, 0, 0, 0,
    9, 4, 2, 1, 2, 0x0a, 0, 0, 0,
    7, 5, 7, 2, 0, 2, 0, 7, 5, 0x88, 2, 0, 2, 0,
    9, 4, 3, 0, 2, 0xff, 0xfd, 1, 0,
    7, 5, 9, 2, 0, 2, 0, 7, 5, 0x8a, 2, 0, 2, 0
};
static int enum_submit(struct usbh_urb *u)
{
    const struct usb_setup_packet *s = u->setup;
    assert(s && locked);
    const uint8_t *source = NULL;
    size_t length = 0;
    uint8_t response[18] = { 18, 1 };
    if (s->bRequest == APPLE_REQUEST_CARPLAY_CONFIGS)
    {
        assert(s->bmRequestType == 0xc0 && s->wIndex == 4 && s->wLength == 1);
        ++enum_activations;
        if (enum_activation_result < 0)
            return enum_activation_result;
        if (enum_activation_result == 1 && enum_publish)
            enum_expanded = true;
        length = (size_t)enum_activation_result;
        source = response;
    }
    else
    {
        assert(s->bRequest == USB_REQUEST_GET_DESCRIPTOR);
        if ((s->wValue >> 8) == USB_DESCRIPTOR_TYPE_DEVICE)
        {
            if (enum_reads_fail && enum_activations)
                return -USB_ERR_IO;
            response[17] = enum_expanded ? 6 : 4;
            source = response;
            length = sizeof(response);
        }
        else
        {
            assert((s->wValue >> 8) == USB_DESCRIPTOR_TYPE_CONFIGURATION);
            unsigned index = s->wValue & 255;
            static const uint8_t other[] = { 9, 2, 9, 0, 0, 1, 0, 0x80, 50 };
            source = other;
            length = sizeof(other);
            if (index == 3)
            {
                source = enum_ethernet;
                length = sizeof(enum_ethernet);
            }
            if (index == 5 && enum_expanded)
            {
                source = enum_carplay;
                length = sizeof(enum_carplay);
            }
        }
    }
    if (length > u->transfer_buffer_length)
        length = u->transfer_buffer_length;
    if (length)
        memcpy(u->transfer_buffer, source, length);
    u->actual_length = (uint32_t)length + 8;
    u->errorcode = 0;
    u->complete(u->arg, (int)u->actual_length);
    return 0;
}
void usb_osal_msleep(uint32_t ms)
{
    probe_clock += ms;
}
void usbh_ehci_transfer_snapshot(struct usbh_urb *u, int reason)
{
    assert(reason == -USB_ERR_TIMEOUT && u == pending);
    if (diagnostic_completes)
    {
        u->errorcode = diagnostic_error;
        u->actual_length = (unsigned)diagnostic_length;
        if (diagnostic_length)
            memset(u->transfer_buffer, 0xa6, (size_t)diagnostic_length);
        u->data_toggle ^= 1U;
        if (diagnostic_disconnect)
        {
            ++pipe.generation;
            pipe.data_toggle = 0;
        }
        u->complete(u->arg, diagnostic_error ? diagnostic_error : diagnostic_length);
    }
}

static int critical;
usb_osal_mutex_t usb_osal_mutex_create(void)
{
    static int locks[2];
    static unsigned n;
    return &locks[n++ % 2];
}
size_t usb_osal_enter_critical_section(void)
{
    assert(!critical);
    critical = 1;
    return 0;
}
void usb_osal_leave_critical_section(size_t flags)
{
    (void)flags;
    assert(critical);
    critical = 0;
}
void *rt_malloc_align(size_t n, size_t align)
{
    if (fail_alloc)
        return NULL;
    assert(n % align == 0);
    void *p;
    assert(!posix_memalign(&p, align, n));
    allocations++;
    allocation = p;
    return p;
}
void rt_free_align(void *p)
{
    allocations--;
    free(p);
}
usb_osal_sem_t usb_osal_sem_create(uint32_t initial)
{
    assert(!initial);
    if (fail_sem)
        return NULL;
    int *s = calloc(1, sizeof(*s));
    assert(s);
    semaphores++;
    return s;
}
void usb_osal_sem_delete(usb_osal_sem_t s)
{
    semaphores--;
    free(s);
}
int usb_osal_sem_give(usb_osal_sem_t s)
{
    *(int *)s = 1;
    return 0;
}
int usb_osal_sem_take(usb_osal_sem_t s, uint32_t ms)
{
    assert(ms);
    if (!*(int *)s)
        probe_clock += ms;
    return *(int *)s ? 0 : -USB_ERR_TIMEOUT;
}
int usb_osal_mutex_take(usb_osal_mutex_t m)
{
    assert(m && !locked);
    last_mutex = m;
    probe_clock += lock_delay;
    locked = 1;
    return 0;
}
int usb_osal_mutex_give(usb_osal_mutex_t m)
{
    assert(m && locked);
    locked = 0;
    return 0;
}
int usbh_submit_urb(struct usbh_urb *u)
{
    if (enumeration_test)
        return enum_submit(u);
    assert(locked);
    assert((uintptr_t)u->setup % 64 == 0);
    assert((uintptr_t)u->transfer_buffer % 64 == 0);
    assert(u == &allocation->urb && !u->timeout);
    if (submit_error)
        return submit_error;
    pending = u;
    if (timeout)
        return 0;
    if (simulate_pid && !u->setup)
    {
        unsigned index = (u->ep->bEndpointAddress & 15U) +
                         ((u->ep->bEndpointAddress & 0x80U) ? 16U : 0U);
        unsigned mps = u->ep->wMaxPacketSize;
        assert(mps);
        last_pid = u->data_toggle;
        /* One successful short packet, full packets, and a ZLP all advance
         * the receiver's expected DATA PID. Duplicate OUT is ACKed but not
         * delivered to its consumer, exactly why USB success isn't enough. */
        unsigned packets = (u->transfer_buffer_length + mps - 1U) / mps;
        if (!packets)
            packets = 1;
        if (peer_toggle[index] == u->data_toggle)
        {
            ++delivered[index];
            peer_toggle[index] ^= packets & 1U;
        }
        u->data_toggle ^= packets & 1U;
    }
    if (disconnect_in_submit)
    {
        ++pipe.generation;
        pipe.data_toggle = 0;
    }
    int len = (int)u->transfer_buffer_length;
    bool input = u->setup ? (u->setup->bmRequestType & 0x80) != 0
                          : (u->ep->bEndpointAddress & 0x80) != 0;
    if (input && len)
        memset(u->transfer_buffer, 0xa5, (size_t)len);
    if (!input)
        for (int i = 0; i < len; ++i) assert(u->transfer_buffer[i] == 0x37);
    u->actual_length = len - short_by + extra + (u->setup ? 8 : 0);
    u->complete(u->arg, (int)u->actual_length);
    return 0;
}
int usbh_kill_urb(struct usbh_urb *u)
{
    assert(u == pending);
    kills++;
    if (kill_completes)
    {
        u->errorcode = 0;
        u->actual_length = 3;
        memset(u->transfer_buffer, 0xa7, 3);
        u->data_toggle ^= 1U;
        u->complete(u->arg, 3);
        return -USB_ERR_INVAL; /* completion already released hcpriv */
    }
    if (kill_error)
        return kill_error;
    u->complete(u->arg, -USB_ERR_SHUTDOWN);
    return 0;
}
static void clean(void)
{
    assert(!allocations && !semaphores && !locked);
}
static void test_enumeration_activation(void)
{
    assert(sizeof(enum_ethernet) == 64 && sizeof(enum_carplay) == 103);
    struct usbh_bus bus = { 0 };
    struct usbh_hubport port = { 0 };
    port.bus = &bus;
    port.mutex = &port;
    port.connected = true;
    port.device_desc.idVendor = APPLE_USB_VENDOR_ID;
    s_usb.enabled = true;
    enumeration_test = true;
    for (unsigned scenario = 0; scenario < 6; ++scenario)
    {
        enum_initial_expanded = scenario == 1;
        enum_expanded = enum_initial_expanded;
        enum_activations = 0;
        enum_publish = scenario != 4;
        enum_reads_fail = scenario == 5;
        enum_activation_result = scenario == 2 ? -USB_ERR_STALL : scenario == 3 ? 0
                                                                                : 1;
        port.device_desc.bNumConfigurations = enum_expanded ? 6 : 4;
        uint32_t started = probe_clock;
        uint8_t selected = usbh_get_hport_active_config_index(&port);
        printf("enumeration scenario=%u selected_index=%u mode_requests=%u configs=%u\n",
               scenario, selected, enum_activations, port.device_desc.bNumConfigurations);
        fflush(stdout);
#ifdef RTCARPLAY_NATIVE_NCM
        assert(selected == (scenario < 2 ? 5 : 0));
#else
        assert(selected == (scenario < 2 ? 5 : scenario == 5 ? 0
                                                             : 3));
#endif
        assert(enum_activations == (scenario == 1 ? 0 : 1));
        assert(port.device_desc.bNumConfigurations == (scenario < 2 ? 6 : 4));
        if (scenario == 4)
            assert(probe_clock - started >= CARPLAY_CONFIG_ACTIVATE_TIMEOUT_MS);
        clean();
    }
    enumeration_test = false;
    puts("actual enumeration: mode request before tethering fallback, fresh descriptors, existing mode, rejected/short requests, unchanged mode and lost device passed");
}
static unsigned activation_calls;
static bool activation_ready;
static struct usbh_hubport *activation_port;
static unsigned session_link_calls;
static bool session_link_ready;
void usbh_ipheth_session_link(struct usbh_hubport *hport, bool ready)
{
    assert(hport == activation_port);
    ++session_link_calls;
    session_link_ready = ready;
}
void usbh_ipheth_host_ready(struct usbh_hubport *hport, bool ready)
{
    ++activation_calls;
    activation_port = hport;
    activation_ready = ready;
}
int main(void)
{
#if defined(RTCARPLAY_AUTO_CONNECT)
    /* A real class bind before open: phone already plugged in at boot.
     * No callback is installed yet; the later owner reads the retained device. */
    assert(s_usb.enabled);
    static uint8_t raw[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 0xff, 0xfe, 2, 0,
        7, 5, 4, 2, 0, 2, 0, 7, 5, 0x85, 2, 0, 2, 0
    };
    struct usbh_bus bus = { 0 };
    struct usbh_hubport early = { 0 };
    early.bus = &bus;
    early.dev_addr = 1;
    early.speed = USB_SPEED_HIGH;
    early.device_desc.idVendor = APPLE_USB_VENDOR_ID;
    early.device_desc.bNumConfigurations = 1;
    early.raw_config_desc = raw;
    early.config.config_desc.wTotalLength = sizeof(raw);
    assert(carplay_usbmux_connect(&early, 0) == 0);
    assert(s_usb.callback == NULL && s_usb.present);
    assert(lubanlite_open(NULL, NULL, NULL) == RTCP_OK);
    rtcp_usb_device_t boot_device;
    assert(lubanlite_get_device(NULL, &boot_device) == RTCP_OK);
    assert(boot_device.present && boot_device.vendor_id == APPLE_USB_VENDOR_ID);
    assert(boot_device.configuration_value == 1 && boot_device.address == 1);
    assert(carplay_usbmux_disconnect(&early, 0) == 0);
    assert(lubanlite_get_device(NULL, &boot_device) == RTCP_ESTATE);
    puts("actual USB class: boot-time bind before owner subscription retained and retired");
#else
    assert(!s_usb.enabled); /* Manual bring-up retains its opt-in behavior. */
#endif
    struct usbh_hubport h = { 0 };
    h.mutex = &h;
    s_usb.present = true;
    s_usb.hport = &h;
    uint32_t epoch = rtcp_usb_lubanlite_generation();
#ifdef RTCARPLAY_NATIVE_NCM
    assert(rtcp_usb_lubanlite_host_ready(epoch, true));
    assert(!activation_calls && !session_link_calls);
    snapshot_write_begin();
    snapshot_write_end();
    assert(!rtcp_usb_lubanlite_host_ready(epoch, true));
    epoch = rtcp_usb_lubanlite_generation();
    assert(rtcp_usb_lubanlite_host_ready(epoch, false));
    s_usb.present = false;
    assert(!rtcp_usb_lubanlite_host_ready(epoch, true));
    assert(!activation_calls && !session_link_calls);
    puts("native NCM: CarKit cannot activate IPHETH; stale generations rejected");
#else
    assert(rtcp_usb_lubanlite_host_ready(epoch, true));
    assert(activation_calls == 1 && activation_ready && activation_port == &h);
    assert(session_link_calls == 1 && session_link_ready);
    snapshot_write_begin();
    snapshot_write_end();
    assert(!rtcp_usb_lubanlite_host_ready(epoch, true));
    assert(activation_calls == 1);
    assert(session_link_calls == 1);
    epoch = rtcp_usb_lubanlite_generation();
    assert(rtcp_usb_lubanlite_host_ready(epoch, false));
    assert(activation_calls == 2 && !activation_ready);
    assert(session_link_calls == 2 && !session_link_ready);
    s_usb.present = false;
    assert(!rtcp_usb_lubanlite_host_ready(epoch, true));
    assert(activation_calls == 2);
    assert(session_link_calls == 2);
#endif
    s_usb.hport = NULL;
    pipe.mutex = &pipe;
    struct usb_endpoint_descriptor ep = { .bEndpointAddress = 0x85 };
    uint8_t bytes[200];
    uint8_t *unaligned = bytes + 1;
    memset(bytes, 0x37, sizeof(bytes));
    assert(control_transfer_timeout(&h, 0x80, 6, 0, 0, unaligned, 9, 100) == 9);
    assert(bytes[0] == 0x37 && bytes[10] == 0x37 && unaligned[0] == 0xa5);
    clean();
    assert(control_transfer_timeout(&h, 0x40, 4, 0, 0, NULL, 0, 100) == 0);
    clean();
    short_by = 3;
    memset(bytes, 0x37, sizeof(bytes));
    assert(submit_bulk(&h, &ep, unaligned, 65, 100, NULL) == 62);
    assert(unaligned[61] == 0xa5 && unaligned[62] == 0x37);
    clean();
    short_by = 0;
    ep.bEndpointAddress = 4;
    memset(bytes, 0x37, sizeof(bytes));
    assert(submit_bulk(&h, &ep, unaligned, 129, 100, NULL) == 129);
    clean();
    assert(submit_bulk(&h, &ep, NULL, 0, 100, NULL) == 0);
    clean();
    extra = 1;
    assert(control_transfer_timeout(&h, 0x80, 6, 0, 0, unaligned, 9, 100) == RTCP_EIO);
    clean();
    extra = 0;
    short_by = 10;
    assert(control_transfer_timeout(&h, 0x80, 6, 0, 0, unaligned, 9, 100) == RTCP_EIO);
    clean();
    short_by = 0;
    assert(submit_bulk(&h, &ep, NULL, 1, 100, NULL) == RTCP_EINVAL);
    clean();
    assert(submit_bulk(&h, &ep, unaligned, SIZE_MAX, 100, NULL) == RTCP_EINVAL);
    clean();
    fail_alloc = 1;
    assert(submit_bulk(&h, &ep, unaligned, 1, 100, NULL) == RTCP_ENOMEM);
    clean();
    fail_alloc = 0;
    fail_sem = 1;
    assert(submit_bulk(&h, &ep, unaligned, 1, 100, NULL) == RTCP_ENOMEM);
    clean();
    fail_sem = 0;
    submit_error = -USB_ERR_STALL;
    assert(submit_bulk(&h, &ep, unaligned, 1, 100, NULL) == RTCP_EIO);
    clean();
    submit_error = 0;
    timeout = 1;
    assert(submit_bulk(&h, &ep, unaligned, 1, 100, NULL) == RTCP_ETIMEDOUT);
    clean();
    assert(kills == 1);
    kill_error = -USB_ERR_TIMEOUT;
    assert(control_transfer_timeout(&h, 0x80, 6, 0, 0, unaligned, 9, 100) == RTCP_ETIMEDOUT);
    assert(allocations == 1 && semaphores == 1 && !locked);
    /* A late callback and DMA write remain valid after the caller returns. */
    pending->transfer_buffer[0] = 0xff;
    pending->complete(pending->arg, 1);
    usb_osal_sem_delete(allocation->done);
    rt_free_align(allocation);
    clean();
    /* Real owned-URB path: model VERSION, SETUP and SYN as three short OUT
     * transfers. A fresh zeroed URB each time must still use DATA0/1/0. */
    timeout = 0;
    kill_error = 0;
    simulate_pid = 1;
    ep.bEndpointAddress = 4;
    ep.wMaxPacketSize = 512;
    memset(bytes, 0x37, sizeof(bytes));
    const size_t lengths[] = { 20, 17, 36 };
    for (unsigned i = 0; i < 3; ++i)
    {
        assert(submit_bulk(&h, &ep, unaligned, lengths[i], 100, &pipe) == (int)lengths[i]);
        assert(last_pid == (i & 1U));
        assert(delivered[4] == i + 1U);
        clean();
    }
    /* IN and OUT maintain separate state. Repeated IN short replies carry
     * the HCD's returned toggle into the next allocated URB too. */
    struct usb_bulk_state in_pipe = { .mutex = &in_pipe };
    ep.bEndpointAddress = 0x85;
    for (unsigned i = 0; i < 2; ++i)
    {
        assert(submit_bulk(&h, &ep, unaligned, 20, 100, &in_pipe) == 20);
        assert(last_pid == (i & 1U));
        clean();
    }
    assert(pipe.data_toggle == 1 && in_pipe.data_toggle == 0);
    ep.bEndpointAddress = 4;
    memset(bytes, 0x37, sizeof(bytes));
    /* A full packet and its terminating ZLP each consume one DATA PID. */
    static uint8_t full[512];
    memset(full, 0x37, sizeof(full));
    assert(submit_bulk(&h, &ep, full, sizeof(full), 100, &pipe) == 512);
    assert(last_pid == 1 && pipe.data_toggle == 0);
    clean();
    assert(submit_bulk(&h, &ep, NULL, 0, 100, &pipe) == 0);
    assert(last_pid == 0 && pipe.data_toggle == 1);
    clean();
    /* Failed submissions and timeouts without progress must not reset PID. */
    submit_error = -USB_ERR_IO;
    assert(submit_bulk(&h, &ep, unaligned, 1, 100, &pipe) == RTCP_EIO);
    assert(pipe.data_toggle == 1);
    clean();
    submit_error = 0;
    timeout = 1;
    kill_error = 0;
    assert(submit_bulk(&h, &ep, unaligned, 1, 100, &pipe) == RTCP_ETIMEDOUT);
    assert(pipe.data_toggle == 1);
    clean();
    timeout = 0;
    /* Late completion of an old generation cannot overwrite reset state. */
    disconnect_in_submit = 1;
    assert(submit_bulk(&h, &ep, unaligned, 1, 100, &pipe) == RTCP_ESTATE);
    assert(pipe.data_toggle == 0);
    clean();
    disconnect_in_submit = 0;
    s_usb.mux_out_state.data_toggle = s_usb.mux_in_state.data_toggle = 1;
    uint32_t previous = s_usb.mux_out_state.generation;
    reset_bulk_states();
    assert(!s_usb.mux_out_state.data_toggle && !s_usb.mux_in_state.data_toggle);
    assert(s_usb.mux_out_state.generation == previous + 1);
    /* Completion during a diagnostic dump must not force failed cancel
     * quarantine for an already completed URB. */
    simulate_pid = 0;
    timeout = 1;
    diagnostic_completes = 1;
    int previous_kills = kills;
    ep.bEndpointAddress = 0x85;
    pipe.data_toggle = 0;
    /* A positive completion in this race must return data, preserve the
     * next DATA PID and leave a canary intact, rather than discard a TLS
     * fragment and report an idle timeout. */
    diagnostic_length = 7;
    memset(bytes, 0x37, sizeof(bytes));
    assert(submit_bulk(&h, &ep, unaligned, 9, 100, &pipe) == 7);
    assert(unaligned[0] == 0xa6 && unaligned[6] == 0xa6 && unaligned[7] == 0x37);
    assert(pipe.data_toggle == 1 && kills == previous_kills);
    clean();
    diagnostic_length = 0;
    assert(submit_bulk(&h, &ep, unaligned, 1, 100, &pipe) == 0);
    assert(pipe.data_toggle == 0 && kills == previous_kills);
    clean();
    diagnostic_length = 7;
    diagnostic_error = -USB_ERR_IO;
    assert(submit_bulk(&h, &ep, unaligned, 9, 100, &pipe) == RTCP_EIO);
    assert(pipe.data_toggle == 0 && kills == previous_kills);
    clean();
    diagnostic_error = 0;
    diagnostic_disconnect = 1;
    assert(submit_bulk(&h, &ep, unaligned, 9, 100, &pipe) == RTCP_ESTATE);
    assert(pipe.data_toggle == 0 && kills == previous_kills);
    clean();
    diagnostic_disconnect = 0;
    diagnostic_length = 0;
    diagnostic_completes = 0;
    kill_completes = 1;
    memset(bytes, 0x37, sizeof(bytes));
    assert(submit_bulk(&h, &ep, unaligned, 9, 100, &pipe) == 3);
    assert(unaligned[0] == 0xa7 && unaligned[2] == 0xa7 && unaligned[3] == 0x37);
    assert(pipe.data_toggle == 1);
    clean();
    kill_completes = 0;
    timeout = 0;
    /* Public USBMUX IN operation follows usbmuxd's 16 KiB USB_MRU. */
    s_usb.present = true;
    s_usb.hport = &h;
    s_usb.mux_in = 0x85;
    if (!s_usb.mux_in_state.mutex)
        s_usb.mux_in_state.mutex = usb_osal_mutex_create();
    s_usb.mux_in_desc = ep;
    s_usb.mux_in_desc.bEndpointAddress = 0x85;
    static uint8_t large[32768];
    memset(large, 0x37, sizeof(large));
    simulate_pid = 0;
    assert(lubanlite_bulk_transfer(NULL, 0x85, large, sizeof(large), 100) == 16384);
    assert(large[16383] == 0xa5 && large[16384] == 0x37);
    clean();
    assert(last_mutex == s_usb.mux_in_state.mutex && last_mutex != h.mutex);
    uint32_t old_generation = s_usb.mux_in_state.generation++;
    assert(dma_transfer(&h, &s_usb.mux_in_desc, NULL, large, 20, 100,
                        &s_usb.mux_in_state, old_generation) == RTCP_ESTATE);
    clean(); /* Reused port address cannot revive an old queued request. */
    /* Distinguish waiting for the shared mutex from holding it across idle
     * Bulk IN. Counters must not change delivery, cancellation or ownership. */
    memset(&s_usb_probe, 0, sizeof(s_usb_probe));
    lock_delay = 37;
    timeout = 1;
    assert(submit_bulk(&h, &ep, unaligned, 1, 200, NULL) == RTCP_ETIMEDOUT);
    assert(s_usb_probe.in_wait.max == 37 && s_usb_probe.in_hold.max == 201);
    assert(!s_usb_probe.errors); /* idle IN timeout is expected */
    lock_delay = 0;
    timeout = 0;
    ep.bEndpointAddress = 4;
    short_by = 1;
    memset(bytes, 0x37, sizeof(bytes));
    assert(submit_bulk(&h, &ep, unaligned, 9, 200, NULL) == 8);
    assert(s_usb_probe.shorts == 1 && s_usb_probe.errors == 1);
    assert(s_usb_probe.last_ep == 4 && s_usb_probe.last_actual == 8);
    clean();
    test_enumeration_activation();
    puts("Luban-Lite DMA alignment, copy bounds and cancellation tests passed");
}

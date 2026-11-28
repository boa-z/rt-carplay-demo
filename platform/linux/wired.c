#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <dirent.h>
#include <libusb.h>
#include <linux/if_addr.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "airplay_info_plist.h"
#include "lockdown_session.h"
#include "mbedtls/platform_util.h"
#include "rtcp/usb.h"
#include "rtcp_linux.h"
#include "usb_pair_cache.h"
#include "usbmux_host.h"

typedef struct
{
    uint32_t elapsed_ms;
    unsigned calls;
} random_timing_t;

typedef struct
{
    libusb_device_handle *handle;
    rtcp_usbmux_host_t mux;
    rtcp_lockdown_session_t lockdown;
    rtcp_pair_record_t record;
    rtcp_stream_t service;
    int mux_interface;
    uint8_t input;
    uint8_t output;
    uint16_t packet_size;
    bool link_failed;
    char path[64];
    char address[64];
    random_timing_t random_timing;
} wired_phone_t;

static pthread_t worker;
static bool worker_started;
static atomic_bool worker_stop;
static atomic_bool reconnect_requested;
static pthread_mutex_t endpoint_lock = PTHREAD_MUTEX_INITIALIZER;
static demo_endpoint_t endpoint;
static uint64_t endpoint_generation;
static const demo_options_t *options;
static const demo_identity_t *identity;
static pthread_t key_worker;
static bool key_worker_started;
static rtcp_pair_keys_t *prepared_keys;

static uint32_t now_ms(void)
{
    return (uint32_t)rtcp_os_linux.now_ms();
}

static uint32_t stream_clock(void *context)
{
    (void)context;
    return now_ms();
}

static int bulk_send(void *context, const uint8_t *bytes, size_t length)
{
    wired_phone_t *phone = context;
    int transferred;
    if (atomic_load(&worker_stop) ||
        libusb_bulk_transfer(phone->handle, phone->output, (uint8_t *)bytes,
                             (int)length, &transferred, 1000) != 0 ||
        (size_t)transferred != length)
        return RTCP_EIO;
    if (length && length % phone->packet_size == 0 &&
        libusb_bulk_transfer(phone->handle, phone->output, (uint8_t *)bytes,
                             0, &transferred, 1000) != 0)
        return RTCP_EIO;
    return RTCP_OK;
}

static int bulk_receive(void *context, uint8_t *bytes, size_t capacity, uint32_t timeout)
{
    wired_phone_t *phone = context;
    int transferred = 0;
    if (atomic_load(&worker_stop))
        return RTCP_EIO;
    int result = libusb_bulk_transfer(phone->handle, phone->input, bytes, (int)capacity,
                                      &transferred, timeout ? timeout : 1);
    if (result == 0 || (result == LIBUSB_ERROR_TIMEOUT && transferred > 0))
        return transferred;
    return result == LIBUSB_ERROR_TIMEOUT ? RTCP_ETIMEDOUT : RTCP_EIO;
}

typedef struct
{
    wired_phone_t *phone;
    rtcp_usbmux_connection_t *connection;
} mux_stream_t;

static int stream_read(void *context, void *bytes, size_t length, uint32_t timeout)
{
    mux_stream_t *stream = context;
    return rtcp_usbmux_connection_read_wait(&stream->phone->mux, stream->connection, bytes,
                                            length, timeout, stream_clock, NULL);
}

static int stream_write(void *context, const void *bytes, size_t length)
{
    mux_stream_t *stream = context;
    return rtcp_usbmux_connection_send(&stream->phone->mux, stream->connection, bytes, length);
}

static int stream_open(void *context, uint16_t port, rtcp_stream_t *output)
{
    static const rtcp_stream_ops_t operations = { .read = stream_read, .write = stream_write };
    wired_phone_t *phone = context;
    if (atomic_load(&worker_stop))
        return RTCP_EIO;
    mux_stream_t *stream = calloc(1, sizeof(*stream));
    if (!stream)
        return RTCP_ENOMEM;
    stream->phone = phone;
    int result = rtcp_usbmux_connect(&phone->mux, port, now_ms(), 3000, &stream->connection);
    if (result == RTCP_OK)
        *output = (rtcp_stream_t){ .ops = &operations, .context = stream };
    else
        free(stream);
    return result;
}

static void stream_close(void *context, const rtcp_stream_t *stream)
{
    wired_phone_t *phone = context;
    mux_stream_t *slot = stream->context;
    rtcp_usbmux_connection_close(&phone->mux, slot->connection);
    free(slot);
}

static int random_bytes(void *context, uint8_t *bytes, size_t length)
{
    random_timing_t *timing = context;
    uint32_t start = now_ms();
    bool ready = !atomic_load(&worker_stop) && rtcp_os_linux.random(bytes, length);
    if (timing)
    {
        ++timing->calls;
        timing->elapsed_ms += now_ms() - start;
    }
    return ready ? 0 : -1;
}

static void pair_timing(void *context, const char *stage, uint32_t elapsed_ms, int result)
{
    const char *origin = context ? "usb" : "background";
    fprintf(stderr, "carplay-pair: %s %s elapsed_ms=%u result=%d\n", origin, stage, elapsed_ms, result);
}

static void *generate_keys(void *context)
{
    (void)context;
    (void)nice(10);
    const rtcp_pair_profile_t profile = { now_ms, pair_timing, NULL };
    rtcp_pair_keys_generate(&prepared_keys, random_bytes, NULL, &profile);
    return NULL;
}

static rtcp_pair_keys_t *take_keys(void *context)
{
    if (key_worker_started)
    {
        uint32_t start = now_ms();
        pthread_join(key_worker, NULL);
        key_worker_started = false;
        pair_timing(context, "prepared-rsa-wait", now_ms() - start, prepared_keys ? RTCP_OK : RTCP_ENOENT);
    }
    rtcp_pair_keys_t *keys = prepared_keys;
    prepared_keys = NULL;
    return keys;
}

static bool first_pairing(void)
{
    if (getenv("CARPLAY_SYNC_PAIR_KEYS"))
        return false;
    DIR *directory = opendir(options->directory);
    if (!directory)
        return false;
    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(directory)))
    {
        const char *name = entry->d_name;
        size_t length = strlen(name);
        if (length > 9 && !strncmp(name, "usb-", 4) && !strcmp(name + length - 5, ".pair"))
        {
            found = true;
            break;
        }
    }
    closedir(directory);
    return !found;
}

static void sleep_retry(void *context, uint32_t ms)
{
    (void)context;
    for (uint32_t elapsed = 0; elapsed < ms && !atomic_load(&worker_stop); elapsed += 100)
        rtcp_os_linux.sleep_ms(ms - elapsed > 100 ? 100 : ms - elapsed);
}

static bool pair_path(wired_phone_t *phone, uint8_t serial_index, char output[256])
{
    uint8_t serial[96];
    if (serial_index == 0)
        return false;
    int length = libusb_get_string_descriptor_ascii(phone->handle, serial_index, serial, sizeof(serial));
    if (length <= 0 || (size_t)length >= sizeof(serial))
        return false;
    for (int index = 0; index < length; ++index)
        if (!strchr("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-", serial[index]) ||
            serial[index] == 0)
            return false;
    int count = snprintf(output, 256, "%s/usb-%.*s.pair", options->directory, length, (char *)serial);
    return count > 0 && count < 256;
}

static int pair(wired_phone_t *phone, uint8_t serial_index)
{
    rtcp_usbmux_transport_t transport = { bulk_send, bulk_receive, phone };
    rtcp_usbmux_init(&phone->mux, &transport);
    uint32_t start = now_ms();
    int result = rtcp_usbmux_start(&phone->mux, start);
    pair_timing(phone, "usbmux-start", now_ms() - start, result);
    if (result != RTCP_OK)
        return result;
    char path[256];
    start = now_ms();
    bool persist = pair_path(phone, serial_index, path);
    pair_timing(phone, "serial", now_ms() - start, persist ? RTCP_OK : RTCP_ENOENT);
    start = now_ms();
    int cached = persist ? carplay_usb_pair_cache_load(path, &phone->record) : RTCP_ENOENT;
    pair_timing(phone, "cache-load", now_ms() - start, cached);
    if (cached == RTCP_EINVAL)
        fprintf(stderr, "carplay: invalid USB trust cache, requesting fresh trust\n");
    else if (cached != RTCP_OK && cached != RTCP_ENOENT)
        return cached;
    rtcp_lockdown_session_config_t config = {
        .open_connection = stream_open,
        .close_connection = stream_close,
        .connection_user = phone,
        .sleep_ms = sleep_retry,
        .rng = random_bytes,
        .rng_context = &phone->random_timing,
        .label = "rt-carplay",
        .record = &phone->record,
        .resume_pairing = cached == RTCP_OK,
        .step_timeout_ms = 5000,
        .pair_retry_ms = 1000,
        .pair_attempts = 30,
        .now_ms = now_ms,
        .report_timing = pair_timing,
        .take_keys = take_keys,
    };
    start = now_ms();
    result = rtcp_lockdown_session_open(&phone->lockdown, &config);
    const char *error = phone->lockdown.remote_error;
    if (result != RTCP_OK && config.resume_pairing &&
        phone->lockdown.stage == RTCP_LOCKDOWN_STAGE_START_SESSION &&
        (!strcmp(error, "InvalidHostID") || !strcmp(error, "InvalidPairRecord") ||
         !strcmp(error, "MissingPairRecord")))
    {
        mbedtls_platform_zeroize(&phone->record, sizeof(phone->record));
        config.resume_pairing = false;
        result = rtcp_lockdown_session_open(&phone->lockdown, &config);
    }
    pair_timing(phone, config.resume_pairing ? "resume" : "first-pair", now_ms() - start, result);
    fprintf(stderr, "carplay-pair: usb entropy elapsed_ms=%u calls=%u\n",
            phone->random_timing.elapsed_ms, phone->random_timing.calls);
    if (phone->lockdown.paired && persist && !config.resume_pairing)
    {
        start = now_ms();
        int saved = carplay_usb_pair_cache_save(path, &phone->record);
        pair_timing(phone, "cache-save", now_ms() - start, saved);
        if (saved != RTCP_OK)
            fprintf(stderr, "carplay: USB trust active but could not be persisted\n");
    }
    if (result != RTCP_OK)
        fprintf(stderr, "carplay: Lockdown %s/%s failed (%d, %s)\n",
                rtcp_lockdown_stage_name(phone->lockdown.stage), phone->lockdown.operation ? phone->lockdown.operation : "unknown", result, *error ? error : "no device error");
    return result;
}

static bool device_path(libusb_device *device, uint8_t configuration, int interface, char output[64])
{
    uint8_t ports[8];
    int depth = libusb_get_port_numbers(device, ports, sizeof(ports));
    if (depth <= 0)
        return false;
    int length = snprintf(output, 64, "%u-%u", libusb_get_bus_number(device), ports[0]);
    for (int index = 1; index < depth; ++index)
        length += snprintf(output + length, 64 - (size_t)length, ".%u", ports[index]);
    length += snprintf(output + length, 64 - (size_t)length, ":%u.%d", configuration, interface);
    return length > 0 && length < 64;
}

static bool configuration(libusb_device_handle *handle, rtcp_usb_device_t *choice)
{
    uint8_t device[18];
    int size = libusb_get_descriptor(handle, LIBUSB_DT_DEVICE, 0, device, sizeof(device));
    if (size != sizeof(device))
        return false;
    rtcp_usb_configuration_kind_t best = RTCP_USB_CONFIG_OTHER;
    for (uint8_t index = 0; index < device[17] && index < 16; ++index)
    {
        uint8_t bytes[2048];
        size = libusb_get_descriptor(handle, LIBUSB_DT_CONFIG, index, bytes, sizeof(bytes));
        rtcp_usb_device_t candidate = { .configuration_index = index };
        if (size < LIBUSB_DT_CONFIG_SIZE ||
            rtcp_usb_parse_configuration(&candidate, bytes, (size_t)size) != RTCP_OK)
            continue;
        rtcp_usb_configuration_kind_t kind = rtcp_usb_configuration_kind(&candidate);
        if (kind > best)
        {
            best = kind;
            *choice = candidate;
        }
    }
    return best != RTCP_USB_CONFIG_OTHER;
}

static bool configure(wired_phone_t *phone, libusb_device *device, int *network_interface)
{
    rtcp_usb_device_t choice = { 0 };
    if (!configuration(phone->handle, &choice))
    {
        uint8_t response;
        if (libusb_control_transfer(phone->handle, 0xc0, 0x52, 0, 4, &response, 1, 1000) < 0)
            return false;
        for (unsigned attempt = 0; attempt < 10 && !choice.configuration_value; ++attempt)
        {
            sleep_retry(NULL, 100);
            if (atomic_load(&worker_stop))
                return false;
            configuration(phone->handle, &choice);
        }
    }
    int mux = rtcp_usb_find_interface(&choice, RTCP_USB_CLASS_VENDOR,
                                      RTCP_USB_APPLE_SUBCLASS_USBMUX, RTCP_USB_APPLE_PROTOCOL_USBMUX);
    *network_interface = rtcp_usb_carplay_interface_number(&choice);
    if (mux < 0 || *network_interface < 0 ||
        !device_path(device, choice.configuration_value, *network_interface, phone->path))
        return false;
    const rtcp_usb_interface_t *interface = &choice.interfaces[mux];
    if (!rtcp_usb_interface_bulk_endpoints(interface, &phone->output, &phone->input) ||
        !phone->output || !phone->input)
        return false;
    int active;
    if (libusb_get_configuration(phone->handle, &active) != 0)
        return false;
    if (active != choice.configuration_value)
    {
        int result = libusb_set_configuration(phone->handle, choice.configuration_value);
        if (result != 0)
        {
            fprintf(stderr, "carplay: USB configuration %u failed: %s\n",
                    choice.configuration_value, libusb_error_name(result));
            return false;
        }
    }
    if (libusb_claim_interface(phone->handle, interface->number) != 0)
        return false;
    phone->mux_interface = interface->number;
    const rtcp_usb_altsetting_t *selected = NULL;
    for (uint8_t alt = 0; alt < interface->altsetting_count; ++alt)
    {
        const rtcp_usb_altsetting_t *setting = &interface->altsettings[alt];
        uint16_t packet_size = 0;
        bool has_input = false;
        for (uint8_t index = 0; index < setting->endpoint_count; ++index)
        {
            if (setting->endpoints[index].address == phone->output)
                packet_size = setting->endpoints[index].max_packet_size;
            if (setting->endpoints[index].address == phone->input)
                has_input = true;
        }
        if (has_input && packet_size && (!selected || setting->alternate_setting > selected->alternate_setting))
        {
            selected = setting;
            phone->packet_size = packet_size;
        }
    }
    return selected && (selected->alternate_setting == 0 ||
                        libusb_set_interface_alt_setting(phone->handle, interface->number,
                                                         selected->alternate_setting) == 0);
}

static void find_network(wired_phone_t *phone)
{
    char path[128], interface[IF_NAMESIZE] = { 0 };
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/net", phone->path);
    DIR *directory = opendir(path);
    if (directory)
    {
        struct dirent *entry;
        while ((entry = readdir(directory)))
        {
            if (entry->d_name[0] != '.' && strlen(entry->d_name) < sizeof(interface))
            {
                memcpy(interface, entry->d_name, strlen(entry->d_name) + 1);
                break;
            }
        }
        closedir(directory);
    }
    char address[64] = { 0 };
    if (interface[0])
    {
        int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd >= 0)
        {
            struct ifreq request = { 0 };
            memcpy(request.ifr_name, interface, sizeof(interface));
            if (ioctl(fd, SIOCGIFFLAGS, &request) == 0 && !(request.ifr_flags & IFF_UP))
            {
                request.ifr_flags |= IFF_UP;
                ioctl(fd, SIOCSIFFLAGS, &request);
            }
            close(fd);
        }
        FILE *addresses = fopen("/proc/net/if_inet6", "r");
        if (addresses)
        {
            char raw[33], name[IF_NAMESIZE];
            unsigned index, prefix, scope, flags;
            while (fscanf(addresses, "%32s %x %x %x %x %15s", raw, &index, &prefix, &scope, &flags, name) == 6)
            {
                if (scope == 0x20 && !(flags & (IFA_F_TENTATIVE | IFA_F_DADFAILED)) &&
                    !strcmp(name, interface))
                {
                    snprintf(address, sizeof(address), "%.4s:%.4s:%.4s:%.4s:%.4s:%.4s:%.4s:%.4s",
                             raw, raw + 4, raw + 8, raw + 12, raw + 16, raw + 20, raw + 24, raw + 28);
                    break;
                }
            }
            fclose(addresses);
        }
    }
    pthread_mutex_lock(&endpoint_lock);
    if (strcmp(endpoint.interface, interface) || strcmp(endpoint.address, address))
    {
        endpoint = (demo_endpoint_t){ 0 };
        endpoint.generation = ++endpoint_generation;
        memcpy(endpoint.interface, interface, sizeof(interface));
        memcpy(endpoint.address, address, sizeof(address));
    }
    pthread_mutex_unlock(&endpoint_lock);
}

static bool get_airplay(iap2_session_airplay_t *out, void *context)
{
    wired_phone_t *phone = context;
    demo_endpoint_t snapshot;
    demo_wired_snapshot(&snapshot);
    if (!snapshot.port || !snapshot.address[0])
        return false;
    memcpy(phone->address, snapshot.address, sizeof(phone->address));
    *out = (iap2_session_airplay_t){
        .airplay_port = snapshot.port,
        .ip_address = phone->address,
        .device_identifier = identity->device_id,
        .public_key = identity->public_key,
        .source_version = AIRPLAY_INFO_DEFAULT_SOURCE_VERSION,
        .mdns_ready = true,
        .event_connected = snapshot.event,
        .transport_connected = snapshot.transport,
    };
    return true;
}

static bool iap2_send(const uint8_t *bytes, size_t length, void *context)
{
    wired_phone_t *phone = context;
    return phone->service.ops->write(phone->service.context, bytes, length) == RTCP_OK;
}

static void iap2_event(iap2_session_event_t event, void *context)
{
    wired_phone_t *phone = context;
    if (event == IAP2_SESSION_EVENT_LINK_DEAD || event == IAP2_SESSION_EVENT_MFI_FAILED)
        phone->link_failed = true;
    fprintf(stderr, "carplay: wired iAP2 event %d\n", event);
}

static void serve(wired_phone_t *phone, int network_interface)
{
    phone->service = rtcp_lockdown_session_service(&phone->lockdown);
    iap2_session_config_t config = {
        .identity = { .name = "Luban CarPlay", .model_identifier = "D213", .manufacturer = "ArtInChip", .serial_number = identity->device_id, .firmware_version = "0.1", .hardware_version = "DEMO128", .language = "en", .provide_location = demo_gnss_available() },
        .mfi_provider = options->mfi_provider,
        .tx = iap2_send,
        .get_airplay = get_airplay,
        .on_event = iap2_event,
        .context = phone,
        .transport = IAP2_SESSION_TRANSPORT_WIRED_USB,
        .car_play_interface_number = (uint8_t)network_interface,
        .wired_reinvite_delay_ms = 5000,
        .on_now_playing_details = demo_track_update,
        .on_phone_status = demo_phone_update,
        .on_route_details = demo_route_update,
    };
    if (!phone->service.ops)
        return;
    iap2_session_t *session = iap2_session_create(&config);
    if (!session)
        return;
    iap2_session_link_up(session, rtcp_os_linux.now_ms());
    demo_gnss_stream_t gnss = { 0 };
    uint64_t next_scan = 0;
    while (!atomic_load(&worker_stop) && !atomic_exchange(&reconnect_requested, false) && !phone->link_failed)
    {
        uint8_t buffer[1024];
        int received = phone->service.ops->read(phone->service.context, buffer, sizeof(buffer), 100);
        if (received <= 0 && received != RTCP_ETIMEDOUT)
            break;
        uint64_t now = rtcp_os_linux.now_ms();
        if (now >= next_scan)
        {
            find_network(phone);
            next_scan = now + 500;
        }
        if (received > 0)
            iap2_session_feed(session, buffer, (size_t)received, now);
        iap2_session_advance(session, now);
        demo_gnss_tick(&gnss, session, now);
    }
    iap2_session_link_down(session);
    iap2_session_destroy(session);
}

static int usb_changed(libusb_context *usb, libusb_device *device, libusb_hotplug_event event, void *context)
{
    (void)usb;
    (void)device;
    (void)event;
    *(int *)context = 1;
    return 0;
}

static void *work(void *context)
{
    libusb_context *usb = context;
    wired_phone_t *phone = calloc(1, sizeof(*phone));
    if (!phone)
    {
        libusb_exit(usb);
        return NULL;
    }
    int changed = 0;
    libusb_hotplug_callback_handle callback;
    bool watching = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) &&
                    libusb_hotplug_register_callback(usb, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
                                                     0, 0x05ac, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
                                                     usb_changed, &changed, &callback) == LIBUSB_SUCCESS;
    demo_boot_mark("usb-watch");
    key_worker_started = first_pairing() && pthread_create(&key_worker, NULL, generate_keys, NULL) == 0;
    while (!atomic_load(&worker_stop))
    {
        changed = 0;
        libusb_device **devices;
        ssize_t count = libusb_get_device_list(usb, &devices);
        if (count < 0)
        {
            sleep_retry(NULL, 1000);
            continue;
        }
        bool phone_found = false;
        for (ssize_t index = 0; index < count && !atomic_load(&worker_stop); ++index)
        {
            struct libusb_device_descriptor descriptor;
            libusb_device *device = devices[index];
            if (libusb_get_device_descriptor(device, &descriptor) != 0 || descriptor.idVendor != 0x05ac ||
                descriptor.idProduct < 0x1200 || descriptor.idProduct >= 0x1300)
                continue;
            phone_found = true;
            memset(phone, 0, sizeof(*phone));
            phone->mux_interface = -1;
            if (libusb_open(device, &phone->handle) != 0)
                continue;
            demo_boot_mark("usb-phone");
            int interface;
            if (configure(phone, device, &interface) && pair(phone, descriptor.iSerialNumber) == RTCP_OK)
                serve(phone, interface);
            pthread_mutex_lock(&endpoint_lock);
            endpoint = (demo_endpoint_t){ 0 };
            atomic_store(&reconnect_requested, false);
            pthread_mutex_unlock(&endpoint_lock);
            rtcp_lockdown_session_close(&phone->lockdown);
            mbedtls_platform_zeroize(&phone->record, sizeof(phone->record));
            if (phone->mux_interface >= 0)
                libusb_release_interface(phone->handle, phone->mux_interface);
            libusb_close(phone->handle);
        }
        libusb_free_device_list(devices, 1);
        uint32_t retry_at = now_ms();
        if (watching)
        {
            while (!changed && !atomic_load(&worker_stop) && (!phone_found || now_ms() - retry_at < 1000))
            {
                struct timeval timeout = { .tv_usec = 250000 };
                if (libusb_handle_events_timeout_completed(usb, &timeout, &changed) < 0)
                {
                    libusb_hotplug_deregister_callback(usb, callback);
                    watching = false;
                    break;
                }
            }
        }
        if (!watching)
            sleep_retry(NULL, 1000);
    }
    if (watching)
        libusb_hotplug_deregister_callback(usb, callback);
    rtcp_pair_keys_free(take_keys(NULL));
    free(phone);
    libusb_exit(usb);
    return NULL;
}

int demo_wired_start(const demo_options_t *settings, const demo_identity_t *accessory)
{
    libusb_context *usb;
    if (libusb_init(&usb) != 0)
        return RTCP_EIO;
    options = settings;
    identity = accessory;
    atomic_store(&worker_stop, false);
    if (pthread_create(&worker, NULL, work, usb) != 0)
    {
        libusb_exit(usb);
        return RTCP_EIO;
    }
    worker_started = true;
    return RTCP_OK;
}

void demo_wired_stop(void)
{
    atomic_store(&worker_stop, true);
    if (worker_started)
        pthread_join(worker, NULL);
    worker_started = false;
}

void demo_wired_snapshot(demo_endpoint_t *output)
{
    pthread_mutex_lock(&endpoint_lock);
    *output = endpoint;
    pthread_mutex_unlock(&endpoint_lock);
}

void demo_wired_publish(uint64_t generation, uint16_t port, const rtcp_receiver_state_t *state)
{
    pthread_mutex_lock(&endpoint_lock);
    if (generation && endpoint.generation == generation)
    {
        endpoint.port = port;
        endpoint.event = state->input_ready;
        endpoint.transport = state->transport_connected;
    }
    pthread_mutex_unlock(&endpoint_lock);
}

void demo_wired_reconnect(uint64_t generation)
{
    pthread_mutex_lock(&endpoint_lock);
    if (generation && endpoint.generation == generation)
    {
        endpoint = (demo_endpoint_t){ 0 };
        atomic_store(&reconnect_requested, true);
    }
    pthread_mutex_unlock(&endpoint_lock);
}

#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "airplay_info_plist.h"
#include "rtcp_linux.h"

static const char profile_path[] = "/org/luban/CarPlay/Profile";
static const char agent_path[] = "/org/luban/CarPlay/Agent";
static const char uuid[] = "00000000-deca-fade-deca-deafdecacaff";
enum
{
    RFCOMM_CHANNEL = 8
};
static const char service_record[] =
    "<record>"
    "<attribute id='0x0001'><sequence><uuid value='%s'/></sequence></attribute>"
    "<attribute id='0x0004'><sequence><sequence><uuid value='0x0100'/></sequence>"
    "<sequence><uuid value='0x0003'/><uint8 value='%u'/></sequence></sequence></attribute>"
    "<attribute id='0x0005'><sequence><uuid value='0x1002'/></sequence></attribute>"
    "<attribute id='0x0009'><sequence><sequence><uuid value='0x1101'/>"
    "<uint16 value='0x0100'/></sequence></sequence></attribute>"
    "<attribute id='0x0100'><text value='Luban CarPlay'/></attribute>"
    "</record>";
static const char introspection[] =
    "<node>"
    "<interface name='org.bluez.Profile1'>"
    "<method name='Release'/>"
    "<method name='NewConnection'><arg type='o' direction='in'/>"
    "<arg type='h' direction='in'/><arg type='a{sv}' direction='in'/></method>"
    "<method name='RequestDisconnection'><arg type='o' direction='in'/></method>"
    "</interface>"
    "<interface name='org.bluez.Agent1'>"
    "<method name='Release'/><method name='Cancel'/>"
    "<method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>"
    "<method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
    "</interface>"
    "</node>";

typedef enum
{
    RADIO_IDLE,
    RADIO_POWER,
    RADIO_AGENT,
    RADIO_DEFAULT_AGENT,
    RADIO_PROFILE,
    RADIO_READY,
    RADIO_PAIRABLE_TIMEOUT,
    RADIO_DISCOVERABLE_TIMEOUT,
    RADIO_PAIRABLE,
    RADIO_DISCOVERABLE,
} radio_step_t;

static struct
{
    GDBusConnection *bus;
    GDBusNodeInfo *node;
    GDBusMethodInvocation *confirmation;
    guint profile, agent, owner_watch;
    bool profile_registered, agent_registered;
    bool owner_changed, profile_released, startup_discovery_opened, call_pending;
    unsigned bus_generation;
    radio_step_t step;
    char *bluez_owner;
    int fd, passkey;
    uint64_t deadline, retry;
    iap2_session_t *link, *tunnel;
    demo_gnss_stream_t gnss;
    rtcp_receiver_t *receiver;
    const demo_options_t *options;
    const demo_identity_t *identity;
    char adapter_path[64];
    char peer_path[128];
    char address[64], ssid[33], password[64];
    uint8_t channel;
    uint8_t pending[16384];
    size_t pending_size;
    bool link_failed, tunnel_failed, handoff, event_connected;
} radio = { .fd = -1, .passkey = -1 };

static void bluetooth_stop(void);
static void bluetooth_advance(void);

void demo_wireless_confirm(bool accept)
{
    if (radio.confirmation)
    {
        if (accept)
            g_dbus_method_invocation_return_value(radio.confirmation, NULL);
        else
            g_dbus_method_invocation_return_dbus_error(radio.confirmation, "org.bluez.Error.Rejected", "Pairing declined");
        g_clear_object(&radio.confirmation);
    }
    radio.passkey = -1;
}

static bool adapter_peer(const char *device)
{
    size_t length = strlen(radio.adapter_path);
    return !strncmp(device, radio.adapter_path, length) && !strncmp(device + length, "/dev_", 5);
}

static void method(GDBusConnection *connection, const gchar *sender, const gchar *object,
                   const gchar *interface, const gchar *operation, GVariant *arguments,
                   GDBusMethodInvocation *invocation, gpointer context)
{
    (void)connection;
    (void)object;
    (void)context;
    if (g_strcmp0(sender, radio.bluez_owner) != 0)
    {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "Unknown Bluetooth daemon");
        return;
    }
    if (!strcmp(interface, "org.bluez.Profile1"))
    {
        if (!strcmp(operation, "NewConnection"))
        {
            const char *device;
            gint32 handle;
            GVariant *properties;
            g_variant_get(arguments, "(&oh@a{sv})", &device, &handle, &properties);
            g_variant_unref(properties);
            GUnixFDList *list = g_dbus_message_get_unix_fd_list(g_dbus_method_invocation_get_message(invocation));
            int fd = list && radio.fd < 0 && radio.receiver ? g_unix_fd_list_get(list, handle, NULL) : -1;
            if (fd < 0 || !adapter_peer(device) || strlen(device) >= sizeof(radio.peer_path) ||
                fcntl(fd, F_SETFL, O_NONBLOCK) != 0)
            {
                if (fd >= 0)
                    close(fd);
                g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "CarPlay unavailable");
                return;
            }
            snprintf(radio.peer_path, sizeof(radio.peer_path), "%s", device);
            radio.fd = fd;
        }
        else if (!strcmp(operation, "Release"))
            radio.profile_released = true;
        else
        {
            const char *device;
            g_variant_get(arguments, "(&o)", &device);
            if (!strcmp(device, radio.peer_path))
                radio.link_failed = true;
        }
    }
    else if (!strcmp(operation, "RequestConfirmation"))
    {
        const char *device;
        guint32 key;
        g_variant_get(arguments, "(&ou)", &device, &key);
        if (radio.receiver && adapter_peer(device) && !radio.options->headless && key <= 999999)
        {
            demo_wireless_confirm(false);
            radio.confirmation = g_object_ref(invocation);
            radio.passkey = (int)key;
            radio.deadline = rtcp_os_linux.now_ms() + 30000;
            return;
        }
        g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "Confirmation requires the launcher");
        return;
    }
    else if (!strcmp(operation, "AuthorizeService"))
    {
        const char *device, *service;
        g_variant_get(arguments, "(&o&s)", &device, &service);
        if (!radio.receiver || !adapter_peer(device) || g_ascii_strcasecmp(service, uuid))
        {
            g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "Service unavailable");
            return;
        }
    }
    else
        demo_wireless_confirm(false);
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static void bluez_appeared(GDBusConnection *connection, const gchar *name, const gchar *owner, gpointer context)
{
    (void)name;
    (void)context;
    bluetooth_stop();
    g_set_object(&radio.bus, connection);
    g_free(radio.bluez_owner);
    radio.bluez_owner = g_strdup(owner);
    radio.retry = 0;
}

static void bluez_vanished(GDBusConnection *connection, const gchar *name, gpointer context)
{
    (void)name;
    (void)context;
    radio.owner_changed = true;
    bluetooth_stop();
    g_set_object(&radio.bus, connection);
    g_clear_pointer(&radio.bluez_owner, g_free);
    radio.startup_discovery_opened = false;
}

static bool bluetooth_start(void)
{
    static const GDBusInterfaceVTable methods = { .method_call = method };
    radio.node = g_dbus_node_info_new_for_xml(introspection, NULL);
    if (!radio.node)
        return false;
    radio.profile = g_dbus_connection_register_object(radio.bus, profile_path, radio.node->interfaces[0],
                                                      &methods, NULL, NULL, NULL);
    radio.agent = g_dbus_connection_register_object(radio.bus, agent_path, radio.node->interfaces[1],
                                                    &methods, NULL, NULL, NULL);
    if (!radio.profile || !radio.agent)
        return false;
    radio.step = RADIO_POWER;
    bluetooth_advance();
    return true;
}

static GVariant *profile_arguments(void)
{
    GVariantBuilder properties;
    g_variant_builder_init(&properties, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&properties, "{sv}", "Name", g_variant_new_string("Luban CarPlay"));
    g_variant_builder_add(&properties, "{sv}", "Role", g_variant_new_string("server"));
    g_variant_builder_add(&properties, "{sv}", "Channel", g_variant_new_uint16(RFCOMM_CHANNEL));
    g_variant_builder_add(&properties, "{sv}", "RequireAuthentication", g_variant_new_boolean(true));
    char *record = g_strdup_printf(service_record, uuid, (unsigned)RFCOMM_CHANNEL);
    g_variant_builder_add(&properties, "{sv}", "ServiceRecord", g_variant_new_take_string(record));
    return g_variant_new("(os@a{sv})", profile_path, uuid, g_variant_builder_end(&properties));
}

void demo_wireless_discoverable(void)
{
    if (!radio.receiver || radio.step != RADIO_READY)
        return;
    radio.step = RADIO_PAIRABLE_TIMEOUT;
    bluetooth_advance();
}

static void bluetooth_reply(GObject *object, GAsyncResult *result, gpointer context)
{
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, NULL);
    bool succeeded = reply != NULL;
    g_clear_pointer(&reply, g_variant_unref);
    if (GPOINTER_TO_UINT(context) != radio.bus_generation)
        return;
    radio.call_pending = false;
    if (!succeeded)
    {
        if (radio.step >= RADIO_PAIRABLE_TIMEOUT)
        {
            radio.step = RADIO_READY;
            fprintf(stderr, "carplay: could not open Bluetooth pairing window\n");
        }
        else
            bluetooth_stop();
        radio.retry = rtcp_os_linux.now_ms() + 2000;
        return;
    }
    if (radio.step == RADIO_AGENT)
        radio.agent_registered = true;
    if (radio.step == RADIO_POWER)
        demo_boot_mark("bluetooth-powered");
    if (radio.step == RADIO_PROFILE)
    {
        radio.profile_registered = true;
        demo_boot_mark("bluetooth-profile");
        fprintf(stderr, "carplay: BlueZ RFCOMM adapter ready\n");
    }
    if (radio.step == RADIO_DISCOVERABLE)
    {
        radio.startup_discovery_opened = true;
        radio.step = RADIO_READY;
        demo_boot_mark("wireless-pairing");
        fprintf(stderr, "carplay: Bluetooth pairing available for 120 seconds\n");
    }
    else
        ++radio.step;
    bluetooth_advance();
}

static void bluetooth_advance(void)
{
    const char *path = "/org/bluez";
    const char *interface = "org.bluez.AgentManager1";
    const char *method_name;
    const char *property = NULL;
    GVariant *value;
    switch (radio.step)
    {
    case RADIO_POWER:
        property = "Powered";
        value = g_variant_new_boolean(true);
        break;
    case RADIO_PAIRABLE_TIMEOUT:
        property = "PairableTimeout";
        value = g_variant_new_uint32(120);
        break;
    case RADIO_DISCOVERABLE_TIMEOUT:
        property = "DiscoverableTimeout";
        value = g_variant_new_uint32(120);
        break;
    case RADIO_PAIRABLE:
        property = "Pairable";
        value = g_variant_new_boolean(true);
        break;
    case RADIO_DISCOVERABLE:
        property = "Discoverable";
        value = g_variant_new_boolean(true);
        break;
    case RADIO_AGENT:
        method_name = "RegisterAgent";
        value = g_variant_new("(os)", agent_path, "DisplayYesNo");
        break;
    case RADIO_DEFAULT_AGENT:
        method_name = "RequestDefaultAgent";
        value = g_variant_new("(o)", agent_path);
        break;
    case RADIO_PROFILE:
        interface = "org.bluez.ProfileManager1";
        method_name = "RegisterProfile";
        value = profile_arguments();
        break;
    default:
        return;
    }
    if (property)
    {
        path = radio.adapter_path;
        interface = "org.freedesktop.DBus.Properties";
        method_name = "Set";
        value = g_variant_new("(ssv)", "org.bluez.Adapter1", property, value);
    }
    radio.call_pending = true;
    g_dbus_connection_call(radio.bus, radio.bluez_owner, path, interface, method_name, value, NULL,
                           G_DBUS_CALL_FLAGS_NONE, 750, NULL, bluetooth_reply, GUINT_TO_POINTER(radio.bus_generation));
}

int demo_wireless_passkey(void)
{
    return radio.passkey;
}

static bool get_wifi(iap2_session_wifi_t *output, void *context)
{
    (void)context;
    if (!radio.receiver || !radio.address[0])
        return false;
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    struct ifreq request = { 0 };
    snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", radio.options->interface);
    int result = ioctl(fd, SIOCGIFHWADDR, &request);
    close(fd);
    if (result != 0)
        return false;
    *output = (iap2_session_wifi_t){ .ssid = radio.ssid, .passphrase = radio.password, .channel = radio.channel, .security_type = 2 };
    memcpy(output->bssid, request.ifr_hwaddr.sa_data, sizeof(output->bssid));
    return true;
}

static bool get_airplay(iap2_session_airplay_t *output, void *context)
{
    (void)context;
    rtcp_receiver_state_t state;
    if (!radio.receiver || !radio.address[0] || rtcp_receiver_get_state(radio.receiver, &state) != RTCP_OK)
        return false;
    *output = (iap2_session_airplay_t){
        .airplay_port = rtcp_receiver_port(radio.receiver),
        .ip_address = radio.address,
        .device_identifier = radio.identity->device_id,
        .public_key = radio.identity->public_key,
        .source_version = AIRPLAY_INFO_DEFAULT_SOURCE_VERSION,
        .mdns_ready = true,
        .event_connected = state.input_ready,
        .transport_connected = state.transport_connected,
    };
    return true;
}

static bool send_rfcomm(const uint8_t *bytes, size_t size, void *context)
{
    (void)context;
    if (size > sizeof(radio.pending) - radio.pending_size)
        return false;
    memcpy(radio.pending + radio.pending_size, bytes, size);
    radio.pending_size += size;
    return true;
}

static bool send_tunnel(const uint8_t *bytes, size_t size, void *context)
{
    (void)context;
    return radio.receiver && rtcp_receiver_send_iap(radio.receiver, bytes, size);
}

static void link_event(iap2_session_event_t event, void *context)
{
    bool *failed = context;
    if (event == IAP2_SESSION_EVENT_LINK_DEAD || event == IAP2_SESSION_EVENT_MFI_FAILED)
        *failed = true;
    fprintf(stderr, "carplay: wireless iAP2 %s event %d\n",
            failed == &radio.tunnel_failed ? "tunnel" : "RFCOMM", event);
}

static iap2_session_t *new_link(bool tunnel)
{
    iap2_session_config_t config = {
        .identity = { .name = "Luban CarPlay", .model_identifier = "D213", .manufacturer = "ArtInChip", .serial_number = radio.identity->device_id, .firmware_version = "0.1", .hardware_version = "DEMO128", .language = "en", .wifi_ssid = radio.ssid, .provide_location = tunnel && demo_gnss_available() },
        .mfi_provider = radio.options->mfi_provider,
        .tx = tunnel ? send_tunnel : send_rfcomm,
        .get_wifi = get_wifi,
        .get_airplay = get_airplay,
        .on_event = link_event,
        .context = tunnel ? &radio.tunnel_failed : &radio.link_failed,
        .airplay_tunnel = tunnel,
        .on_now_playing_details = tunnel ? demo_track_update : NULL,
        .on_phone_status = tunnel ? demo_phone_update : NULL,
        .on_route_details = tunnel ? demo_route_update : NULL,
    };
    iap2_session_t *session = iap2_session_create(&config);
    if (session)
    {
        uint64_t now = rtcp_os_linux.now_ms();
        iap2_session_link_up(session, now);
        if (tunnel)
            iap2_session_notify_airplay_event(session, true, now);
    }
    return session;
}

static void tunnel_stop(void *context)
{
    (void)context;
    if (radio.tunnel)
    {
        iap2_session_link_down(radio.tunnel);
        iap2_session_destroy(radio.tunnel);
        radio.tunnel = NULL;
    }
    radio.tunnel_failed = false;
    radio.gnss = (demo_gnss_stream_t){ 0 };
}

static bool tunnel_bytes(const uint8_t *bytes, size_t length, void *context)
{
    (void)context;
    if (!radio.tunnel)
        radio.tunnel = new_link(true);
    if (!radio.tunnel)
        return false;
    iap2_session_feed(radio.tunnel, bytes, length, rtcp_os_linux.now_ms());
    return !radio.tunnel_failed;
}

static bool tunnel_tick(void *context)
{
    (void)context;
    if (radio.tunnel)
    {
        uint64_t now = rtcp_os_linux.now_ms();
        iap2_session_advance(radio.tunnel, now);
        demo_gnss_tick(&radio.gnss, radio.tunnel, now);
    }
    return !radio.tunnel_failed;
}

static bool handoff(const char *device, void *context)
{
    (void)device;
    (void)context;
    radio.handoff = radio.fd >= 0;
    return true;
}

void demo_wireless_backend(rtcp_iap2_backend_t *backend)
{
    *backend = (rtcp_iap2_backend_t){ .on_bytes = tunnel_bytes, .on_tick = tunnel_tick, .on_stop = tunnel_stop, .disable_bluetooth = handoff };
}

static void rfcomm_close(void)
{
    if (radio.link)
    {
        iap2_session_link_down(radio.link);
        iap2_session_destroy(radio.link);
        radio.link = NULL;
    }
    if (radio.fd >= 0)
        close(radio.fd);
    radio.fd = -1;
    radio.pending_size = 0;
    radio.peer_path[0] = '\0';
    radio.link_failed = false;
    radio.handoff = false;
    radio.event_connected = false;
}

void demo_wireless_step(rtcp_receiver_t *receiver, const char *ip, bool active)
{
    radio.receiver = active ? receiver : NULL;
    if (radio.receiver)
        snprintf(radio.address, sizeof(radio.address), "%s", ip);
    g_main_context_iteration(NULL, false);
    uint64_t now = rtcp_os_linux.now_ms();
    if (radio.profile_released)
        bluetooth_stop();
    if (now >= radio.retry && (!radio.owner_watch || !radio.bus || g_dbus_connection_is_closed(radio.bus)))
    {
        if (radio.owner_watch)
        {
            g_bus_unwatch_name(radio.owner_watch);
            bluez_vanished(NULL, NULL, NULL);
        }
        radio.owner_watch = g_bus_watch_name(G_BUS_TYPE_SYSTEM, "org.bluez", G_BUS_NAME_WATCHER_FLAGS_NONE,
                                             bluez_appeared, bluez_vanished, NULL, NULL);
        radio.retry = now + 2000;
    }
    if (radio.confirmation && (!radio.receiver || now >= radio.deadline))
        demo_wireless_confirm(false);
    if (radio.bluez_owner && radio.step == RADIO_IDLE && now >= radio.retry)
    {
        if (!bluetooth_start())
        {
            bluetooth_stop();
            radio.retry = now + 2000;
        }
    }
    if (radio.receiver && radio.step == RADIO_READY && !radio.startup_discovery_opened && now >= radio.retry)
        demo_wireless_discoverable();
    if (!radio.receiver || radio.link_failed || (radio.handoff && !radio.tunnel_failed && iap2_session_is_authenticated(radio.tunnel) && iap2_session_identification_accepted(radio.tunnel)))
        rfcomm_close();
    if (radio.fd < 0)
        return;
    if (!radio.link)
        radio.link = new_link(false);
    if (!radio.link)
    {
        rfcomm_close();
        return;
    }
    rtcp_receiver_state_t state = { 0 };
    rtcp_receiver_get_state(radio.receiver, &state);
    if (state.input_ready != radio.event_connected)
    {
        radio.event_connected = state.input_ready;
        iap2_session_notify_airplay_event(radio.link, state.input_ready, now);
    }
    if (radio.pending_size)
    {
        ssize_t written = send(radio.fd, radio.pending, radio.pending_size, MSG_NOSIGNAL);
        if (written > 0)
        {
            radio.pending_size -= (size_t)written;
            memmove(radio.pending, radio.pending + written, radio.pending_size);
        }
        else if (written < 0 && errno != EAGAIN && errno != EINTR)
        {
            rfcomm_close();
            return;
        }
    }
    uint8_t buffer[2048];
    ssize_t read_bytes = recv(radio.fd, buffer, sizeof(buffer), 0);
    if (read_bytes > 0)
        iap2_session_feed(radio.link, buffer, (size_t)read_bytes, now);
    else if (read_bytes == 0 || (errno != EAGAIN && errno != EINTR))
    {
        rfcomm_close();
        return;
    }
    iap2_session_advance(radio.link, now);
}

int demo_wireless_configure(const demo_options_t *options, const demo_identity_t *identity)
{
    radio.options = options;
    radio.identity = identity;
    if (!options->adapter[0] ||
        strspn(options->adapter, "abcdefghijklmnopqrstuvwxyz0123456789") != strlen(options->adapter) ||
        snprintf(radio.adapter_path, sizeof(radio.adapter_path), "/org/bluez/%s", options->adapter) >=
            (int)sizeof(radio.adapter_path))
        return RTCP_EINVAL;
    FILE *configuration = fopen(options->ap_config, "r");
    if (!configuration)
        return RTCP_EIO;
    char line[256];
    while (fgets(line, sizeof(line), configuration))
    {
        line[strcspn(line, "\r\n")] = '\0';
        size_t length = strlen(line);
        if (!strncmp(line, "ssid=", 5) && length > 5 && length < 5 + sizeof(radio.ssid))
            memcpy(radio.ssid, line + 5, length - 4);
        if (!strncmp(line, "wpa_passphrase=", 15) && length >= 23 && length < 15 + sizeof(radio.password))
            memcpy(radio.password, line + 15, length - 14);
        unsigned channel;
        char trailing;
        if (sscanf(line, "channel=%u%c", &channel, &trailing) == 1 && channel > 0 && channel <= 177)
            radio.channel = (uint8_t)channel;
    }
    fclose(configuration);
    return radio.channel && radio.ssid[0] && radio.password[0] ? RTCP_OK : RTCP_EINVAL;
}

static void bluetooth_stop(void)
{
    ++radio.bus_generation;
    demo_wireless_confirm(false);
    rfcomm_close();
    if (radio.bus && radio.bluez_owner && !radio.owner_changed)
    {
        if (radio.profile_registered || (radio.call_pending && radio.step == RADIO_PROFILE))
            g_dbus_connection_call(radio.bus, radio.bluez_owner, "/org/bluez", "org.bluez.ProfileManager1",
                                   "UnregisterProfile", g_variant_new("(o)", profile_path), NULL,
                                   G_DBUS_CALL_FLAGS_NONE, 750, NULL, NULL, NULL);
        if (radio.agent_registered || (radio.call_pending && radio.step == RADIO_AGENT))
            g_dbus_connection_call(radio.bus, radio.bluez_owner, "/org/bluez", "org.bluez.AgentManager1",
                                   "UnregisterAgent", g_variant_new("(o)", agent_path), NULL,
                                   G_DBUS_CALL_FLAGS_NONE, 750, NULL, NULL, NULL);
    }
    if (radio.profile)
        g_dbus_connection_unregister_object(radio.bus, radio.profile);
    if (radio.agent)
        g_dbus_connection_unregister_object(radio.bus, radio.agent);
    if (radio.node)
        g_dbus_node_info_unref(radio.node);
    radio.profile = radio.agent = 0;
    radio.profile_registered = radio.agent_registered = false;
    radio.owner_changed = false;
    radio.profile_released = false;
    radio.call_pending = false;
    radio.step = RADIO_IDLE;
    radio.node = NULL;
}

void demo_wireless_close(void)
{
    radio.receiver = NULL;
    if (radio.owner_watch)
        g_bus_unwatch_name(radio.owner_watch);
    radio.owner_watch = 0;
    bluetooth_stop();
    g_clear_object(&radio.bus);
    g_clear_pointer(&radio.bluez_owner, g_free);
    tunnel_stop(NULL);
}

#define _GNU_SOURCE
#include "linux_demo.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <linux/rtnetlink.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "airplay_info_plist.h"
#include "airplay_pairing_store.h"
#include "airplay_txt.h"
#include "connection_retry.h"
#include "mdns_responder.h"
#include "mbedtls/platform_util.h"
#include "rtcp/version.h"
#include "rtcp_linux.h"
#include "receiver_session.h"

#ifdef CARPLAY_LVGL
#include "../../assets/rt-carplay-oem-png.h"

static const rtcp_oem_icon_t demo_oem_icon = {
    .png = carplay_demo_oem_icon_png,
    .png_len = sizeof(carplay_demo_oem_icon_png),
    .width_pixels = CARPLAY_DEMO_OEM_ICON_SIZE,
    .height_pixels = CARPLAY_DEMO_OEM_ICON_SIZE,
};

static int launcher_set_projection(bool enabled, void *context)
{
    rtcp_receiver_t *receiver = *(rtcp_receiver_t **)context;
    return rtcp_receiver_set_projection_enabled(receiver, enabled);
}

static int launcher_set_appearance(rtcp_appearance_setting_t appearance, void *context)
{
    return rtcp_receiver_set_appearance(*(rtcp_receiver_t **)context, appearance, false, false);
}

static void launcher_request_keyframe(void *context)
{
    rtcp_receiver_t *receiver = *(rtcp_receiver_t **)context;
    rtcp_receiver_request_screen_keyframe(receiver);
}

static void launcher_send_touch(const rtcp_touch_contact_t *contacts, void *context)
{
    rtcp_receiver_t *receiver = *(rtcp_receiver_t **)context;
    if (receiver)
        rtcp_receiver_send_touch(receiver, contacts);
}
#endif

static const char demo_name[] = "Luban CarPlay";
static char active_interface[IF_NAMESIZE];
static struct rtcp_net network;
static pthread_mutex_t metadata_lock = PTHREAD_MUTEX_INITIALIZER;
static carplay_phone_track_t current_track;
static demo_phone_status_t current_phone;
static demo_route_t current_route;
volatile sig_atomic_t demo_stop;

static void stop(int signal_number)
{
    (void)signal_number;
    demo_stop = 1;
}

void demo_track_update(const rtcp_now_playing_details_t *update, void *context)
{
    (void)context;
    pthread_mutex_lock(&metadata_lock);
    carplay_phone_track_update(&current_track, update, false);
    pthread_mutex_unlock(&metadata_lock);
}

void demo_phone_update(const rtcp_phone_status_t *update, void *context)
{
    (void)context;
    pthread_mutex_lock(&metadata_lock);
    switch (update->kind)
    {
    case RTCP_PHONE_STATUS_INVALIDATED:
        current_phone = (demo_phone_status_t){ 0 };
        break;
    case RTCP_PHONE_STATUS_POWER:
        if (update->data.power.has_battery_level)
        {
            current_phone.battery = update->data.power.battery_level;
            current_phone.has_battery = current_phone.battery <= 100;
        }
        if (update->data.power.has_charging_state)
        {
            current_phone.has_charging = true;
            current_phone.charging = update->data.power.charging_state;
        }
        break;
    case RTCP_PHONE_STATUS_CELLULAR:
        if (update->data.cellular.carrier)
            snprintf(current_phone.carrier, sizeof(current_phone.carrier), "%s", update->data.cellular.carrier);
        if (update->data.cellular.signal_strength)
        {
            current_phone.has_signal = update->data.cellular.signal_strength_count > 0;
            if (current_phone.has_signal)
                current_phone.signal = update->data.cellular.signal_strength[0];
        }
        if (update->data.cellular.has_airplane_mode)
            current_phone.airplane = update->data.cellular.airplane_mode;
        break;
    case RTCP_PHONE_STATUS_CALL:
        if (update->data.call.has_status)
        {
            current_phone.has_call = true;
            current_phone.call = update->data.call.status;
        }
        break;
    }
    pthread_mutex_unlock(&metadata_lock);
}

void demo_route_update(const rtcp_route_details_t *update, void *context)
{
    (void)context;
    pthread_mutex_lock(&metadata_lock);
    if (update->invalidated)
        current_route = (demo_route_t){ 0 };
    else
    {
        const rtcp_route_guidance_t *route = &update->guidance;
        if (route->road)
            snprintf(current_route.road, sizeof(current_route.road), "%s", route->road);
        if (route->destination)
            snprintf(current_route.destination, sizeof(current_route.destination), "%s", route->destination);
        if (route->distance_to_maneuver)
            snprintf(current_route.distance, sizeof(current_route.distance), "%s", route->distance_to_maneuver);
    }
    pthread_mutex_unlock(&metadata_lock);
}

void demo_phone_snapshot(carplay_phone_track_t *track, demo_phone_status_t *phone, demo_route_t *route)
{
    pthread_mutex_lock(&metadata_lock);
    *track = current_track;
    *phone = current_phone;
    *route = current_route;
    pthread_mutex_unlock(&metadata_lock);
}

static int listen_on_interface(uint8_t family, uint16_t port)
{
    return rtcp_net_linux_listen_on(family, port, active_interface);
}

static int udp_on_interface(uint8_t family, uint16_t port, bool reuse)
{
    return rtcp_net_linux_udp_open_on(family, port, reuse, active_interface);
}

static int join_on_interface(int fd, const uint8_t group[4])
{
    struct ip_mreqn request = { .imr_ifindex = (int)if_nametoindex(active_interface) };
    memcpy(&request.imr_multiaddr, group, 4);
    return request.imr_ifindex != 0 &&
                   setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) == 0 &&
                   setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &request, sizeof(request)) == 0
               ? RTCP_OK
               : RTCP_EIO;
}

static bool lan_address(const char *interface, char address[64])
{
    struct ifaddrs *addresses;
    if (getifaddrs(&addresses) != 0)
        return false;
    bool found = false;
    for (struct ifaddrs *entry = addresses; entry; entry = entry->ifa_next)
    {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET ||
            !(entry->ifa_flags & IFF_UP) || strcmp(entry->ifa_name, interface) != 0)
            continue;
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)entry->ifa_addr;
        found = inet_ntop(AF_INET, &ipv4->sin_addr, address, 64) != NULL;
        break;
    }
    freeifaddrs(addresses);
    return found;
}

static int watch_addresses(void)
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd >= 0)
    {
        struct sockaddr_nl address = { .nl_family = AF_NETLINK, .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR };
        if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0)
        {
            close(fd);
            fd = -1;
        }
    }
    return fd;
}

static bool addresses_changed(int fd)
{
    bool changed = false;
    for (unsigned batch = 0; batch < 16; ++batch)
    {
        uint8_t buffer[4096];
        ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
        if (got <= 0)
            return changed || errno == ENOBUFS;
        changed = true;
    }
    return changed;
}

static mdns_responder_t *advertise(const demo_endpoint_t *endpoint,
                                   const demo_identity_t *identity,
                                   const airplay_info_plist_config_t *info, uint16_t port)
{
    char features[32], host[32];
    if (!airplay_txt_format_features(airplay_info_plist_features(info), features, sizeof(features)))
        return NULL;
    snprintf(host, sizeof(host), "luban-%.12s", identity->public_key);
    airplay_txt_config_t text = {
        .device_id = identity->device_id,
        .public_key = identity->public_key,
        .pairing_id = identity->device_id,
        .features = features,
        .model = info->model,
        .source_version = AIRPLAY_INFO_DEFAULT_SOURCE_VERSION,
        .protocol_version = "1.1",
    };
    airplay_txt_record_t records[AIRPLAY_TXT_MAX_RECORDS];
    size_t count = airplay_txt_build(&text, records, AIRPLAY_TXT_MAX_RECORDS);
    uint8_t txt[768];
    mdns_responder_config_t config = {
        .host_name = host,
        .instance_name = demo_name,
        .port = port,
        .txt = txt,
        .txt_len = airplay_txt_encode(records, count, txt, sizeof(txt)),
    };
    if (!config.txt_len || inet_pton(AF_INET, endpoint->address, config.ipv4) != 1)
        return NULL;
    mdns_responder_t *responder = mdns_responder_create(&config);
    if (responder && mdns_responder_open(responder, &network) == RTCP_OK)
        return responder;
    mdns_responder_destroy(responder);
    return NULL;
}

static void usage(FILE *stream)
{
    fprintf(stream, "rt-carplay-demo [--wired | --lan IFACE] [--headless]\n"
                    "  --wireless AP_IFACE --ap-config FILE --bluetooth-adapter hci0\n"
                    "  --state-dir DIR --framebuffer DEVICE --touch DEVICE --i2c DEVICE\n"
                    "  --mfi-mode hardware|software|auto --mfi-cert DER --mfi-key DER --mfi-protocol 2|3\n"
                    "  --playback PCM --capture PCM --port PORT --fps 30|60 --gnss TTY\n"
                    "  --help --version\n");
}

static bool number(const char *text, unsigned long maximum, unsigned long *value)
{
    if (text[0] < '0' || text[0] > '9')
        return false;
    char *end;
    errno = 0;
    *value = strtoul(text, &end, 10);
    return errno == 0 && *end == '\0' && *value != 0 && *value <= maximum;
}

static int lock_directory(const char *directory)
{
    struct stat properties;
    if (directory[0] != '/')
    {
        errno = EINVAL;
        return -1;
    }
    if ((mkdir(directory, 0700) < 0 && errno != EEXIST) || lstat(directory, &properties) < 0)
        return -1;
    if (!S_ISDIR(properties.st_mode) || (properties.st_mode & 0077) != 0 ||
        properties.st_uid != geteuid())
    {
        errno = S_ISDIR(properties.st_mode) ? EACCES : ENOTDIR;
        return -1;
    }
    char path[256];
    if (snprintf(path, sizeof(path), "%s/owner.lock", directory) >= (int)sizeof(path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
        return fd;
    int error = errno;
    close(fd);
    errno = error;
    return -1;
}

static bool same_endpoint(const demo_endpoint_t *left, const demo_endpoint_t *right)
{
    return left->generation == right->generation && !strcmp(left->interface, right->interface) &&
           !strcmp(left->address, right->address);
}

typedef struct
{
    const demo_options_t *options;
    demo_identity_t *identity;
    rtcp_pairing_provider_t *pairing;
    demo_mfi_t **mfi;
    int *lock;
    bool identity_ready;
    int result;
    atomic_bool finished;
} demo_credentials_t;

static void *prepare_credentials(void *data)
{
    demo_credentials_t *credentials = data;
    const demo_options_t *options = credentials->options;
    credentials->result = RTCP_EIO;
    if (*credentials->lock < 0)
        *credentials->lock = lock_directory(options->directory);
    if (!credentials->identity_ready && *credentials->lock >= 0)
    {
        int status = demo_identity_open(credentials->identity, options->directory, credentials->pairing);
        credentials->identity_ready = status == RTCP_OK;
        if (status != RTCP_OK)
            fprintf(stderr, "carplay: could not load identity and pair records (%d)\n", status);
    }
    if (credentials->identity_ready)
    {
        bool use_mfi = options->wired || options->wireless || options->mfi_mode != DEMO_MFI_DEFAULT;
        credentials->result = use_mfi ? demo_mfi_open(options, credentials->mfi) : RTCP_OK;
    }
    atomic_store(&credentials->finished, true);
    return NULL;
}

static int run(demo_options_t *options, int *state_lock)
{
    rtcp_receiver_config_t config = { 0 };
    demo_identity_t identity = { 0 };
    demo_mfi_t *mfi = NULL;
    rtcp_receiver_t *receiver = NULL;
    mdns_responder_t *mdns = NULL;
    int route_watch = -1;
    int result = RTCP_OK;
#ifdef CARPLAY_LVGL
    bool launcher_ready = !options->headless && demo_launcher_open(options) == RTCP_OK;
    if (!options->headless && !launcher_ready)
        fprintf(stderr, "carplay: local framebuffer unavailable\n");
    const demo_launcher_commands_t launcher = {
        .set_projection = launcher_set_projection,
        .request_keyframe = launcher_request_keyframe,
        .send_touch = launcher_send_touch,
        .set_appearance = launcher_set_appearance,
        .context = &receiver,
    };
    uint64_t receiver_generation = 0;
#endif
    demo_credentials_t credentials = {
        .options = options,
        .identity = &identity,
        .pairing = &config.platform.pairing,
        .mfi = &mfi,
        .lock = state_lock,
    };
    while (!demo_stop)
    {
#ifdef CARPLAY_LVGL
        pthread_t startup;
        atomic_store(&credentials.finished, false);
        if (launcher_ready && pthread_create(&startup, NULL, prepare_credentials, &credentials) == 0)
        {
            while (!demo_stop && !atomic_load(&credentials.finished))
            {
                demo_launcher_step(&launcher, &(demo_launcher_snapshot_t){ 0 });
                rtcp_os_linux.sleep_ms(10);
            }
            pthread_join(startup, NULL);
        }
        else
#endif
            prepare_credentials(&credentials);
        if (credentials.result == RTCP_OK)
            break;
#ifdef CARPLAY_LVGL
        if (launcher_ready)
        {
            uint64_t retry_at = rtcp_os_linux.now_ms() + 3000;
            while (!demo_stop && rtcp_os_linux.now_ms() < retry_at)
            {
                demo_launcher_step(&launcher, &(demo_launcher_snapshot_t){ 0 });
                rtcp_os_linux.sleep_ms(10);
            }
            continue;
        }
#endif
        result = RTCP_EIO;
        goto done;
    }
    if (demo_stop)
    {
        result = RTCP_OK;
        goto done;
    }
    options->mfi_provider = demo_mfi_provider(mfi);
    config.platform.mfi_provider = options->mfi_provider;
    demo_boot_mark("receiver-identity");
    if (options->gnss && !demo_gnss_open(options->gnss))
        fprintf(stderr, "carplay: GNSS unavailable on %s\n", options->gnss);
#ifdef CARPLAY_AIC_MPP
    if (!options->headless && demo_video_open(options, &config.media) != RTCP_OK)
    {
        fprintf(stderr, "carplay: hardware video unavailable\n");
        demo_video_close();
    }
#endif
    bool playback = false, capture = false;
#ifdef CARPLAY_ALSA
    if (!options->headless)
        demo_audio_open(options, &config.media, &playback, &capture);
#endif
#ifdef CARPLAY_LVGL
    if (launcher_ready)
    {
        demo_launcher_attach_media(&config.media);
        demo_launcher_apply_layout();
    }
#endif
    airplay_info_plist_config_t info = {
        .device_id = identity.device_id,
        .name = demo_name,
        .model = "ArtInChip",
        .width_pixels = options->width,
        .height_pixels = options->height,
        .max_fps = options->fps,
        .audio_output = playback,
        .audio_input = capture,
        .audio_compressed = playback,
    };
#ifdef CARPLAY_LVGL
    if (launcher_ready)
        info.width_pixels = demo_launcher_projection_width();
    if (!options->headless)
    {
        info.oem_icons = &demo_oem_icon;
        info.oem_icon_count = 1;
        info.oem_icon_label = "rt-carplay";
    }
#endif
    uint8_t plist[AIRPLAY_INFO_PLIST_MAX_BYTES];
    network = rtcp_net_linux;
    network.listen = listen_on_interface;
    network.udp_open = udp_on_interface;
    network.udp_join = join_on_interface;
    config.platform.net = &network;
    config.platform.os = &rtcp_os_linux;
    config.platform.info_plist = plist;
    config.port = options->port;
    const carplay_session_callbacks_t callbacks = {
        .now_playing = demo_track_update,
#ifdef CARPLAY_LVGL
        .host_ui = launcher_ready ? demo_launcher_show_local : NULL,
#endif
    };
    demo_endpoint_t active = { 0 }, candidate = { 0 };
    rtcp_receiver_state_t previous = { 0 };
#ifdef CARPLAY_AIC_MPP
    bool previous_projection = false;
#endif
    carplay_connection_retry_t retry = { 0 };
    uint64_t advertise_after = 0;
    uint64_t address_rescan = 0;
    demo_endpoint_t lan = { 0 };
    if (options->interface)
    {
        route_watch = watch_addresses();
        snprintf(lan.interface, sizeof(lan.interface), "%s", options->interface);
    }
#ifdef CARPLAY_BLUEZ
    bool wireless_ready = false;
    uint64_t wireless_retry = 0;
#endif
#ifdef CARPLAY_USB
    bool wired_ready = false;
    uint64_t wired_retry = 0;
    uint64_t wired_layout_retry_until = 0;
#endif
    while (!demo_stop)
    {
        uint64_t now = rtcp_os_linux.now_ms();
        demo_endpoint_t endpoint = { 0 };
#ifdef CARPLAY_USB
        if (options->wired && !wired_ready && now >= wired_retry)
        {
            wired_ready = demo_wired_start(options, &identity) == RTCP_OK;
            if (!wired_ready)
                fprintf(stderr, "carplay: cannot start USB host worker, retrying\n");
            wired_retry = now + 3000;
        }
        if (wired_ready)
            demo_wired_snapshot(&endpoint);
#endif
#ifdef CARPLAY_BLUEZ
        if (options->wireless && !wireless_ready && now >= wireless_retry)
        {
            wireless_ready = demo_wireless_configure(options, &identity) == RTCP_OK;
            if (!wireless_ready)
                fprintf(stderr, "carplay: waiting for valid AP configuration %s\n", options->ap_config);
            else
                address_rescan = 0;
            wireless_retry = now + 1000;
        }
#endif
        if ((!options->wired
#ifdef CARPLAY_BLUEZ
             || wireless_ready
#endif
             ) &&
            ((route_watch >= 0 && addresses_changed(route_watch)) || now >= address_rescan))
        {
            if (!lan_address(options->interface, lan.address))
                lan.address[0] = '\0';
            address_rescan = now + (route_watch >= 0 ? 5000 : 500);
        }
        if (!endpoint.address[0] && lan.address[0]
#ifdef CARPLAY_USB
            && now >= wired_layout_retry_until
#endif
            )
            endpoint = lan;

        bool layout_changed = false;
#ifdef CARPLAY_LVGL
        if (launcher_ready && info.width_pixels != demo_launcher_projection_width())
        {
            layout_changed = true;
            info.width_pixels = demo_launcher_projection_width();
            fprintf(stderr, "carplay: renegotiating display %ux%u @ %u fps\n",
                    info.width_pixels, info.height_pixels, info.max_fps);
        }
#endif
        if (layout_changed || !same_endpoint(&candidate, &endpoint))
        {
            candidate = endpoint;
            retry = (carplay_connection_retry_t){ 0 };
        }
        if (receiver && (layout_changed || !same_endpoint(&active, &endpoint)))
        {
#ifdef CARPLAY_USB
            if (layout_changed && active.generation && active.generation == endpoint.generation)
            {
                demo_wired_reconnect(active.generation);
                wired_layout_retry_until = now + 5000;
                endpoint = (demo_endpoint_t){ 0 };
            }
#endif
#ifdef CARPLAY_BLUEZ
            if (options->wireless && !same_endpoint(&active, &endpoint))
                demo_wireless_step(NULL, NULL, false);
#endif
            if (mdns)
                mdns_responder_close(mdns, &network);
            mdns_responder_destroy(mdns);
            mdns = NULL;
            result = carplay_session_close(&receiver);
            if (result != RTCP_OK)
                goto done;
#ifdef CARPLAY_AIC_MPP
            demo_video_session(false);
#endif
#ifdef CARPLAY_LVGL
            ++receiver_generation;
#endif
            demo_track_update(&(rtcp_now_playing_details_t){ .invalidated = true }, NULL);
            previous = (rtcp_receiver_state_t){ 0 };
#ifdef CARPLAY_AIC_MPP
            previous_projection = false;
#endif
        }
#ifdef CARPLAY_LVGL
        if (layout_changed)
            demo_launcher_apply_layout();
#endif
        if (!receiver && endpoint.address[0] && !carplay_retry_remaining(&retry, (uint32_t)now))
        {
            info.width_physical_mm = (uint16_t)((200U * info.width_pixels + options->width / 2U) / options->width);
            if (!airplay_info_plist_build(&info, plist, sizeof(plist), &config.platform.info_plist_len))
            {
                result = RTCP_EINVAL;
                goto done;
            }
            memcpy(active_interface, endpoint.interface, sizeof(active_interface));
            config.family = endpoint.generation ? RTCP_AF_INET6 : RTCP_AF_INET;
            config.iap2 = (rtcp_iap2_backend_t){ 0 };
#ifdef CARPLAY_BLUEZ
            if (options->wireless && !endpoint.generation)
                demo_wireless_backend(&config.iap2);
#endif
            result = carplay_session_open(&receiver, &config, &callbacks);
            if (result != RTCP_OK)
            {
                fprintf(stderr, "carplay: listen on %s failed (%d)\n", active_interface, result);
                carplay_retry_schedule(&retry, (uint32_t)now, result);
                result = carplay_session_close(&receiver);
                if (result != RTCP_OK)
                    goto done;
            }
            else
            {
                active = endpoint;
#ifdef CARPLAY_LVGL
                ++receiver_generation;
#endif
                fprintf(stderr, "carplay: RTSP ready on %s [%s]:%u, display %ux%u @ %u fps\n", active.interface,
                        active.address, rtcp_receiver_port(receiver), info.width_pixels, info.height_pixels, info.max_fps);
                demo_boot_mark(active.generation ? "wired-listener" : options->wireless ? "wireless-listener"
                                                                                        : "lan-listener");
                advertise_after = 0;
            }
        }
        if (receiver && !active.generation && !mdns && now >= advertise_after)
        {
            mdns = advertise(&active, &identity, &info, rtcp_receiver_port(receiver));
            if (!mdns)
                fprintf(stderr, "carplay: mDNS unavailable on %s\n", active.interface);
            else
                demo_boot_mark(options->wireless ? "wireless-mdns" : "lan-mdns");
            advertise_after = now + 2000;
        }
#ifdef CARPLAY_BLUEZ
        if (wireless_ready)
            demo_wireless_step(receiver, active.address, mdns != NULL);
#endif
        rtcp_receiver_state_t state = { 0 };
        if (receiver)
        {
            rtcp_receiver_poll(receiver, 5);
            rtcp_receiver_get_state(receiver, &state);
            if (state.input_ready && !previous.input_ready)
                demo_boot_mark(active.generation ? "wired-session" : options->wireless ? "wireless-session"
                                                                                       : "lan-session");
        }
        else
            rtcp_os_linux.sleep_ms(10);
        if (state.transport_connected != previous.transport_connected ||
            state.session_established != previous.session_established ||
            state.input_ready != previous.input_ready || state.video_ready != previous.video_ready)
            fprintf(stderr, "carplay: %s RTSP tcp=%u record=%u event=%u screen=%u\n", active.interface,
                    state.transport_connected, state.session_established, state.input_ready, state.video_ready);
        previous = state;
        if (mdns)
            mdns_responder_poll(mdns, &network, 0);
#ifdef CARPLAY_USB
        if (receiver && active.generation)
            demo_wired_publish(active.generation, rtcp_receiver_port(receiver), &state);
#endif
        bool projection = false;
#ifdef CARPLAY_AIC_MPP
        demo_video_session(state.video_ready);
        if (receiver && demo_video_needs_keyframe())
            rtcp_receiver_request_screen_keyframe(receiver);
        projection = state.input_ready && demo_video_visible();
        if (projection && !previous_projection)
            demo_boot_mark(active.generation ? "wired-video" : options->wireless ? "wireless-video"
                                                                                 : "lan-video");
        previous_projection = projection;
#endif
#ifdef CARPLAY_LVGL
        const demo_launcher_snapshot_t snapshot = {
            .receiver = state,
            .projection_owner = rtcp_receiver_get_projection_owner(receiver),
            .stream_generation = rtcp_receiver_get_projection_stream_generation(receiver),
            .receiver_generation = receiver_generation,
            .wired = active.generation != 0,
            .projection_visible = projection,
        };
        demo_launcher_step(&launcher, &snapshot);
#else
        (void)projection;
#endif
    }
    result = RTCP_OK;
done:
    if (route_watch >= 0)
        close(route_watch);
#ifdef CARPLAY_USB
    demo_wired_stop();
#endif
    if (mdns)
        mdns_responder_close(mdns, &network);
    mdns_responder_destroy(mdns);
    (void)carplay_session_close(&receiver);
#ifdef CARPLAY_BLUEZ
    demo_wireless_close();
#endif
#ifdef CARPLAY_AIC_MPP
    demo_video_close();
#endif
#ifdef CARPLAY_ALSA
    demo_audio_close();
#endif
#ifdef CARPLAY_LVGL
    demo_launcher_close();
#endif
    demo_mfi_close(mfi);
    demo_gnss_close();
    mbedtls_platform_zeroize(&identity, sizeof(identity));
    return result;
}

int main(int argc, char **argv)
{
    enum
    {
        OPTION_WIRED = 256,
        OPTION_LAN,
        OPTION_WIRELESS,
        OPTION_AP_CONFIG,
        OPTION_ADAPTER,
        OPTION_HEADLESS,
        OPTION_STATE,
        OPTION_FB,
        OPTION_TOUCH,
        OPTION_I2C,
        OPTION_MFI_MODE,
        OPTION_MFI_CERT,
        OPTION_MFI_KEY,
        OPTION_MFI_PROTOCOL,
        OPTION_PLAYBACK,
        OPTION_CAPTURE,
        OPTION_PORT,
        OPTION_FPS,
        OPTION_GNSS
    };
    static const struct option arguments[] = {
        { "wired", no_argument, NULL, OPTION_WIRED },
        { "lan", required_argument, NULL, OPTION_LAN },
        { "wireless", required_argument, NULL, OPTION_WIRELESS },
        { "ap-config", required_argument, NULL, OPTION_AP_CONFIG },
        { "bluetooth-adapter", required_argument, NULL, OPTION_ADAPTER },
        { "headless", no_argument, NULL, OPTION_HEADLESS },
        { "state-dir", required_argument, NULL, OPTION_STATE },
        { "framebuffer", required_argument, NULL, OPTION_FB },
        { "touch", required_argument, NULL, OPTION_TOUCH },
        { "i2c", required_argument, NULL, OPTION_I2C },
        { "mfi-mode", required_argument, NULL, OPTION_MFI_MODE },
        { "mfi-cert", required_argument, NULL, OPTION_MFI_CERT },
        { "mfi-key", required_argument, NULL, OPTION_MFI_KEY },
        { "mfi-protocol", required_argument, NULL, OPTION_MFI_PROTOCOL },
        { "playback", required_argument, NULL, OPTION_PLAYBACK },
        { "capture", required_argument, NULL, OPTION_CAPTURE },
        { "port", required_argument, NULL, OPTION_PORT },
        { "gnss", required_argument, NULL, OPTION_GNSS },
        { "fps", required_argument, NULL, OPTION_FPS },
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    demo_options_t options = {
        .directory = "/var/lib/carplay",
        .framebuffer = "/dev/fb0",
        .ap_config = "/run/carplay/hostapd.conf",
        .adapter = "hci0",
        .touch = "auto",
        .i2c = "/dev/i2c-0",
        .playback = "default",
        .capture = "default",
        .width = 1024,
        .height = 600,
        .fps = 30,
        .wired = true,
    };
    int option;
    unsigned long value;
    while ((option = getopt_long(argc, argv, "hV", arguments, NULL)) != -1)
    {
        switch (option)
        {
        case OPTION_WIRED:
            options.wired = true;
            break;
        case OPTION_LAN:
            options.wired = false;
            options.interface = optarg;
            break;
        case OPTION_WIRELESS:
            options.wireless = true;
            options.interface = optarg;
            break;
        case OPTION_AP_CONFIG:
            options.ap_config = optarg;
            break;
        case OPTION_ADAPTER:
            options.adapter = optarg;
            break;
        case OPTION_HEADLESS:
            options.headless = true;
            break;
        case OPTION_STATE:
            options.directory = optarg;
            break;
        case OPTION_FB:
            options.framebuffer = optarg;
            break;
        case OPTION_TOUCH:
            options.touch = optarg;
            break;
        case OPTION_I2C:
            options.i2c = optarg;
            break;
        case OPTION_MFI_MODE:
            if (!strcmp(optarg, "hardware"))
                options.mfi_mode = DEMO_MFI_HARDWARE;
            else if (!strcmp(optarg, "software"))
                options.mfi_mode = DEMO_MFI_SOFTWARE;
            else if (!strcmp(optarg, "auto"))
                options.mfi_mode = DEMO_MFI_AUTO;
            else
                return 2;
            break;
        case OPTION_MFI_CERT:
            options.mfi_cert = optarg;
            break;
        case OPTION_MFI_KEY:
            options.mfi_key = optarg;
            break;
        case OPTION_MFI_PROTOCOL:
            if (!number(optarg, 3, &value) || value < 2)
                return 2;
            options.mfi_protocol = (uint8_t)value;
            break;
        case OPTION_GNSS:
            options.gnss = optarg;
            break;
        case OPTION_PLAYBACK:
            options.playback = optarg;
            break;
        case OPTION_CAPTURE:
            options.capture = optarg;
            break;
        case OPTION_PORT:
            if (!number(optarg, UINT16_MAX, &value))
                return 2;
            options.port = (uint16_t)value;
            break;
        case OPTION_FPS:
            if (!number(optarg, 60, &value) || (value != 30 && value != 60))
                return 2;
            options.fps = (uint8_t)value;
            options.fps_override = true;
            break;
        case 'h':
            usage(stdout);
            return 0;
        case 'V':
            printf("rt-carplay %s (%s)\n", rtcp_version_string(), rtcp_build_git_sha());
            return 0;
        default:
            return 2;
        }
    }
    if (optind != argc || ((!options.wired || options.wireless) && (!options.interface || !options.interface[0] || strlen(options.interface) >= IF_NAMESIZE)) ||
        (options.wireless && !options.wired))
        return 2;
    bool has_software_args = options.mfi_cert || options.mfi_key || options.mfi_protocol;
    if ((options.mfi_mode == DEMO_MFI_SOFTWARE || (options.mfi_mode == DEMO_MFI_AUTO && has_software_args))
            ? !options.mfi_cert || !options.mfi_key || !options.mfi_protocol
            : has_software_args)
    {
        fprintf(stderr, "carplay: select software or auto and supply the MFi certificate, key and protocol together\n");
        return 2;
    }
#ifndef CARPLAY_BLUEZ
    if (options.wireless)
        return 2;
#endif
#ifndef CARPLAY_USB
    if (options.wired)
    {
        fprintf(stderr, "carplay: this build requires --lan IFACE\n");
        return 2;
    }
#endif
    umask(0077);
    int lock = lock_directory(options.directory);
    if (lock < 0)
    {
        int error = errno;
        fprintf(stderr, "carplay: cannot use state directory %s: %s\n", options.directory,
                error == EWOULDBLOCK ? "state lock in use by another instance" : strerror(error));
#ifdef CARPLAY_LVGL
        if (options.headless || (error != EROFS && error != EIO && error != ENOENT))
#endif
            return 1;
    }
    struct sigaction action = { .sa_handler = stop };
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    int result = run(&options, &lock);
    if (lock >= 0)
        close(lock);
    return result == RTCP_OK ? 0 : 1;
}

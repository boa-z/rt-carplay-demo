#define _GNU_SOURCE
#include "linux_demo.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <pthread.h>
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
#include "mdns_responder.h"
#include "mbedtls/platform_util.h"
#include "rtcp/version.h"
#include "rtcp_linux.h"

#ifdef CARPLAY_LVGL
#include "../../assets/rt-carplay-oem-png.h"

static const rtcp_oem_icon_t demo_oem_icon = {
    .png = carplay_demo_oem_icon_png,
    .png_len = sizeof(carplay_demo_oem_icon_png),
    .width_pixels = CARPLAY_DEMO_OEM_ICON_SIZE,
    .height_pixels = CARPLAY_DEMO_OEM_ICON_SIZE,
};
#endif

static const char demo_name[] = "Luban CarPlay";
static char active_interface[IF_NAMESIZE];
static struct rtcp_net network;
static pthread_mutex_t metadata_lock = PTHREAD_MUTEX_INITIALIZER;
static demo_track_t current_track;
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
    if (update->invalidated)
        current_track = (demo_track_t){ 0 };
    if (update->item.title)
        snprintf(current_track.title, sizeof(current_track.title), "%s", update->item.title);
    if (update->item.artist)
        snprintf(current_track.artist, sizeof(current_track.artist), "%s", update->item.artist);
    if (update->item.album)
        snprintf(current_track.album, sizeof(current_track.album), "%s", update->item.album);
    if (update->item.has_playing)
        current_track.playing = update->item.playing;
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

void demo_phone_snapshot(demo_track_t *track, demo_phone_status_t *phone, demo_route_t *route)
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

static int run(demo_options_t *options)
{
    rtcp_receiver_config_t config = { 0 };
    demo_identity_t identity = { 0 };
    rtcp_receiver_t *receiver = NULL;
    mdns_responder_t *mdns = NULL;
    int result = demo_identity_open(&identity, options->directory, &config.platform.pairing);
    if (result != RTCP_OK)
    {
        fprintf(stderr, "carplay: could not load identity and pair records (%d)\n", result);
        goto done;
    }
    if ((options->wired || options->wireless) && (result = demo_mfi_open(options->i2c)) != RTCP_OK)
    {
        fprintf(stderr, "carplay: MFi coprocessor unavailable on %s\n", options->i2c);
        goto done;
    }
    if (options->gnss && !demo_gnss_open(options->gnss))
        fprintf(stderr, "carplay: GNSS unavailable on %s\n", options->gnss);
#ifdef CARPLAY_BLUEZ
    if (options->wireless && (result = demo_wireless_configure(options, &identity)) != RTCP_OK)
    {
        fprintf(stderr, "carplay: invalid or unreadable AP configuration %s\n", options->ap_config);
        goto done;
    }
#endif

#ifdef CARPLAY_LVGL
    if (!options->headless && (result = demo_launcher_open(options)) != RTCP_OK)
        goto done;
#endif
#ifdef CARPLAY_AIC_MPP
    if (!options->headless && (result = demo_video_open(options, &config.media)) != RTCP_OK)
        goto done;
#endif
    bool playback = false, capture = false;
#ifdef CARPLAY_ALSA
    if (!options->headless)
        demo_audio_open(options, &config.media, &playback, &capture);
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
    if (!options->headless)
    {
        info.oem_icons = &demo_oem_icon;
        info.oem_icon_count = 1;
        info.oem_icon_label = "rt-carplay";
    }
#endif
    uint8_t plist[AIRPLAY_INFO_PLIST_MAX_BYTES];
    if (!airplay_info_plist_build(&info, plist, sizeof(plist), &config.platform.info_plist_len))
    {
        result = RTCP_EINVAL;
        goto done;
    }
    network = rtcp_net_linux;
    network.listen = listen_on_interface;
    network.udp_open = udp_on_interface;
    network.udp_join = join_on_interface;
    config.platform.net = &network;
    config.platform.os = &rtcp_os_linux;
    config.platform.info_plist = plist;
    config.port = options->port;
#ifdef CARPLAY_USB
    if (options->wired && (result = demo_wired_start(options, &identity)) != RTCP_OK)
        goto done;
#endif
    demo_endpoint_t active = { 0 };
    uint64_t retry_after = 0, advertise_after = 0;
    while (!demo_stop)
    {
        demo_endpoint_t endpoint = { 0 };
#ifdef CARPLAY_USB
        if (options->wired)
            demo_wired_snapshot(&endpoint);
#endif
        if (!endpoint.address[0] && (!options->wired || options->wireless) &&
            lan_address(options->interface, endpoint.address))
        {
            endpoint.generation = 0;
            snprintf(endpoint.interface, sizeof(endpoint.interface), "%s", options->interface);
        }

        if (receiver && (strcmp(active.interface, endpoint.interface) ||
                         strcmp(active.address, endpoint.address) ||
                         active.generation != endpoint.generation))
        {
#ifdef CARPLAY_BLUEZ
            if (options->wireless)
                demo_wireless_step(NULL, NULL, false);
#endif
            if (mdns)
                mdns_responder_close(mdns, &network);
            mdns_responder_destroy(mdns);
            mdns = NULL;
            rtcp_receiver_destroy(receiver);
            receiver = NULL;
            retry_after = 0;
            demo_track_update(&(rtcp_now_playing_details_t){ .invalidated = true }, NULL);
        }
        uint64_t now = rtcp_os_linux.now_ms();
        if (!receiver && endpoint.address[0] && now >= retry_after)
        {
            memcpy(active_interface, endpoint.interface, sizeof(active_interface));
            config.family = endpoint.generation ? RTCP_AF_INET6 : RTCP_AF_INET;
            config.iap2 = (rtcp_iap2_backend_t){ 0 };
#ifdef CARPLAY_BLUEZ
            if (options->wireless && !endpoint.generation)
                demo_wireless_backend(&config.iap2);
#endif
            result = rtcp_receiver_create(&config, &receiver);
            if (result == RTCP_OK)
                result = rtcp_receiver_start(receiver);
            if (result != RTCP_OK)
            {
                fprintf(stderr, "carplay: listen on %s failed (%d)\n", active_interface, result);
                rtcp_receiver_destroy(receiver);
                receiver = NULL;
                retry_after = now + 2000;
            }
            else
            {
                active = endpoint;
                rtcp_receiver_set_now_playing_details_callback(receiver, demo_track_update, NULL);
                fprintf(stderr, "carplay: RTSP ready on %s [%s]:%u\n", active.interface,
                        active.address, rtcp_receiver_port(receiver));
                advertise_after = 0;
            }
        }
        if (receiver && !active.generation && !mdns && now >= advertise_after)
        {
            mdns = advertise(&active, &identity, &info, rtcp_receiver_port(receiver));
            if (!mdns)
                fprintf(stderr, "carplay: mDNS unavailable on %s\n", active.interface);
            advertise_after = now + 2000;
        }
#ifdef CARPLAY_BLUEZ
        if (options->wireless)
            demo_wireless_step(receiver, active.address, mdns != NULL);
#endif
        rtcp_receiver_state_t state = { 0 };
        if (receiver)
        {
            rtcp_receiver_poll(receiver, 5);
            rtcp_receiver_get_state(receiver, &state);
        }
        else
            rtcp_os_linux.sleep_ms(10);
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
#endif
#ifdef CARPLAY_LVGL
        demo_launcher_step(receiver, projection);
#else
        (void)projection;
#endif
    }
    result = RTCP_OK;
done:
#ifdef CARPLAY_USB
    demo_wired_stop();
#endif
    if (mdns)
        mdns_responder_close(mdns, &network);
    mdns_responder_destroy(mdns);
    rtcp_receiver_destroy(receiver);
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
    demo_mfi_close();
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
        return 1;
    }
    struct sigaction action = { .sa_handler = stop };
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    int result = run(&options);
    close(lock);
    return result == RTCP_OK ? 0 : 1;
}

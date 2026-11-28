#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <fcntl.h>
#include <glob.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "carplay_ui_theme.h"
#include "connectivity_page.h"
#include "icons/carplay_icons.h"
#include "launcher_layout.h"
#include "pairing_files.h"
#include "rtcp_linux.h"
#include "rtcp/version.h"
#include "settings_navigation.h"
#include "system_shell.h"
#include "ui_language.h"
#ifdef CARPLAY_AIC_MPP
#include <video/artinchip_fb.h>
#include "fusion_dashboard.h"
#include "fusion_display.h"
#endif

LV_FONT_DECLARE(carplay_ui_17);
LV_FONT_DECLARE(carplay_ui_15);
LV_FONT_DECLARE(carplay_ui_18);
LV_FONT_DECLARE(carplay_ui_21);
LV_FONT_DECLARE(carplay_ui_34);
LV_FONT_DECLARE(carplay_cjk_full_18);
enum
{
    TOUCH_SLOTS = 16,
    TOUCH_LOCAL = 1,
    TOUCH_PHONE = 2,
    TOUCH_OUTSIDE = 3
};
typedef struct
{
    int x, y;
    bool active;
    unsigned owner;
    int output;
} touch_slot_t;

static struct
{
    const demo_options_t *options;
    int fb, touch;
    uint8_t *pixels, *draw;
    size_t map_size, stride, origin, bytes_per_pixel;
    uint16_t width, height;
    struct input_absinfo axis_x, axis_y;
    touch_slot_t contacts[TOUCH_SLOTS];
    int slot;
    bool multitouch, discard, pointer_down;
    lv_point_t pointer;
    rtcp_touch_contact_t report[RTCP_TOUCH_CONTACTS];
    lv_display_t *display;
    lv_obj_t *status, *clock, *pages[CARPLAY_SHELL_DESTINATIONS], *phone_details;
    lv_obj_t *theme_buttons[2], *display_buttons[2], *settings_status;
    lv_obj_t *settings_heading, *settings_pages[SETTINGS_PAGE_COUNT], *settings_tabs[SETTINGS_PAGE_COUNT];
    lv_obj_t *language_buttons[2], *volume_label, *volume_slider, *mic_caption;
    lv_obj_t *ethernet[2], *wifi[2], *clock_source[1];
    settings_page_t settings_route;
    uint8_t volume, mic_muted;
    lv_obj_t *appearance_buttons[3];
    lv_obj_t *fps_buttons[2];
    uint8_t phone_appearance;
    int sent_appearance;
    uint64_t appearance_retry;
    lv_obj_t *brightness_slider, *brightness_label;
    char brightness_path[256];
    unsigned brightness_max;
    uint8_t brightness;
    lv_obj_t *surface;
    carplay_phone_details_t details;
#ifdef CARPLAY_BLUEZ
    lv_obj_t *pair_panel, *pair_code;
#endif
    carplay_system_shell_t shell;
    carplay_dashboard_t dashboard;
    carplay_phone_hub_t phone_hub;
    carplay_connectivity_view_t connectivity;
    char theme_path[256], display_path[256];
    bool initialized, first_frame, projected, want_project, want_local, phone_requested_local, event_connected;
    uint64_t update_after, receiver_generation, touch_retry;
#ifdef CARPLAY_AIC_MPP
    struct aicfb_alpha_config original_alpha;
    struct aicfb_layer_data original_layer;
    lv_obj_t *fusion_panel;
    carplay_instruments_t fusion_instruments;
    uint32_t touch_epoch, retained_stream;
    uint64_t resume_until, display_retry;
    bool alpha_ready, fusion_ready, fusion_selected, fusion_active, screen_owner_changed;
    void (*audio_modes)(const rtcp_audio_modes_t *, void *);
#endif
} ui = { .fb = -1, .touch = -1 };

static uint32_t tick(void)
{
    return (uint32_t)rtcp_os_linux.now_ms();
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *source)
{
    size_t line = (size_t)(area->x2 - area->x1 + 1) * ui.bytes_per_pixel;
    uint8_t *destination = ui.pixels + ui.origin + (size_t)area->y1 * ui.stride +
                           (size_t)area->x1 * ui.bytes_per_pixel;
    for (int row = area->y1; row <= area->y2; ++row)
    {
        memcpy(destination, source, line);
        destination += ui.stride;
        source += line;
    }
    lv_display_flush_ready(display);
    if (!ui.first_frame && lv_display_flush_is_last(display))
    {
        ui.first_frame = true;
        demo_boot_mark("hmi-framebuffer");
    }
}

static void pointer(lv_indev_t *device, lv_indev_data_t *data)
{
    (void)device;
    data->point = ui.pointer;
    data->state = ui.pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static int coordinate(int raw, const struct input_absinfo *limits, int extent)
{
    if (raw <= limits->minimum)
        return 0;
    if (raw >= limits->maximum)
        return extent - 1;
    return (int)(((int64_t)raw - limits->minimum) * (extent - 1) /
                 ((int64_t)limits->maximum - limits->minimum));
}

static void dispatch(const demo_launcher_commands_t *commands)
{
    bool changed = false, pressed = false;
#ifdef CARPLAY_AIC_MPP
    carplay_fusion_snapshot_t layout;
    carplay_fusion_snapshot(tick(), &layout);
#endif
    for (unsigned index = 0; index < TOUCH_SLOTS; ++index)
    {
        touch_slot_t *slot = &ui.contacts[index];
        if (!slot->active)
        {
            if (slot->owner == TOUCH_PHONE)
            {
                ui.report[slot->output].down = false;
                changed = true;
            }
            slot->owner = 0;
            continue;
        }
        uint16_t x = (uint16_t)coordinate(slot->x, &ui.axis_x, ui.width);
        uint16_t y = (uint16_t)coordinate(slot->y, &ui.axis_y, ui.height);
        uint16_t mapped_x = x, mapped_y = y;
        bool phone = false, local = !ui.projected;
#ifdef CARPLAY_AIC_MPP
        local |= ui.fusion_active && x >= ui.width - layout.local_width;
        phone = ui.projected && layout.video_generation == layout.generation &&
                rtcp_display_map_point(&layout.video_rect, layout.logical_width, layout.logical_height,
                                       x, y, &mapped_x, &mapped_y);
#endif
        if (!slot->owner)
        {
            slot->owner = local ? TOUCH_LOCAL : TOUCH_OUTSIDE;
            if (phone)
            {
                for (unsigned output = 0; output < RTCP_TOUCH_CONTACTS; ++output)
                {
                    if (!ui.report[output].down)
                    {
                        slot->owner = TOUCH_PHONE;
                        slot->output = (int)output;
                        break;
                    }
                }
            }
        }
        if (slot->owner == TOUCH_PHONE)
        {
            rtcp_touch_contact_t *contact = &ui.report[slot->output];
            if (phone)
            {
                changed |= !contact->down || contact->x != mapped_x || contact->y != mapped_y;
                *contact = (rtcp_touch_contact_t){ .x = mapped_x, .y = mapped_y, .down = true };
            }
            else
            {
                contact->down = false;
                slot->owner = TOUCH_OUTSIDE;
                changed = true;
            }
        }
        if (slot->owner == TOUCH_LOCAL && !pressed && local)
        {
            pressed = true;
            ui.pointer.x = x;
            ui.pointer.y = y;
        }
    }
    ui.pointer_down = pressed;
    if (changed && ui.projected)
        commands->send_touch(ui.report, commands->context);
}

static void touch_events(const demo_launcher_commands_t *commands)
{
    if (ui.touch < 0)
        return;
    struct input_event event;
    for (unsigned batch = 0; batch < 128 && read(ui.touch, &event, sizeof(event)) == sizeof(event); ++batch)
    {
        if (event.type == EV_SYN && event.code == SYN_DROPPED)
        {
            ui.discard = true;
            memset(ui.contacts, 0, sizeof(ui.contacts));
            memset(ui.report, 0, sizeof(ui.report));
            ui.pointer_down = false;
            if (ui.projected)
                commands->send_touch(ui.report, commands->context);
        }
        else if (event.type == EV_SYN && event.code == SYN_REPORT)
        {
            ui.discard = false;
            dispatch(commands);
        }
        else if (!ui.discard && event.type == EV_ABS)
        {
            if (event.code == ABS_MT_SLOT)
                ui.slot = event.value >= 0 && event.value < TOUCH_SLOTS ? event.value : -1;
            if (ui.slot < 0)
                continue;
            touch_slot_t *slot = &ui.contacts[ui.slot];
            if (event.code == ABS_MT_TRACKING_ID)
                slot->active = event.value >= 0;
            if ((ui.multitouch && event.code == ABS_MT_POSITION_X) || (!ui.multitouch && event.code == ABS_X))
                slot->x = event.value;
            if ((ui.multitouch && event.code == ABS_MT_POSITION_Y) || (!ui.multitouch && event.code == ABS_Y))
                slot->y = event.value;
        }
        else if (!ui.discard && event.type == EV_KEY)
        {
            if (event.code == BTN_TOUCH && !ui.multitouch)
                ui.contacts[0].active = event.value != 0;
            if ((event.code == KEY_BACK || event.code == KEY_ESC) && event.value == 1)
                ui.want_local = true;
        }
    }
}

static int open_touch(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return -1;
    struct input_absinfo slots;
    ui.multitouch = ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slots) == 0;
    unsigned x = ui.multitouch ? ABS_MT_POSITION_X : ABS_X;
    unsigned y = ui.multitouch ? ABS_MT_POSITION_Y : ABS_Y;
    if (ioctl(fd, EVIOCGABS(x), &ui.axis_x) == 0 && ioctl(fd, EVIOCGABS(y), &ui.axis_y) == 0 &&
        ui.axis_x.maximum > ui.axis_x.minimum && ui.axis_y.maximum > ui.axis_y.minimum)
        return fd;
    close(fd);
    return -1;
}

static void show_page(unsigned id)
{
    if (id >= CARPLAY_SHELL_DESTINATIONS)
        return;
    lv_obj_add_flag(ui.phone_details, LV_OBJ_FLAG_HIDDEN);
    for (unsigned index = 0; index < CARPLAY_SHELL_DESTINATIONS; ++index)
    {
        if (index == id)
            lv_obj_remove_flag(ui.pages[index], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(ui.pages[index], LV_OBJ_FLAG_HIDDEN);
    }
    carplay_system_shell_select(&ui.shell, id);
}

static void select_page(lv_event_t *event)
{
    show_page((unsigned)(uintptr_t)lv_event_get_user_data(event));
}

static void show_phone_hub(lv_event_t *event)
{
    (void)event;
    show_page(1);
}

static void show_phone_details(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(ui.pages[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.phone_details, LV_OBJ_FLAG_HIDDEN);
}

static void theme(lv_event_t *event)
{
    uint8_t dark = (uintptr_t)lv_event_get_user_data(event) != 0;
    if (carplay_file_save(ui.theme_path, &dark, sizeof(dark)) != RTCP_OK)
    {
        carplay_ui_set_text(ui.settings_status, "Settings not saved.");
        fprintf(stderr, "carplay: could not save display appearance\n");
        return;
    }
    carplay_ui_set_dark(dark != 0);
    carplay_segments_select(ui.theme_buttons, 2, dark);
    carplay_ui_set_text(ui.settings_status, "Settings saved.");
}

static void connect_phone(lv_event_t *event)
{
    (void)event;
    if (ui.event_connected)
        ui.want_project = true;
#ifdef CARPLAY_BLUEZ
    else if (ui.options->wireless)
        demo_wireless_discoverable();
#endif
}

bool demo_launcher_show_local(void *context)
{
    (void)context;
    if (!ui.initialized)
        return false;
    ui.phone_requested_local = true;
    ui.want_local = true;
    fprintf(stderr, "carplay: phone requested host UI\n");
    return true;
}

static void show_home(lv_event_t *event)
{
    (void)event;
    show_page(0);
}

#ifdef CARPLAY_AIC_MPP
static void select_display(lv_event_t *event);
#endif

static void settings_select(lv_event_t *event)
{
    ui.settings_route = (settings_page_t)(uintptr_t)lv_event_get_user_data(event);
    carplay_ui_set_text(ui.settings_status, "");
    carplay_settings_show(ui.settings_heading, ui.settings_pages, ui.settings_tabs, ui.settings_route);
}

static void settings_back(lv_event_t *event)
{
    settings_page_t section = carplay_settings_section(ui.settings_route);
    if (section == ui.settings_route)
        show_home(event);
    else
    {
        ui.settings_route = section;
        carplay_settings_show(ui.settings_heading, ui.settings_pages, ui.settings_tabs, section);
    }
}

static uint8_t setting_load(const char *name, uint8_t fallback, uint8_t maximum)
{
    char path[256];
    uint8_t value;
    size_t length;
    if (snprintf(path, sizeof(path), "%s/%s", ui.options->directory, name) >= (int)sizeof(path) ||
        carplay_file_load(path, &value, sizeof(value), &length) != RTCP_OK || length != 1 || value > maximum)
        return fallback;
    return value;
}

static bool setting_save(const char *name, uint8_t value)
{
    char path[256];
    return snprintf(path, sizeof(path), "%s/%s", ui.options->directory, name) < (int)sizeof(path) &&
           carplay_file_save(path, &value, sizeof(value)) == RTCP_OK;
}

static void select_language(lv_event_t *event)
{
    bool chinese = lv_event_get_user_data(event) != NULL;
    carplay_ui_set_chinese(chinese);
    carplay_segments_select(ui.language_buttons, 2, chinese ? 0 : 1);
    setting_save("language", chinese);
}

static void phone_appearance(lv_event_t *event)
{
    ui.phone_appearance = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    carplay_segments_select(ui.appearance_buttons, 3, ui.phone_appearance);
    ui.appearance_retry = 0;
    setting_save("phone-appearance", ui.phone_appearance);
}

static void select_fps(lv_event_t *event)
{
    uint8_t high = lv_event_get_user_data(event) != NULL;
    bool saved = setting_save("high-fps", high);
    if (saved)
        carplay_segments_select(ui.fps_buttons, 2, high);
    carplay_ui_set_text(ui.settings_status, saved ? "Saved. Restart the receiver to apply." : "Settings not saved.");
}

static bool set_brightness(unsigned percent)
{
    if (!ui.brightness_max)
        return false;
    FILE *file = fopen(ui.brightness_path, "w");
    if (!file)
        return false;
    unsigned level = (percent * ui.brightness_max + 50) / 100;
    bool written = fprintf(file, "%u\n", level ? level : 1) > 0;
    return fclose(file) == 0 && written;
}

static void brightness_change(lv_event_t *event)
{
    unsigned value = lv_slider_get_value(ui.brightness_slider);
    lv_label_set_text_fmt(ui.brightness_label, "%u%%", value);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
        set_brightness(value);
    else if (lv_event_get_code(event) != LV_EVENT_PRESSED && value != ui.brightness && set_brightness(value))
    {
        ui.brightness = value;
        setting_save("brightness", ui.brightness);
    }
}

static void brightness_open(void)
{
    glob_t paths = { 0 };
    if (glob("/sys/class/backlight/*/max_brightness", 0, NULL, &paths) == 0 && paths.gl_pathc == 1)
    {
        const char *path = paths.gl_pathv[0];
        size_t directory = strlen(path) - strlen("max_brightness");
        FILE *file = fopen(path, "r");
        unsigned limit = 0;
        if (file)
        {
            if (fscanf(file, "%u", &limit) != 1)
                limit = 0;
            fclose(file);
        }
        if (limit && limit <= UINT16_MAX &&
            snprintf(ui.brightness_path, sizeof(ui.brightness_path), "%.*sbrightness", (int)directory, path) <
                (int)sizeof(ui.brightness_path) &&
            access(ui.brightness_path, W_OK) == 0)
            ui.brightness_max = limit;
    }
    globfree(&paths);
    ui.brightness = setting_load("brightness", 70, 100);
    if (ui.brightness < 5)
        ui.brightness = 5;
}

#ifdef CARPLAY_ALSA
static void volume_change(lv_event_t *event)
{
    unsigned value = lv_slider_get_value(ui.volume_slider);
    lv_label_set_text_fmt(ui.volume_label, "%u%%", value);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
        demo_audio_set_local_volume(value);
    else if (lv_event_get_code(event) != LV_EVENT_PRESSED && value != ui.volume)
    {
        ui.volume = value;
        setting_save("volume", ui.volume);
    }
}

static void mute_mic(lv_event_t *event)
{
    (void)event;
    ui.mic_muted = !ui.mic_muted;
    demo_audio_set_mic_muted(ui.mic_muted);
    carplay_ui_set_text(ui.mic_caption, ui.mic_muted ? "Unmute microphone" : "Mute microphone");
    setting_save("mic-muted", ui.mic_muted);
}
#endif

static void network_readout(const char *interface, lv_obj_t *labels[2])
{
    if (!interface || !labels[0])
        return;
    char address[INET_ADDRSTRLEN] = "";
    const char *link = "Disconnected";
    struct ifaddrs *interfaces;
    if (getifaddrs(&interfaces) == 0)
    {
        for (const struct ifaddrs *item = interfaces; item; item = item->ifa_next)
        {
            if (strcmp(interface, item->ifa_name))
                continue;
            if (item->ifa_flags & IFF_RUNNING)
                link = "Connected";
            if (item->ifa_addr && item->ifa_addr->sa_family == AF_INET)
                inet_ntop(AF_INET, &((const struct sockaddr_in *)item->ifa_addr)->sin_addr, address, sizeof(address));
        }
        freeifaddrs(interfaces);
    }
    carplay_ui_set_text(labels[0], link);
    carplay_ui_set_text(labels[1], *address ? address : "—");
}

static void build_settings(lv_obj_t *parent)
{
    unsigned routes = CARPLAY_SETTINGS_ROUTE(SETTINGS_CONNECTIONS) | CARPLAY_SETTINGS_ROUTE(SETTINGS_DISPLAY) |
                      CARPLAY_SETTINGS_ROUTE(SETTINGS_GENERAL) | CARPLAY_SETTINGS_ROUTE(SETTINGS_SYSTEM) |
                      CARPLAY_SETTINGS_ROUTE(SETTINGS_LANGUAGE) | CARPLAY_SETTINGS_ROUTE(SETTINGS_CLOCK) |
                      CARPLAY_SETTINGS_ROUTE(SETTINGS_ABOUT) | CARPLAY_SETTINGS_ROUTE(SETTINGS_ETHERNET) |
                      CARPLAY_SETTINGS_ROUTE(SETTINGS_DEVELOPER);
#ifdef CARPLAY_ALSA
    routes |= CARPLAY_SETTINGS_ROUTE(SETTINGS_SOUND);
#endif
#ifdef CARPLAY_BLUEZ
    if (ui.options->wireless)
        routes |= CARPLAY_SETTINGS_ROUTE(SETTINGS_WIFI) | CARPLAY_SETTINGS_ROUTE(SETTINGS_BLUETOOTH);
#endif
    carplay_settings_build(parent, &ui.settings_heading, ui.settings_pages, ui.settings_tabs,
                           routes, settings_select, settings_back, NULL);
    lv_obj_t *display = ui.settings_pages[SETTINGS_DISPLAY];
    carplay_launcher_label(display, "Screen", 6, 0, 260, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    carplay_settings_slider_build(display, 26, "Display brightness", NULL, &ui.brightness_label,
                                  &ui.brightness_slider, 5, 100, brightness_change, NULL);
    lv_slider_set_value(ui.brightness_slider, ui.brightness, LV_ANIM_OFF);
    if (ui.brightness_max)
        lv_label_set_text_fmt(ui.brightness_label, "%u%%", ui.brightness);
    else
    {
        lv_obj_add_state(ui.brightness_slider, LV_STATE_DISABLED);
        carplay_ui_set_text(ui.brightness_label, "Unavailable");
    }
    carplay_appearance_build(display, 123, ui.theme_buttons, theme);
    ui.settings_status = carplay_launcher_label(parent, "", CARPLAY_SETTINGS_DETAIL_X, 461,
                                                CARPLAY_SETTINGS_DETAIL_WIDTH, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    carplay_settings_fps_build(ui.settings_pages[SETTINGS_DEVELOPER], ui.fps_buttons, select_fps);
    carplay_segments_select(ui.fps_buttons, 2, ui.options->fps == 60);
    if (ui.options->fps_override)
    {
        lv_obj_add_state(ui.fps_buttons[0], LV_STATE_DISABLED);
        lv_obj_add_state(ui.fps_buttons[1], LV_STATE_DISABLED);
    }
    lv_obj_t *projection = carplay_projection_settings_build(display, ui.display_buttons,
#ifdef CARPLAY_AIC_MPP
                                                             select_display);
    if (!ui.fusion_ready)
        lv_obj_add_state(ui.display_buttons[1], LV_STATE_DISABLED);
    carplay_segments_select(ui.display_buttons, 2, ui.fusion_selected);
#else
                                                             NULL);
    lv_obj_add_state(ui.display_buttons[0], LV_STATE_DISABLED);
    lv_obj_add_state(ui.display_buttons[1], LV_STATE_DISABLED);
#endif
    carplay_settings_phone_appearance_build(projection, ui.appearance_buttons, phone_appearance);
    ui.phone_appearance = setting_load("phone-appearance", 0, RTCP_APPEARANCE_DARK);
    ui.sent_appearance = -1;
    carplay_segments_select(ui.appearance_buttons, 3, ui.phone_appearance);
    carplay_segments_select(ui.theme_buttons, 2, carplay_ui_dark());
    carplay_settings_language_build(ui.settings_pages[SETTINGS_LANGUAGE], ui.language_buttons, select_language);
    carplay_segments_select(ui.language_buttons, 2, carplay_ui_is_chinese() ? 0 : 1);
    const char * const captions[] = { "Platform", "rt-carplay", "Build hash" };
    const char * const values[] = { "Luban Linux", rtcp_version_string(), rtcp_build_git_sha() };
    carplay_settings_readout_build(ui.settings_pages[SETTINGS_ABOUT], captions, values, NULL, 3);
    const char * const clock_captions[] = { "Date & time" };
    const char * const clock_values[] = { "" };
    carplay_settings_readout_build(ui.settings_pages[SETTINGS_CLOCK], clock_captions, clock_values, ui.clock_source, 1);
    const char * const net_captions[] = { "Status", "IP address" };
    const char * const net_values[] = { "", "" };
    carplay_settings_readout_build(ui.settings_pages[SETTINGS_ETHERNET], net_captions, net_values, ui.ethernet, 2);
#ifdef CARPLAY_BLUEZ
    if (ui.options->wireless)
    {
        carplay_settings_readout_build(ui.settings_pages[SETTINGS_WIFI], net_captions, net_values, ui.wifi, 2);
        lv_obj_t *list = carplay_settings_list(ui.settings_pages[SETTINGS_BLUETOOTH],
                                               CARPLAY_SETTINGS_DETAIL_WIDTH - 4, LV_SIZE_CONTENT);
        carplay_settings_row(list, "Pair phone", &carplay_icon_smartphone_24, false, connect_phone, NULL);
    }
#endif
#ifdef CARPLAY_ALSA
    lv_obj_t *sound = ui.settings_pages[SETTINGS_SOUND];
    carplay_settings_slider_build(sound, 0, "Media volume", &carplay_icon_volume_29, &ui.volume_label,
                                  &ui.volume_slider, 0, 100, volume_change, "Adjust local audio output");
    ui.volume = setting_load("volume", 100, 100);
    demo_audio_set_local_volume(ui.volume);
    lv_slider_set_value(ui.volume_slider, ui.volume, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui.volume_label, "%u%%", ui.volume);
    lv_obj_t *audio = carplay_settings_list(sound, CARPLAY_SETTINGS_DETAIL_WIDTH - 4, LV_SIZE_CONTENT);
    lv_obj_set_y(audio, 142);
    ui.mic_muted = setting_load("mic-muted", 0, 1);
    demo_audio_set_mic_muted(ui.mic_muted);
    ui.mic_caption = lv_obj_get_child(carplay_settings_row(audio, ui.mic_muted ? "Unmute microphone" : "Mute microphone",
                                                           NULL, false, mute_mic, NULL),
                                      0);
#endif
    carplay_settings_show(ui.settings_heading, ui.settings_pages, ui.settings_tabs, SETTINGS_DISPLAY);
}

#ifdef CARPLAY_AIC_MPP
static void leave_projection(lv_event_t *event)
{
    (void)event;
    ui.want_local = true;
}

static void select_display(lv_event_t *event)
{
    uint8_t choice = lv_event_get_user_data(event) != NULL;
    if (carplay_file_save(ui.display_path, &choice, sizeof(choice)) != RTCP_OK)
    {
        carplay_ui_set_text(ui.settings_status, "Settings not saved.");
        return;
    }
    ui.fusion_selected = choice != 0;
    carplay_segments_select(ui.display_buttons, 2, choice);
    carplay_ui_set_text(ui.settings_status, "Settings saved.");
}

#endif

#ifdef CARPLAY_BLUEZ
static void confirm_pairing(lv_event_t *event)
{
    demo_wireless_confirm(lv_event_get_user_data(event) != NULL);
    lv_obj_add_flag(ui.pair_panel, LV_OBJ_FLAG_HIDDEN);
}
#endif

static void build_pages(void)
{
    lv_obj_t *screen = lv_screen_active();
    carplay_ui_style(screen, CARPLAY_STYLE_SCREEN);
    lv_obj_set_style_text_font(screen, &carplay_ui_17, 0);
    ui.surface = carplay_launcher_surface(screen, ui.width, ui.height);
    carplay_system_shell_build(&ui.shell, ui.surface, select_page);
    ui.status = carplay_launcher_label(ui.shell.status, "", 24, 9, 820,
                                       &carplay_ui_17, CARPLAY_STYLE_MUTED);
    ui.clock = carplay_launcher_label(ui.shell.status, "--:--", 916, 9, 76,
                                      &carplay_ui_17, CARPLAY_STYLE_MUTED);
    lv_obj_set_style_text_align(ui.clock, LV_TEXT_ALIGN_RIGHT, 0);
    for (unsigned index = 0; index < CARPLAY_SHELL_DESTINATIONS; ++index)
    {
        ui.pages[index] = lv_obj_create(ui.shell.content);
        lv_obj_remove_style_all(ui.pages[index]);
        lv_obj_remove_flag(ui.pages[index], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(ui.pages[index], lv_pct(100), lv_pct(100));
        if (index)
            lv_obj_add_flag(ui.pages[index], LV_OBJ_FLAG_HIDDEN);
    }
    carplay_dashboard_build(&ui.dashboard, ui.pages[0], show_phone_hub);
    lv_obj_add_flag(ui.dashboard.note, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(ui.dashboard.media, &carplay_cjk_full_18, 0);
    lv_obj_set_style_text_font(ui.dashboard.artist, &carplay_cjk_full_18, 0);
    carplay_phone_hub_build(&ui.phone_hub, ui.pages[1], show_phone_details);
    carplay_connectivity_build(&ui.connectivity, ui.pages[1], connect_phone);
#ifdef CARPLAY_AIC_MPP
    if (ui.fusion_ready)
    {
        ui.fusion_panel = carplay_fusion_strip_build(screen, ui.width, ui.height,
                                                     &ui.fusion_instruments, leave_projection);
        lv_obj_add_flag(ui.fusion_panel, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    ui.phone_details = carplay_launcher_panel(ui.shell.content, 0, 0, 1024, 484, 0, CARPLAY_STYLE_SCREEN);
    lv_obj_add_flag(ui.phone_details, LV_OBJ_FLAG_HIDDEN);
    carplay_phone_details_build(&ui.details, ui.phone_details, show_phone_hub);

    build_settings(ui.pages[2]);
#ifdef CARPLAY_BLUEZ
    if (ui.options->wireless)
    {
        ui.pair_panel = carplay_settings_list(screen, 0, 0);
        lv_obj_set_size(ui.pair_panel, lv_pct(65), LV_SIZE_CONTENT);
        lv_obj_center(ui.pair_panel);
        carplay_ui_style(ui.pair_panel, CARPLAY_STYLE_SCREEN);
        ui.pair_code = lv_label_create(ui.pair_panel);
        carplay_settings_row(ui.pair_panel, "Confirm matching code", &carplay_icon_smartphone_24,
                             false, confirm_pairing, ui.pair_panel);
        carplay_settings_row(ui.pair_panel, "Cancel pairing", &carplay_icon_smartphone_24,
                             false, confirm_pairing, NULL);
        lv_obj_add_flag(ui.pair_panel, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    carplay_system_shell_select(&ui.shell, 0);
}

static void refresh_phone(void)
{
    carplay_phone_track_t track;
    demo_phone_status_t phone;
    demo_route_t route;
    demo_phone_snapshot(&track, &phone, &route);
    carplay_ui_set_text(ui.dashboard.media, track.title[0] ? track.title : "No media");
    carplay_ui_set_text(ui.dashboard.artist, track.artist);
    carplay_ui_set_text(ui.dashboard.playback, track.title[0] && track.playing ? "Playing" : "");
    const char *direction = route.road[0] ? route.road : route.destination;
    char guidance[2 * sizeof(route.road) + sizeof(route.distance) + 8];
    snprintf(guidance, sizeof(guidance), "%s%s%s", *direction ? direction : "No active route",
             *direction && route.distance[0] ? " · " : "", *direction ? route.distance : "");
    carplay_ui_set_text(ui.dashboard.route, guidance);
    carplay_ui_set_text(ui.phone_hub.destination, route.destination[0] ? route.destination : route.road[0] ? "-"
                                                                                                           : "No active route");
    carplay_ui_set_text(ui.phone_hub.road, route.road);
    carplay_ui_set_text(ui.phone_hub.turn_distance, route.distance);
    carplay_phone_hub_track(&ui.phone_hub, &track);
    static const char * const charging[] = { "Not charging", "Charging", "Charged" };
    const char *power = phone.has_charging && phone.charging < 3 ? charging[phone.charging] : "";
    if (phone.has_battery)
        lv_label_set_text_fmt(ui.details.power, "%u%%%s%s", (unsigned)phone.battery, *power ? "  /  " : "", power);
    else
        carplay_ui_set_text(ui.details.power, "Battery unavailable");
    const char *carrier = phone.carrier[0] ? phone.carrier : "Cellular";
    if (phone.airplane)
        carplay_ui_set_text(ui.details.cellular, "Airplane mode");
    else if (phone.has_signal)
        lv_label_set_text_fmt(ui.details.cellular, "%s  /  %s %u", carrier, carplay_ui_translate("Signal"), (unsigned)phone.signal);
    else
        carplay_ui_set_text(ui.details.cellular, phone.carrier[0] ? carrier : "Cellular unavailable");
    static const char * const calls[] = {
        "No active call", "Dialing", "Ringing", "Connecting call", "Call active", "Call on hold", "Ending call"
    };
    carplay_ui_set_text(ui.details.call, phone.has_call && phone.call < sizeof(calls) / sizeof(calls[0]) ? calls[phone.call] : "Call status unavailable");
}

static bool set_projection(bool enabled, bool fusion)
{
#ifdef CARPLAY_AIC_MPP
    if (ui.alpha_ready)
    {
        struct aicfb_layer_data layer = ui.original_layer;
        if (ui.fusion_ready && (fusion || ui.fusion_active))
        {
            if (fusion)
            {
                layer.buf.crop_en = 1;
                layer.buf.crop.x = ui.width - ui.width / 4;
                layer.buf.crop.width = ui.width / 4;
                layer.buf.crop.height = ui.height;
                layer.pos.x = layer.buf.crop.x;
            }
            if (ioctl(ui.fb, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0)
                return false;
        }
        struct aicfb_alpha_config alpha = ui.original_alpha;
        if (enabled && !fusion)
        {
            alpha.enable = 1;
            alpha.mode = AICFB_GLOBAL_ALPHA_MODE;
            alpha.value = 0;
        }
        if (ioctl(ui.fb, AICFB_UPDATE_ALPHA_CONFIG, &alpha) == 0)
            return true;
        if (ui.fusion_ready && (fusion || ui.fusion_active))
        {
            layer.buf.crop_en = ui.fusion_active;
            layer.buf.crop.x = ui.fusion_active ? ui.width - ui.width / 4 : 0;
            layer.buf.crop.width = ui.fusion_active ? ui.width / 4 : 0;
            layer.buf.crop.height = ui.fusion_active ? ui.height : 0;
            layer.pos.x = layer.buf.crop.x;
            ioctl(ui.fb, AICFB_UPDATE_LAYER_CONFIG, &layer);
        }
        return false;
    }
#else
    (void)fusion;
#endif
    return !enabled;
}

static void attach_touch(void)
{
    if (strcmp(ui.options->touch, "auto"))
        ui.touch = open_touch(ui.options->touch);
    else
    {
        glob_t paths = { 0 };
        if (glob("/dev/input/event*", 0, NULL, &paths) == 0)
        {
            for (size_t index = 0; index < paths.gl_pathc && ui.touch < 0; ++index)
                ui.touch = open_touch(paths.gl_pathv[index]);
        }
        globfree(&paths);
    }
    if (ui.touch >= 0)
        demo_boot_mark("hmi-touch");
}

int demo_launcher_open(demo_options_t *options)
{
    ui.options = options;
    ui.fb = open(options->framebuffer, O_RDWR | O_CLOEXEC);
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    if (ui.fb < 0 || ioctl(ui.fb, FBIOGET_FSCREENINFO, &fixed) < 0 ||
        ioctl(ui.fb, FBIOGET_VSCREENINFO, &variable) < 0 || !variable.xres || !variable.yres ||
        variable.xres > UINT16_MAX || variable.yres > UINT16_MAX)
        return RTCP_EIO;
    lv_color_format_t format;
    if (variable.bits_per_pixel == 16 && variable.red.offset == 11 && variable.green.offset == 5 &&
        variable.blue.offset == 0)
        format = LV_COLOR_FORMAT_RGB565;
    else if (variable.bits_per_pixel == 32 && variable.red.offset == 16 && variable.green.offset == 8 &&
             variable.blue.offset == 0)
        format = LV_COLOR_FORMAT_XRGB8888;
    else
        return RTCP_ENOTSUP;
    ui.width = options->width = (uint16_t)variable.xres;
    ui.height = options->height = (uint16_t)variable.yres;
    ui.bytes_per_pixel = variable.bits_per_pixel / 8;
    ui.stride = fixed.line_length;
    ui.origin = (size_t)variable.yoffset * ui.stride + variable.xoffset * ui.bytes_per_pixel;
    ui.map_size = fixed.smem_len;
    if ((variable.xoffset + (size_t)ui.width) * ui.bytes_per_pixel > ui.stride ||
        ui.origin + (ui.height - 1U) * ui.stride + ui.width * ui.bytes_per_pixel > ui.map_size)
        return RTCP_EINVAL;
    void *mapping = mmap(NULL, ui.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ui.fb, 0);
    if (mapping == MAP_FAILED)
        return RTCP_EIO;
    ui.pixels = mapping;
    size_t size = ui.width * ui.bytes_per_pixel * 32;
    ui.draw = malloc(size);
    if (!ui.draw)
        return RTCP_ENOMEM;
#ifdef CARPLAY_AIC_MPP
    ui.original_alpha.layer_id = AICFB_LAYER_TYPE_UI;
    ui.alpha_ready = ioctl(ui.fb, AICFB_GET_ALPHA_CONFIG, &ui.original_alpha) == 0;
    ui.original_layer.layer_id = AICFB_LAYER_TYPE_UI;
    ui.fusion_ready = ui.alpha_ready && ui.width >= 512 && ui.width % 16 == 0 &&
                      ioctl(ui.fb, AICFB_GET_LAYER_CONFIG, &ui.original_layer) == 0 && ui.original_layer.enable &&
                      !ui.original_layer.buf.crop_en && !ui.original_layer.pos.x && !ui.original_layer.pos.y &&
                      ui.original_layer.buf.size.width == (int)ui.width && ui.original_layer.buf.size.height == (int)ui.height;
#endif
    if (snprintf(ui.theme_path, sizeof(ui.theme_path), "%s/theme", options->directory) >= (int)sizeof(ui.theme_path) ||
        snprintf(ui.display_path, sizeof(ui.display_path), "%s/display-mode", options->directory) >= (int)sizeof(ui.display_path))
        return RTCP_EINVAL;
    lv_init();
    ui.initialized = true;
    lv_tick_set_cb(tick);
    ui.display = lv_display_create(ui.width, ui.height);
    lv_display_set_color_format(ui.display, format);
    lv_display_set_buffers(ui.display, ui.draw, NULL, (uint32_t)size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(ui.display, flush);
    lv_indev_t *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, pointer);
    uint8_t dark = 1;
    size_t length;
    int loaded = carplay_file_load(ui.theme_path, &dark, sizeof(dark), &length);
    if (loaded != RTCP_OK || length > 1 || (length == 1 && dark > 1))
    {
        fprintf(stderr, "carplay: invalid display appearance setting, using dark theme\n");
        dark = 1;
    }
    carplay_ui_set_dark(dark == 1);
    carplay_ui_set_chinese(setting_load("language", 0, 1) != 0);
    if (!options->fps_override)
        options->fps = setting_load("high-fps", options->fps == 60, 1) ? 60 : 30;
#ifdef CARPLAY_AIC_MPP
    uint8_t choice = 1;
    loaded = carplay_file_load(ui.display_path, &choice, sizeof(choice), &length);
    if (loaded != RTCP_OK || length > 1 || choice > 1)
    {
        fprintf(stderr, "carplay: invalid projection layout setting, using the default\n");
        choice = 1;
    }
    ui.fusion_selected = ui.fusion_ready && choice != 0;
#endif
    brightness_open();
    build_pages();
    lv_refr_now(ui.display);
    set_brightness(ui.brightness);
    attach_touch();
    return RTCP_OK;
}

static void release_touches(const demo_launcher_commands_t *commands)
{
    memset(ui.report, 0, sizeof(ui.report));
    if (ui.projected && commands)
        commands->send_touch(ui.report, commands->context);
    for (unsigned index = 0; index < TOUCH_SLOTS; ++index)
        ui.contacts[index].owner = ui.contacts[index].active ? TOUCH_OUTSIDE : 0;
    ui.pointer_down = false;
}

#ifdef CARPLAY_AIC_MPP
static void on_audio_modes(const rtcp_audio_modes_t *modes, void *context)
{
    ui.screen_owner_changed |= modes->has_screen;
    if (ui.audio_modes)
        ui.audio_modes(modes, context);
}
#endif

void demo_launcher_attach_media(rtcp_media_backend_t *media)
{
#ifdef CARPLAY_AIC_MPP
    ui.audio_modes = media->on_audio_modes;
    media->on_audio_modes = on_audio_modes;
    ui.fusion_ready &= media->on_video != NULL;
#else
    (void)media;
#endif
}

uint16_t demo_launcher_projection_width(void)
{
#ifdef CARPLAY_AIC_MPP
    if (ui.fusion_ready && ui.fusion_selected)
        return ui.width - ui.width / 4;
#endif
    return ui.width;
}

void demo_launcher_apply_layout(void)
{
#ifdef CARPLAY_AIC_MPP
    carplay_fusion_set_mode(ui.fusion_ready && ui.fusion_selected ? CARPLAY_FUSION : CARPLAY_FULLSCREEN);
#endif
}

void demo_launcher_step(const demo_launcher_commands_t *commands,
                        const demo_launcher_snapshot_t *snapshot)
{
    if (!ui.initialized)
        return;
    const rtcp_receiver_state_t *state = &snapshot->receiver;
    bool replaced = ui.receiver_generation != snapshot->receiver_generation;
    ui.receiver_generation = snapshot->receiver_generation;
    uint64_t now = rtcp_os_linux.now_ms();
    if (ui.touch < 0 && now >= ui.touch_retry)
    {
        attach_touch();
        ui.touch_retry = now + 1000;
    }
    if (ui.event_connected && (replaced || !state->input_ready))
    {
        ui.want_local = ui.want_project = ui.phone_requested_local = false;
#ifdef CARPLAY_AIC_MPP
        ui.retained_stream = 0;
        ui.resume_until = 0;
        carplay_fusion_set_projection_hidden(false);
#endif
    }
    ui.event_connected = state->input_ready;
    if (replaced || !state->input_ready)
    {
        ui.sent_appearance = -1;
        ui.appearance_retry = 0;
    }
    if (state->input_ready && ui.sent_appearance != ui.phone_appearance && now >= ui.appearance_retry)
    {
        if (commands->set_appearance((rtcp_appearance_setting_t)ui.phone_appearance, commands->context) == RTCP_OK)
            ui.sent_appearance = ui.phone_appearance;
        ui.appearance_retry = now + 1000;
    }
    if (ui.want_local)
    {
#ifdef CARPLAY_AIC_MPP
        ui.retained_stream = ui.projected ? snapshot->stream_generation : 0;
        ui.resume_until = 0;
        carplay_fusion_set_projection_hidden(true);
#endif
        if (!ui.phone_requested_local)
            fprintf(stderr, "carplay: projection pause rc=%d\n",
                    commands->set_projection(false, commands->context));
        ui.phone_requested_local = false;
        ui.want_local = false;
        show_page(0);
    }
    if (ui.want_project && state->input_ready)
    {
#ifdef CARPLAY_AIC_MPP
        ui.screen_owner_changed = false;
#endif
        int result = commands->set_projection(true, commands->context);
        fprintf(stderr, "carplay: projection resume rc=%d\n", result);
#ifdef CARPLAY_AIC_MPP
        if (result == RTCP_OK)
        {
            carplay_fusion_resume_projection_on_frame();
            ui.resume_until = now + 5000;
            commands->request_keyframe(commands->context);
        }
#endif
    }
    ui.want_project = false;
    bool next = snapshot->projection_visible;
    bool fusion = false, geometry_changed = false;
#ifdef CARPLAY_AIC_MPP
    if (ui.resume_until && ui.screen_owner_changed && snapshot->stream_generation &&
        snapshot->stream_generation == ui.retained_stream &&
        snapshot->projection_owner == RTCP_PROJECTION_PHONE &&
        carplay_fusion_resume_latched_projection())
        fprintf(stderr, "carplay: projection resumed from retained frame\n");
    ui.screen_owner_changed = false;
    carplay_fusion_snapshot_t layout;
    carplay_fusion_snapshot(tick(), &layout);
    next &= layout.effective != CARPLAY_LOCAL && layout.video_generation == layout.generation;
    fusion = next && layout.effective == CARPLAY_FUSION;
    if (next)
        ui.resume_until = 0;
    else if (ui.resume_until && now >= ui.resume_until)
    {
        ui.resume_until = 0;
        carplay_fusion_set_projection_hidden(true);
        fprintf(stderr, "carplay: projection resume timed out waiting for a frame\n");
    }
    geometry_changed = ui.touch_epoch != layout.touch_epoch;
    ui.touch_epoch = layout.touch_epoch;
    bool fusion_changed = fusion != ui.fusion_active;
#else
    bool fusion_changed = false;
#endif
    if (replaced || geometry_changed || next != ui.projected || fusion_changed)
        release_touches(replaced ? NULL : commands);
    if (next != ui.projected || fusion_changed)
    {
        if (set_projection(next, fusion))
        {
            ui.projected = next;
            if (next)
                demo_boot_mark(snapshot->wired ? "wired-presentation" : ui.options->wireless ? "wireless-presentation"
                                                                                             : "lan-presentation");
            fprintf(stderr, "carplay: display %s\n", next ? (fusion ? "Fusion" : "Fullscreen") : "Dashboard");
#ifdef CARPLAY_AIC_MPP
            ui.fusion_active = fusion;
            if (ui.fusion_panel)
            {
                if (fusion)
                {
                    lv_obj_move_foreground(ui.fusion_panel);
                    lv_obj_remove_flag(ui.fusion_panel, LV_OBJ_FLAG_HIDDEN);
                }
                else
                    lv_obj_add_flag(ui.fusion_panel, LV_OBJ_FLAG_HIDDEN);
            }
#endif
        }
#ifdef CARPLAY_AIC_MPP
        else if (now >= ui.display_retry)
        {
            fprintf(stderr, "carplay: could not switch framebuffer presentation (projection=%u fusion=%u)\n",
                    next, fusion);
            ui.display_retry = now + 2000;
        }
#endif
    }
    touch_events(commands);
    if (now >= ui.update_after)
    {
        const char *name = ui.options->wireless ? "USB / Wi-Fi" : (ui.options->wired ? "USB" : "LAN");
        const char *status = state->input_ready ? "Connected" : (state->transport_connected ? "Starting CarPlay" : "Waiting for phone");
        lv_label_set_text_fmt(ui.status, "%s  ·  %s", name, status);
        const char *transport = snapshot->wired ? "iPhone · USB" : ui.options->wireless ? "iPhone · Wireless"
                                                                                        : "LAN";
        carplay_ui_set_text(ui.details.caption, "Phone status");
        carplay_ui_set_text(ui.details.transport, state->input_ready ? transport : "Not connected");
        const carplay_connectivity_snapshot_t connectivity = {
            .state = status,
            .transport = state->transport_connected ? transport : "",
            .availability = ui.options->wireless ? "USB and Wi-Fi" : name,
            .resume = state->input_ready,
            .actionable = (state->input_ready && !ui.projected) ||
                          (!state->transport_connected && ui.options->wireless),
        };
        carplay_connectivity_update(&ui.connectivity, &connectivity);
        carplay_ui_set_text(ui.dashboard.connection, status);
        time_t seconds = time(NULL);
        struct tm local;
        char clock[8];
        if (localtime_r(&seconds, &local) && strftime(clock, sizeof(clock), "%H:%M", &local))
        {
            carplay_ui_set_text(ui.clock, clock);
            char date[64];
            if (strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S %Z", &local))
            {
                carplay_ui_set_text(ui.clock_source[0], date);
                carplay_ui_set_text(ui.details.clock, date);
            }
        }
        if (!lv_obj_has_flag(ui.pages[2], LV_OBJ_FLAG_HIDDEN))
        {
            if (ui.settings_route == SETTINGS_ETHERNET)
                network_readout("eth0", ui.ethernet);
            else if (ui.settings_route == SETTINGS_WIFI)
                network_readout(ui.options->interface, ui.wifi);
        }
#ifdef CARPLAY_AIC_MPP
        carplay_instruments_update(&ui.dashboard.instruments, layout.vehicle.available,
                                   layout.vehicle.speed_kph, layout.vehicle.battery_percent);
        if (ui.fusion_panel)
            carplay_instruments_update(&ui.fusion_instruments, layout.vehicle.available,
                                       layout.vehicle.speed_kph, layout.vehicle.battery_percent);
#endif
        refresh_phone();
#ifdef CARPLAY_BLUEZ
        if (ui.pair_panel)
        {
            int passkey = demo_wireless_passkey();
            if (passkey >= 0 && !ui.projected)
            {
                lv_label_set_text_fmt(ui.pair_code, "Bluetooth pairing code: %06d", passkey);
                lv_obj_remove_flag(ui.pair_panel, LV_OBJ_FLAG_HIDDEN);
            }
            else
                lv_obj_add_flag(ui.pair_panel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
        ui.update_after = now + 250;
    }
    lv_timer_handler();
}

void demo_launcher_close(void)
{
    if (ui.projected)
        set_projection(false, false);
    if (ui.initialized)
        lv_deinit();
    if (ui.pixels)
        munmap(ui.pixels, ui.map_size);
    free(ui.draw);
    if (ui.touch >= 0)
        close(ui.touch);
    if (ui.fb >= 0)
        close(ui.fb);
}

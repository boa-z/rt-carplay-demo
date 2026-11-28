#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <fcntl.h>
#include <glob.h>
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
#include "icons/carplay_icons.h"
#include "launcher_layout.h"
#include "pairing_files.h"
#include "rtcp_linux.h"
#include "settings_navigation.h"
#include "system_shell.h"
#include "ui_language.h"
#ifdef CARPLAY_AIC_MPP
#include <video/artinchip_fb.h>
#include "fusion_dashboard.h"
#include "fusion_display.h"
#endif

LV_FONT_DECLARE(carplay_ui_17);
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
    lv_obj_t *status, *clock, *pages[CARPLAY_SHELL_DESTINATIONS], *project_button;
    lv_obj_t *connection, *connection_transport, *project_label, *phone_details;
    lv_obj_t *theme_buttons[2], *display_buttons[2], *settings_status;
    lv_obj_t *surface;
    lv_obj_t *power, *cellular, *call;
#ifdef CARPLAY_BLUEZ
    lv_obj_t *pair_panel, *pair_code;
#endif
    carplay_system_shell_t shell;
    carplay_dashboard_t dashboard;
    carplay_phone_hub_t phone_hub;
    char theme_path[256], display_path[256];
    bool initialized, projected, want_project, want_local, phone_requested_local, event_connected;
    uint64_t update_after;
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

static void dispatch(rtcp_receiver_t *receiver)
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
    if (changed && receiver && ui.projected)
        rtcp_receiver_send_touch(receiver, ui.report);
}

static void touch_events(rtcp_receiver_t *receiver)
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
            if (receiver && ui.projected)
                rtcp_receiver_send_touch(receiver, ui.report);
        }
        else if (event.type == EV_SYN && event.code == SYN_REPORT)
        {
            ui.discard = false;
            dispatch(receiver);
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

static bool host_ui(void *context)
{
    (void)context;
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
    lv_obj_add_flag(ui.phone_hub.progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *connection = carplay_connection_card_build(ui.pages[1], &ui.connection, &ui.connection_transport);
    ui.project_button = carplay_connection_action(connection, connect_phone, &ui.project_label);
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
    lv_obj_t *details_back = carplay_settings_row(ui.phone_details, "Phone Hub", &carplay_icon_arrow_left_22,
                                                  false, show_phone_hub, NULL);
    lv_obj_set_size(details_back, 176, 56);
    lv_obj_set_pos(details_back, 24, 16);
    carplay_launcher_label(ui.phone_details, "Phone details", 220, 24, 720, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    lv_obj_t *details = carplay_launcher_panel(ui.phone_details, 24, 88, 976, 160, 12, CARPLAY_STYLE_CARD);
    ui.power = carplay_launcher_label(details, "", 24, 20, 928, &carplay_ui_18, CARPLAY_STYLE_TEXT);
    ui.cellular = carplay_launcher_label(details, "", 24, 66, 928, &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
    ui.call = carplay_launcher_label(details, "", 24, 112, 928, &carplay_ui_18, CARPLAY_STYLE_TEXT);

    lv_obj_t *heading;
    lv_obj_t *tabs = carplay_settings_frame_build(ui.pages[2], &heading, show_home);
    carplay_ui_set_text(heading, "Display");
    lv_obj_t *display = carplay_settings_tab(tabs, "Display", &carplay_icon_sun_29, select_page,
                                             (void *)(uintptr_t)2);
    carplay_settings_tab_select(display, true);
    lv_obj_t *settings = carplay_settings_detail_create(ui.pages[2]);
    carplay_appearance_build(settings, 123, ui.theme_buttons, theme);
    ui.settings_status = carplay_launcher_label(settings, "", 6, 100, CARPLAY_SETTINGS_DETAIL_WIDTH - 12,
                                                &carplay_ui_17, CARPLAY_STYLE_MUTED);
#ifdef CARPLAY_AIC_MPP
    (void)carplay_projection_settings_build(settings, ui.display_buttons, select_display);
    if (!ui.fusion_ready)
        lv_obj_add_state(ui.display_buttons[1], LV_STATE_DISABLED);
    carplay_segments_select(ui.display_buttons, 2, ui.fusion_selected);
#endif
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
    demo_track_t track;
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
    char artist[sizeof(track.artist) + sizeof(track.album) + 3];
    snprintf(artist, sizeof(artist), "%s%s%s", track.artist,
             track.artist[0] && track.album[0] ? " / " : "", track.album);
    carplay_ui_set_text(ui.phone_hub.title, track.title[0] ? track.title : "-");
    carplay_ui_set_text(ui.phone_hub.artist, artist);
    carplay_ui_set_text(ui.phone_hub.playback, track.title[0] && track.playing ? "Playing" : "");
    static const char * const charging[] = { "Not charging", "Charging", "Charged" };
    const char *power = phone.has_charging && phone.charging < 3 ? charging[phone.charging] : "";
    if (phone.has_battery)
        lv_label_set_text_fmt(ui.power, "Battery %u%%%s%s", (unsigned)phone.battery, *power ? "  /  " : "", power);
    else
        lv_label_set_text(ui.power, "Battery unavailable");
    const char *carrier = phone.carrier[0] ? phone.carrier : "Cellular";
    if (phone.airplane)
        lv_label_set_text(ui.cellular, "Airplane mode");
    else if (phone.has_signal)
        lv_label_set_text_fmt(ui.cellular, "%s  /  Signal %u", carrier, (unsigned)phone.signal);
    else
        lv_label_set_text(ui.cellular, phone.carrier[0] ? carrier : "Cellular unavailable");
    static const char * const calls[] = {
        "No active call", "Dialing", "Ringing", "Connecting call", "Call active", "Call on hold", "Ending call"
    };
    lv_label_set_text(ui.call, phone.has_call && phone.call < sizeof(calls) / sizeof(calls[0]) ? calls[phone.call] : "Call status unavailable");
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
    if (strcmp(options->touch, "auto"))
        ui.touch = open_touch(options->touch);
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
    if (ui.touch < 0)
        fprintf(stderr, "carplay: no evdev touchscreen found\n");
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
    build_pages();
    return RTCP_OK;
}

static void release_touches(rtcp_receiver_t *receiver)
{
    memset(ui.report, 0, sizeof(ui.report));
    if (ui.projected && receiver)
        rtcp_receiver_send_touch(receiver, ui.report);
    for (unsigned index = 0; index < TOUCH_SLOTS; ++index)
        ui.contacts[index].owner = ui.contacts[index].active ? TOUCH_OUTSIDE : 0;
    ui.pointer_down = false;
}

void demo_launcher_bind(rtcp_receiver_t *receiver)
{
    if (!ui.initialized)
        return;
    if (receiver)
        rtcp_receiver_set_host_ui_callback(receiver, host_ui, NULL);
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
    carplay_fusion_set_mode(ui.fusion_selected ? CARPLAY_FUSION : CARPLAY_FULLSCREEN);
#else
    (void)media;
#endif
}

void demo_launcher_step(rtcp_receiver_t *receiver, const rtcp_receiver_state_t *state,
                        bool wired, bool projection)
{
    if (!ui.initialized)
        return;
    uint64_t now = rtcp_os_linux.now_ms();
#ifdef CARPLAY_AIC_MPP
    uint32_t stream = rtcp_receiver_get_projection_stream_generation(receiver);
    if (ui.event_connected && !state->input_ready)
    {
        ui.want_local = ui.want_project = ui.phone_requested_local = false;
        ui.retained_stream = 0;
        ui.resume_until = 0;
        carplay_fusion_set_mode(ui.fusion_selected ? CARPLAY_FUSION : CARPLAY_FULLSCREEN);
        carplay_fusion_set_projection_hidden(false);
    }
#endif
    ui.event_connected = state->input_ready;
    if (ui.want_local)
    {
#ifdef CARPLAY_AIC_MPP
        ui.retained_stream = ui.projected ? stream : 0;
        ui.resume_until = 0;
        carplay_fusion_set_projection_hidden(true);
#endif
        if (!ui.phone_requested_local)
            fprintf(stderr, "carplay: projection pause rc=%d\n",
                    rtcp_receiver_set_projection_enabled(receiver, false));
        ui.phone_requested_local = false;
        ui.want_local = false;
        show_page(0);
    }
    if (ui.want_project && state->input_ready)
    {
#ifdef CARPLAY_AIC_MPP
        ui.screen_owner_changed = false;
#endif
        int result = rtcp_receiver_set_projection_enabled(receiver, true);
        fprintf(stderr, "carplay: projection resume rc=%d\n", result);
#ifdef CARPLAY_AIC_MPP
        if (result == RTCP_OK)
        {
            carplay_fusion_set_mode(ui.fusion_selected ? CARPLAY_FUSION : CARPLAY_FULLSCREEN);
            carplay_fusion_resume_projection_on_frame();
            ui.resume_until = now + 5000;
            rtcp_receiver_request_screen_keyframe(receiver);
        }
#endif
    }
    ui.want_project = false;
    bool next = projection;
    bool fusion = false, geometry_changed = false;
#ifdef CARPLAY_AIC_MPP
    if (ui.resume_until && ui.screen_owner_changed && stream && stream == ui.retained_stream &&
        rtcp_receiver_get_projection_owner(receiver) == RTCP_PROJECTION_PHONE &&
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
    if (geometry_changed || next != ui.projected || fusion_changed)
        release_touches(receiver);
    if (next != ui.projected || fusion_changed)
    {
        if (set_projection(next, fusion))
        {
            ui.projected = next;
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
    touch_events(receiver);
    if (now >= ui.update_after)
    {
        const char *name = ui.options->wireless ? "USB / Wi-Fi" : (ui.options->wired ? "USB" : "LAN");
        const char *status = state->input_ready ? "Connected" : (state->transport_connected ? "Starting CarPlay" : "Waiting for phone");
        lv_label_set_text_fmt(ui.status, "%s  ·  %s", name, status);
        carplay_ui_set_text(ui.connection, status);
        const char *transport = wired ? "iPhone · USB" : ui.options->wireless ? "iPhone · Wireless"
                                                                              : "LAN";
        carplay_ui_set_text(ui.connection_transport, state->transport_connected ? transport : ui.options->wireless ? "Available over USB and Wi-Fi"
                                                                                                                   : "Available over USB");
        carplay_ui_set_text(ui.dashboard.connection, status);
        time_t seconds = time(NULL);
        struct tm local;
        char clock[8];
        if (localtime_r(&seconds, &local) && strftime(clock, sizeof(clock), "%H:%M", &local))
            carplay_ui_set_text(ui.clock, clock);
        carplay_ui_set_text(ui.project_label, state->input_ready ? "Resume CarPlay" : "Connect phone");
        if ((state->input_ready && !ui.projected) ||
            (!state->transport_connected && ui.options->wireless))
            lv_obj_remove_state(ui.project_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(ui.project_button, LV_STATE_DISABLED);
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

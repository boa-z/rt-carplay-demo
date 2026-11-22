/* SPDX-License-Identifier: GPL-3.0-or-later */
/* V4 native pages: Dashboard, Connectivity, Settings, System Update.
 * Presentation only. Connection policy lives in app/; theme, language,
 * volume, backlight, display mode and OTA state come from their real owners. */
#include <rtthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lvgl.h"
#include "connection_manager.h"
#include "internet_share_lubanlite.h"
#include "carplay_app.h"
#include "video_sink_lubanlite.h"
#include "video_layer_policy.h"
#include "fusion_dashboard.h"
#include "touch_panel_lubanlite.h"
#include "rtcp/rtcp.h"
#include "rtcp/input.h"
#include "ui_language.h"
#include "launcher_preferences.h"
#include "carplay_ui_theme.h"
#include "icons/carplay_icons.h"
#include "audio_volume_lubanlite.h"
#include "backlight_lubanlite.h"
#include "carplay_build_info.h"
#if defined(RTCARPLAY_IAP2)
#include "gnss_demo.h"
#endif

static void set_phone_text(lv_obj_t *label, const char *value)
{
    if (strcmp(lv_label_get_text(label), value) != 0)
        lv_label_set_text(label, value);
}

#define lv_label_set_text     carplay_ui_set_text
#define lv_label_set_text_fmt carplay_ui_set_text_fmt
#define TR                    carplay_ui_translate
#if defined(RTCARPLAY_OTA)
#include "ota_lubanlite.h"
#endif
#if defined(RT_USING_RTC)
#include <drivers/rtc.h>
#endif

LV_FONT_DECLARE(carplay_display_220);
LV_FONT_DECLARE(carplay_display_110);
LV_FONT_DECLARE(carplay_ui_34);
LV_FONT_DECLARE(carplay_ui_21);
LV_FONT_DECLARE(carplay_ui_18);
LV_FONT_DECLARE(carplay_ui_17);
LV_FONT_DECLARE(carplay_ui_15);
LV_FONT_DECLARE(carplay_ui_14);

typedef enum
{
    PAGE_DASHBOARD,
    PAGE_CONNECTIVITY,
    PAGE_SETTINGS,
    PAGE_OTA,
    PAGE_COUNT
} page_t;

typedef enum
{
    SETTINGS_DISPLAY,
    SETTINGS_SOUND,
    SETTINGS_LANGUAGE,
    SETTINGS_ABOUT,
    SETTINGS_PHONE,
#if defined(RTCARPLAY_IAP2)
    SETTINGS_GNSS,
#endif
    SETTINGS_PAGE_COUNT
} settings_page_t;

enum
{
    SETTINGS_DETAIL_WIDTH = 632,
    SETTINGS_CONTENT_TOP = 100,
    SETTINGS_DETAIL_HEIGHT = 432,
    SETTINGS_SIDEBAR_WIDTH = 252,
    SETTINGS_NAV_ROW_STEP = 60
};

static rt_mutex_t pointer_lock;
typedef struct
{
    uint16_t x, y;
    bool down;
} demo_pointer_t;
static demo_pointer_t pointer_queue[32], pointer_state;
static unsigned pointer_head, pointer_tail;
static bool pointer_active;
static uint16_t pointer_id;

static lv_obj_t *pages[PAGE_COUNT];
static lv_obj_t *modal_scrim;

/* Dashboard */
static lv_obj_t *dash_dot, *dash_status, *dash_clock, *dash_note;
static lv_obj_t *dash_speed, *dash_soc, *dash_bar;

/* Connectivity */
static lv_obj_t *conn_dot, *conn_state, *conn_action;

/* Settings */
static lv_obj_t *volume_value, *volume_slider, *brightness_value, *brightness_slider;
static lv_obj_t *theme_buttons[3], *language_buttons[2], *display_buttons[2];
static lv_obj_t *phone_appearance_buttons[3];
static lv_obj_t *phone_title, *phone_artist, *phone_playback;
static lv_obj_t *phone_road, *phone_destination, *phone_remaining, *phone_route_caption;
LV_FONT_DECLARE(carplay_cjk_full_18);
#if defined(RTCARPLAY_IAP2)
static lv_obj_t *gnss_latitude, *gnss_longitude, *gnss_keyboard, *gnss_state, *gnss_counts;
static lv_obj_t *gnss_end_latitude, *gnss_end_longitude, *gnss_speed;
static lv_obj_t *gnss_buttons[2], *gnss_mode_buttons[2], *gnss_apply_button;
static bool gnss_moving;
#endif
static lv_obj_t *display_note, *settings_hint;
static lv_obj_t *settings_pages[SETTINGS_PAGE_COUNT];
static lv_obj_t *settings_tabs[SETTINGS_PAGE_COUNT];
static lv_obj_t *settings_captions[SETTINGS_PAGE_COUNT];
static bool volume_dragging, brightness_dragging;
static unsigned theme_choice, display_choice;

/* OTA */
static lv_obj_t *ota_version, *ota_detail, *ota_state, *ota_progress;
static lv_obj_t *ota_check_button, *ota_install_button, *ota_backend;
#if defined(RTCARPLAY_OTA)
static lv_obj_t *ota_confirm_panel, *ota_confirm_text;
static char ota_confirm_commit[41];
#endif
static bool network_ready;

/* Pairing */
static lv_obj_t *bt_panel, *bt_text, *bt_accept;
static uint32_t visible_pairing_request;

static void update_page(lv_timer_t *timer);

/* ---------------- primitives ---------------- */

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                       carplay_style_role_t role)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    carplay_ui_style(obj, role);
    return obj;
}

/* Top of the text box is chosen so the alphabetic baseline lands on the
 * reference coordinate: top = baseline - (line_height - base_line). */
static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int top, int w,
                      const lv_font_t *font, carplay_style_role_t role)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_line_space(obj, 0, 0);
    carplay_ui_style(obj, role);
    lv_obj_set_pos(obj, x, top);
    if (w > 0)
    {
        lv_obj_set_width(obj, w);
        lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    }
    if (font == &carplay_cjk_full_18)
        set_phone_text(obj, value);
    else
        lv_label_set_text(obj, value);
    return obj;
}

/* Icons are official Lucide glyphs converted to A8 masks by
 * tools/generate_icon_assets.py (docs/FONT_ICON_GUIDE.md asks for ready-made
 * single-colour icons, not hand-drawn vectors). An lv_image draws the mask
 * through IMAGE_RECOLOR, so the theme role recolours it live. */
static lv_obj_t *icon_mask(lv_obj_t *parent, const lv_image_dsc_t *mask, int x, int y,
                           carplay_style_role_t role)
{
    lv_obj_t *obj = lv_image_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(obj, mask);
    lv_obj_set_pos(obj, x, y);
    carplay_ui_style(obj, role);
    return obj;
}

static lv_obj_t *stroke(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                        carplay_style_role_t role)
{
    return panel(parent, x, y, w, h, radius, role);
}

static lv_obj_t *divider(lv_obj_t *parent, int x, int y, int w, int h)
{
    return panel(parent, x, y, w, h, 0, CARPLAY_STYLE_LINE_BORDER);
}

static lv_obj_t *button_text(lv_obj_t *parent, const char *caption, int x, int y,
                             int w, int h, int radius, carplay_style_role_t role,
                             carplay_style_role_t text_role, const lv_font_t *font,
                             lv_event_cb_t callback, void *user)
{
    lv_obj_t *obj = panel(parent, x, y, w, h, radius, role);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *caption_obj = text(obj, caption, 0, 0, 0, font, text_role);
    lv_obj_center(caption_obj);
    if (callback)
        lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, user);
    return obj;
}

static void set_text_if_changed(lv_obj_t *label, const char *value)
{
    if (!label || !strcmp(lv_label_get_text(label), value))
        return;
    lv_label_set_text(label, value);
}

/* ---------------- page navigation ---------------- */

static void show_page(page_t target)
{
    for (unsigned i = 0; i < PAGE_COUNT; ++i)
    {
        if (!pages[i])
            continue;
        if (i == (unsigned)target)
            lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void carplay_launcher_show_dashboard(void)
{
    show_page(PAGE_DASHBOARD);
}

static void open_connectivity(lv_event_t *event)
{
    (void)event;
    show_page(PAGE_CONNECTIVITY);
}
static void open_settings(lv_event_t *event)
{
    (void)event;
    show_page(PAGE_SETTINGS);
}
static void open_ota(lv_event_t *event)
{
    (void)event;
    show_page(PAGE_OTA);
}
static void open_dashboard(lv_event_t *event)
{
    (void)event;
    show_page(PAGE_DASHBOARD);
}

/* ---------------- touch plumbing (unchanged contract) ---------------- */

/* Called only by the existing GT911 reader. No LVGL calls here. */
bool carplay_demo_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y)
{
#if !defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (carplay_launcher_video_is_active() &&
        carplay_video_sink_is_running() && carplay_app_is_running())
    {
        if (pointer_lock)
        {
            (void)rt_mutex_take(pointer_lock, RT_WAITING_FOREVER);
            pointer_head = pointer_tail = 0;
            pointer_state.down = false;
            pointer_active = false;
            (void)rt_mutex_release(pointer_lock);
        }
        return false;
    }
#endif
    if (!pointer_lock)
        return true;
    (void)rt_mutex_take(pointer_lock, RT_WAITING_FOREVER);
    if (!pointer_active && phase == RTCP_TOUCH_DOWN)
    {
        pointer_active = true;
        pointer_id = id;
    }
    if (!pointer_active || pointer_id != id)
    {
        (void)rt_mutex_release(pointer_lock);
        return true;
    }
    if (phase == RTCP_TOUCH_UP)
        pointer_active = false;
    demo_pointer_t point = { x, y, phase != RTCP_TOUCH_UP };
    unsigned next = (pointer_head + 1U) % 32U;
    if (next == pointer_tail)
    {
        /* Retain the latest state, including release, if GUI is delayed. */
        pointer_tail = pointer_head;
    }
    pointer_queue[pointer_head] = point;
    pointer_head = next;
    (void)rt_mutex_release(pointer_lock);
    return true;
}

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static void dispatch_fusion_touch(carplay_fusion_touch_t action)
{
    if (action.owner == CARPLAY_TOUCH_PHONE)
        (void)carplay_connection_touch(action.phase, action.id, action.x, action.y);
    else if (action.owner == CARPLAY_TOUCH_LOCAL)
        (void)carplay_demo_touch(action.phase, action.id, action.x, action.y);
}
#endif

static void launcher_panel_touch(uint8_t phase, uint16_t id, uint16_t x,
                                 uint16_t y, void *context)
{
    (void)context;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    dispatch_fusion_touch(carplay_fusion_touch(phase, id, x, y));
#else
    if (!carplay_demo_touch(phase, id, x, y))
    {
        (void)carplay_connection_touch(phase, id, x, y);
    }
#endif
}

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    (void)rt_mutex_take(pointer_lock, RT_WAITING_FOREVER);
    if (pointer_tail != pointer_head)
    {
        pointer_state = pointer_queue[pointer_tail];
        pointer_tail = (pointer_tail + 1U) % 32U;
    }
    data->point.x = pointer_state.x;
    data->point.y = pointer_state.y;
    data->state = pointer_state.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = pointer_tail != pointer_head;
    (void)rt_mutex_release(pointer_lock);
}

static void update_scrim(void)
{
    if (!modal_scrim)
        return;
    lv_obj_t *modals[] = { bt_panel,
#if defined(RTCARPLAY_OTA)
                           ota_confirm_panel,
#endif
                           NULL };
    bool visible = false;
    for (unsigned i = 0; i < sizeof(modals) / sizeof(modals[0]); ++i)
        visible |= modals[i] && !lv_obj_has_flag(modals[i], LV_OBJ_FLAG_HIDDEN);
    if (visible)
        lv_obj_remove_flag(modal_scrim, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(modal_scrim, LV_OBJ_FLAG_HIDDEN);
}

static void show_modal(lv_obj_t *modal)
{
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(modal);
    update_scrim();
}

/* ---------------- dashboard ---------------- */

static void dashboard_sample(uint16_t *speed, uint8_t *soc, bool *valid, bool *simulated)
{
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_fusion_snapshot_t snapshot;
    carplay_fusion_snapshot(rt_tick_get_millisecond(), &snapshot);
    *speed = snapshot.vehicle.speed_kph;
    *soc = snapshot.vehicle.battery_percent;
    *valid = snapshot.vehicle.available;
    *simulated = snapshot.vehicle.simulated;
#else
    /* No vehicle provider in this profile; the V4 reference sample is shown
     * and labelled as demo data rather than implied to be measured. */
    *speed = 26;
    *soc = 78;
    *valid = true;
    *simulated = true;
#endif
}

static void clock_text(char *out, size_t size)
{
#if defined(RT_USING_RTC)
    rt_device_t device = rt_device_find("rtc");
    time_t seconds = 0;
    if (!device || rt_device_control(device, RT_DEVICE_CTRL_RTC_GET_TIME, &seconds) != RT_EOK ||
        seconds <= 0)
    {
        snprintf(out, size, "--:--");
        return;
    }
#if defined(RT_LIBC_DEFAULT_TIMEZONE)
    seconds += (time_t)RT_LIBC_DEFAULT_TIMEZONE * 3600;
#endif
    long rem = (long)(seconds % 86400);
    snprintf(out, size, "%02ld:%02ld", rem / 3600, rem / 60 % 60);
#else
    snprintf(out, size, "--:--");
#endif
}

/* Dashboard header geometry, from svg/01-dashboard_*.svg: the clock's right
 * edge is the anchor at x = 964 and the state dot and label sit at x = 824/847.
 * The reference's own 14 px English label is wider than the space left of the
 * time (the reference PNG shows them overlapping), so the cluster keeps the
 * design anchors whenever the measured label fits and otherwise slides left
 * only as far as the current clock ink requires. */
#define DASH_CLOCK_RIGHT  964
#define DASH_STATUS_X     847
#define DASH_STATUS_GAP   16 /* minimum clear space before the clock ink */
#define DASH_STATUS_MIN_X 560
#define DASH_DOT_SIZE     11
#define DASH_DOT_GAP      12

static int32_t label_width(lv_obj_t *label)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
    const char *value = lv_label_get_text(label);
    return lv_text_get_width(value, strlen(value), font, 0);
}

/* Places the dot and the status label from their rendered widths. Called on
 * every content or language change; writes only when the geometry moved. */
static void layout_dashboard_status(void)
{
    if (!dash_status || !dash_clock || !dash_dot)
        return;
    int32_t width = label_width(dash_status) + 2; /* rounding slack, no clipping */
    int32_t limit = DASH_CLOCK_RIGHT - label_width(dash_clock) - DASH_STATUS_GAP;
    int32_t left = DASH_STATUS_X;
    if (left + width > limit)
        left = limit - width;
    if (left < DASH_STATUS_MIN_X)
        left = DASH_STATUS_MIN_X;
    int32_t dot_x = left - DASH_DOT_GAP - DASH_DOT_SIZE;
    if (lv_obj_get_x(dash_status) != left || lv_obj_get_width(dash_status) != width)
    {
        lv_obj_set_pos(dash_status, left, 64);
        lv_obj_set_width(dash_status, width);
    }
    if (lv_obj_get_x(dash_dot) != dot_x)
        lv_obj_set_pos(dash_dot, dot_x, 63);
}

static void build_dashboard(lv_obj_t *parent)
{
    text(parent, "Dashboard", 64, 54, 520, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    dash_dot = stroke(parent, 824, 63, 11, 11, LV_RADIUS_CIRCLE, CARPLAY_STYLE_DOT_OFF);
    /* The clock is right-aligned against the reference anchor x = 964; it owns
     * that slot and the state dot plus label are placed to its left. */
    dash_clock = text(parent, "--:--", DASH_CLOCK_RIGHT - 84, 62, 84,
                      &carplay_ui_17, CARPLAY_STYLE_MUTED);
    lv_obj_set_style_text_align(dash_clock, LV_TEXT_ALIGN_RIGHT, 0);
    dash_status = text(parent, "Not connected", DASH_STATUS_X, 64, 210, &carplay_ui_14,
                       CARPLAY_STYLE_MUTED);
    layout_dashboard_status();
    dash_note = text(parent, "Demo values", 64, 92, 300, &carplay_ui_14, CARPLAY_STYLE_FAINT);

    text(parent, "Speed", 70, 160, 300, &carplay_ui_21, CARPLAY_STYLE_MUTED);
    dash_speed = text(parent, "--", 62, 236, 380, &carplay_display_220, CARPLAY_STYLE_TEXT);
    text(parent, "km/h", 473, 353, 160, &carplay_ui_34, CARPLAY_STYLE_MUTED);

    divider(parent, 649, 152, 1, 316);
    text(parent, "Battery", 711, 160, 260, &carplay_ui_21, CARPLAY_STYLE_MUTED);
    dash_soc = text(parent, "--", 699, 247, 240, &carplay_display_110, CARPLAY_STYLE_TEXT);
    text(parent, "%", 880, 301, 80, &carplay_ui_34, CARPLAY_STYLE_MUTED);
    dash_bar = lv_bar_create(parent);
    lv_obj_remove_flag(dash_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(dash_bar, 718, 367);
    lv_obj_set_size(dash_bar, 224, 8);
    lv_bar_set_range(dash_bar, 0, 100);
    lv_obj_set_style_radius(dash_bar, 4, 0);
    lv_obj_set_style_radius(dash_bar, 4, LV_PART_INDICATOR);
    carplay_ui_style_part(dash_bar, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(dash_bar, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);

    divider(parent, 64, 485, 896, 1);

    lv_obj_t *connectivity = panel(parent, 64, 505, 437, 67, 14, CARPLAY_STYLE_ACCENT_CARD);
    lv_obj_add_flag(connectivity, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(connectivity, open_connectivity, LV_EVENT_CLICKED, NULL);
    icon_mask(connectivity, &carplay_icon_smartphone_24, 19, 22, CARPLAY_STYLE_ICON_ACCENT);
    lv_obj_t *connectivity_text = text(connectivity, "Connectivity", 0, 0, 0,
                                       &carplay_ui_18, CARPLAY_STYLE_ACCENT_TEXT);
    lv_obj_center(connectivity_text);

    lv_obj_t *settings = panel(parent, 523, 505, 437, 67, 14, CARPLAY_STYLE_CARD);
    lv_obj_add_flag(settings, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings, open_settings, LV_EVENT_CLICKED, NULL);
    icon_mask(settings, &carplay_icon_settings_24, 19, 21, CARPLAY_STYLE_ICON_TEXT);
    lv_obj_t *settings_text = text(settings, "Settings", 0, 0, 0, &carplay_ui_18,
                                   CARPLAY_STYLE_TEXT);
    lv_obj_center(settings_text);
}

/* Header shared by the three inner pages. */
static void build_header(lv_obj_t *parent, const char *title, lv_event_cb_t go_back)
{
    lv_obj_t *back = panel(parent, 54, 32, 44, 44, 13, CARPLAY_STYLE_CARD_ALT);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, go_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = icon_mask(back, &carplay_icon_arrow_left_22, 0, 0,
                               CARPLAY_STYLE_ICON_TEXT);
    lv_obj_center(arrow);
    lv_obj_t *heading = text(parent, title, 0, 0, 700, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    lv_obj_align_to(heading, back, LV_ALIGN_OUT_RIGHT_MID, 18, 0);
}

/* ---------------- connectivity ---------------- */

static void connect_action(lv_event_t *event)
{
    (void)event;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    demo_status_t status = { 0 };
    if (carplay_connection_status(&status) && status.stage == DEMO_LIVE &&
        !status.display_reconfiguring && !carplay_launcher_video_is_active())
    {
        /* A live session is held in LOCAL while the local pages are open. */
        carplay_fusion_ui_resume();
        return;
    }
#endif
    carplay_connection_retry();
}

static void build_connectivity(lv_obj_t *parent)
{
    build_header(parent, "Connectivity", open_dashboard);

    panel(parent, 64, 153, 896, 310, 24, CARPLAY_STYLE_CARD);
    panel(parent, 96, 183, 84, 84, 20, CARPLAY_STYLE_TILE);
    icon_mask(parent, &carplay_icon_smartphone_42, 117, 204, CARPLAY_STYLE_ICON_ACCENT);
    text(parent, "Apple CarPlay", 207, 198, 500, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    text(parent, "Wired / wireless", 208, 245, 500, &carplay_ui_18, CARPLAY_STYLE_MUTED);
    divider(parent, 96, 306, 830, 1);

    conn_dot = stroke(parent, 104, 342, 12, 12, LV_RADIUS_CIRCLE, CARPLAY_STYLE_DOT_OFF);
    conn_state = text(parent, "Waiting for phone", 131, 337, 520, &carplay_ui_21,
                      CARPLAY_STYLE_MUTED);
    conn_action = button_text(parent, "Connect phone", 674, 371, 252, 61, 14,
                              CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                              &carplay_ui_18, connect_action, NULL);
    icon_mask(parent, &carplay_icon_link_24, 693, 389, CARPLAY_STYLE_ICON_ACCENT);

    panel(parent, 64, 481, 896, 91, 17, CARPLAY_STYLE_CARD_ALT);
    icon_mask(parent, &carplay_icon_ellipsis_25, 88, 510, CARPLAY_STYLE_ICON_FAINT);
    text(parent, "Other protocols", 133, 502, 400, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    text(parent, "Android Auto  /  CarLife+", 133, 531, 560, &carplay_ui_17,
         CARPLAY_STYLE_TEXT);
    lv_obj_t *planned = text(parent, "Planned", 700, 518, 218, &carplay_ui_14,
                             CARPLAY_STYLE_FAINT);
    lv_obj_set_style_text_align(planned, LV_TEXT_ALIGN_RIGHT, 0);
    /* Informational only: no mock Android Auto or CarLife backend exists. */
}

/* ---------------- settings ---------------- */

static void settings_status(const char *english, bool error)
{
    if (!settings_hint)
        return;
    lv_label_set_text(settings_hint, english);
    carplay_ui_style(settings_hint, error ? CARPLAY_STYLE_WARN_TEXT : CARPLAY_STYLE_FAINT);
}

static void volume_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED)
    {
        volume_dragging = true;
        return;
    }
    unsigned value = (unsigned)lv_slider_get_value(volume_slider);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        volume_dragging = false;
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED)
        return;
    lv_label_set_text_fmt(volume_value, "%u%%", value);
    if (!carplay_volume_set(value))
    {
        settings_status("Not supported on this device", true);
        return;
    }
    if (code == LV_EVENT_RELEASED)
    {
        int result = carplay_preferences_set(CARPLAY_PREF_VOLUME, value);
        settings_status(result ? "Settings not saved." : "Settings saved.", result != 0);
    }
}

static void brightness_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED)
    {
        brightness_dragging = true;
        return;
    }
    unsigned value = (unsigned)lv_slider_get_value(brightness_slider);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        brightness_dragging = false;
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED)
        return;
    lv_label_set_text_fmt(brightness_value, "%u%%", value);
    if (!carplay_backlight_set(value))
    {
        settings_status("Not supported on this device", true);
        return;
    }
    if (code == LV_EVENT_RELEASED)
    {
        int result = carplay_preferences_set(CARPLAY_PREF_BRIGHTNESS, value);
        settings_status(result ? "Settings not saved." : "Settings saved.", result != 0);
    }
}

static void apply_segments(lv_obj_t * const *buttons, unsigned count, unsigned selected)
{
    for (unsigned i = 0; i < count; ++i)
    {
        if (!buttons[i])
            continue;
        carplay_ui_style(buttons[i], i == selected ? CARPLAY_STYLE_SEGMENT_ON
                                                   : CARPLAY_STYLE_SEGMENT);
        lv_obj_t *caption = lv_obj_get_child(buttons[i], 0);
        if (caption)
            carplay_ui_style(caption, i == selected ? CARPLAY_STYLE_SEGMENT_TEXT_ON
                                                    : CARPLAY_STYLE_SEGMENT_TEXT);
    }
}

static lv_obj_t *segment(lv_obj_t *parent, const char *caption, int x, int y, int w, int h,
                         lv_event_cb_t callback, void *user, const lv_font_t *font)
{
    lv_obj_t *obj = panel(parent, x, y, w, h, 9, CARPLAY_STYLE_SEGMENT);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, user);
    lv_obj_t *caption_obj = text(obj, caption, 0, 0, 0, font, CARPLAY_STYLE_SEGMENT_TEXT);
    lv_obj_center(caption_obj);
    return obj;
}

static void select_theme(lv_event_t *event)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (index > 1)
        return; /* Auto needs an ambient-light input this board does not have. */
    theme_choice = index;
    carplay_ui_set_dark(index == 1);
    int result = carplay_preferences_set(CARPLAY_PREF_THEME, index);
    apply_segments(theme_buttons, 3, theme_choice);
    settings_status(result ? "Settings not saved." : "Settings saved.", result != 0);
}

static void select_phone_appearance(lv_event_t *event)
{
    unsigned choice = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    carplay_app_set_phone_appearance((rtcp_appearance_setting_t)choice);
    apply_segments(phone_appearance_buttons, 3, choice);
    settings_status("Phone appearance requested", false);
}

static void select_language(lv_event_t *event)
{
    bool chinese = (uintptr_t)lv_event_get_user_data(event) != 0;
    carplay_ui_set_chinese(chinese);
    int result = carplay_preferences_set(CARPLAY_PREF_LANGUAGE, chinese ? 1U : 0U);
    apply_segments(language_buttons, 2, chinese ? 0U : 1U);
    settings_status(result ? "Settings not saved." : "Settings saved.", result != 0);
    update_page(NULL);
}

static void select_display(lv_event_t *event)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    display_choice = index;
    apply_segments(display_buttons, 2, display_choice);
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_display_mode_t mode = index ? CARPLAY_FUSION : CARPLAY_FULLSCREEN;
    if (!carplay_connection_set_display_mode(mode))
    {
        settings_status("Settings not saved.", true);
        return;
    }
    int result = carplay_preferences_error();
    settings_status(result ? "Settings not saved." : "Settings saved.", result != 0);
    lv_label_set_text(display_note, "");
#else
    /* This image has no single-screen compositor to switch. */
    settings_status("Not supported on this device", true);
#endif
}

typedef const lv_image_dsc_t *icon_asset_t;

static void build_slider_card(lv_obj_t *parent, int y, const char *title, icon_asset_t icon,
                              lv_obj_t **value_out, lv_obj_t **slider_out,
                              unsigned minimum, unsigned maximum,
                              lv_event_cb_t callback, const char *hint)
{
    lv_obj_t *card = panel(parent, 0, y, SETTINGS_DETAIL_WIDTH - 4, 144, 16,
                           CARPLAY_STYLE_CARD);
    icon_mask(card, icon, 23, 16, CARPLAY_STYLE_ICON_ACCENT);
    text(card, title, 68, 19, 380, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    *value_out = text(card, "", 460, 20, 142, &carplay_ui_18, CARPLAY_STYLE_TEXT);
    lv_obj_set_style_text_align(*value_out, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_pos(slider, 28, 65);
    lv_obj_set_size(slider, 568, 20);
    lv_slider_set_range(slider, (int32_t)minimum, (int32_t)maximum);
    lv_obj_set_style_pad_top(slider, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(slider, 6, LV_PART_MAIN);
    /* An 8 px track: the radius must fit its short side, otherwise LVGL's
     * indicator clipping path misplaces the mask and corrupts the layer. */
    lv_obj_set_style_radius(slider, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 4, LV_PART_INDICATOR);
    carplay_ui_style_part(slider, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(slider, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    carplay_ui_style_part(slider, CARPLAY_STYLE_SLIDER_KNOB, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_PRESS_LOST, NULL);
    *slider_out = slider;

    text(card, hint, 24, 107, 580, &carplay_ui_14, CARPLAY_STYLE_MUTED);
}

static void show_settings_page(settings_page_t target)
{
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
    {
        bool selected = index == (unsigned)target;
        if (selected)
            lv_obj_remove_flag(settings_pages[index], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(settings_pages[index], LV_OBJ_FLAG_HIDDEN);
        carplay_ui_style(settings_tabs[index], selected ? CARPLAY_STYLE_ACCENT_CARD
                                                        : CARPLAY_STYLE_CARD_ALT);
        carplay_ui_style(settings_captions[index], selected ? CARPLAY_STYLE_ACCENT_TEXT
                                                            : CARPLAY_STYLE_TEXT);
    }
}

static void select_settings_page(lv_event_t *event)
{
    settings_status("", false);
    show_settings_page((settings_page_t)(uintptr_t)lv_event_get_user_data(event));
}

static lv_obj_t *settings_nav_row(lv_obj_t *sidebar, const char *caption, icon_asset_t icon,
                                  unsigned index, lv_event_cb_t callback)
{
    lv_obj_t *row = panel(sidebar, 10, 10 + SETTINGS_NAV_ROW_STEP * (int)index,
                          SETTINGS_SIDEBAR_WIDTH - 22, 54, 12, CARPLAY_STYLE_CARD_ALT);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    lv_obj_t *image = icon_mask(row, icon, 16, 0, CARPLAY_STYLE_ICON_ACCENT);
    lv_obj_align(image, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_t *label = text(row, caption, 54, 0, 144, &carplay_ui_18, CARPLAY_STYLE_TEXT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 54, 0);
    return row;
}

static void build_display_settings(lv_obj_t *parent)
{
    build_slider_card(parent, 0, "Display brightness", &carplay_icon_sun_29,
                      &brightness_value, &brightness_slider, CARPLAY_PREF_BRIGHTNESS_MIN,
                      CARPLAY_PREF_BRIGHTNESS_MAX, brightness_event, "Adjust panel backlight");

    lv_obj_t *appearance = panel(parent, 0, 156, SETTINGS_DETAIL_WIDTH - 4, 90, 16,
                                 CARPLAY_STYLE_CARD);
    text(appearance, "Appearance", 24, 34, 230, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    panel(appearance, 301, 17, 304, 56, 11, CARPLAY_STYLE_SEGMENT);
    theme_buttons[0] = segment(appearance, "Light", 303, 20, 100, 50, select_theme, (void *)0,
                               &carplay_ui_15);
    theme_buttons[1] = segment(appearance, "Dark", 403, 20, 100, 50, select_theme, (void *)1,
                               &carplay_ui_15);
    theme_buttons[2] = segment(appearance, "Auto", 503, 20, 100, 50, select_theme, (void *)2,
                               &carplay_ui_15);
    lv_obj_add_state(theme_buttons[2], LV_STATE_DISABLED);

    lv_obj_t *display = panel(parent, 0, 258, SETTINGS_DETAIL_WIDTH - 4, 116, 16,
                              CARPLAY_STYLE_CARD);
    text(display, "CarPlay display", 24, 34, 230, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    panel(display, 301, 17, 304, 56, 11, CARPLAY_STYLE_SEGMENT);
    display_buttons[0] = segment(display, "Fullscreen", 303, 20, 150, 50, select_display,
                                 (void *)0, &carplay_ui_15);
    display_buttons[1] = segment(display, "Fusion", 453, 20, 150, 50, select_display,
                                 (void *)1, &carplay_ui_15);
    display_note = text(display, "", 24, 82, 580, &carplay_ui_14, CARPLAY_STYLE_FAINT);
}

static void build_language_settings(lv_obj_t *parent)
{
    lv_obj_t *card = panel(parent, 0, 0, SETTINGS_DETAIL_WIDTH - 4, 90, 16,
                           CARPLAY_STYLE_CARD);
    text(card, "Language", 24, 34, 230, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    panel(card, 301, 17, 304, 56, 11, CARPLAY_STYLE_SEGMENT);
    language_buttons[0] = segment(card, "简体中文", 303, 20, 150, 50, select_language,
                                  (void *)1, &carplay_ui_15);
    language_buttons[1] = segment(card, "English", 453, 20, 150, 50, select_language,
                                  (void *)0, &carplay_ui_15);
}

static void build_about(lv_obj_t *parent)
{
    const struct
    {
        const char *caption;
        const char *value;
    } items[] = {
        { "Luban-Lite SDK", carplay_build_info.sdk_version },
        { "rt-carplay", carplay_build_info.rtcarplay_version },
        { "Build hash", carplay_build_info.commit },
        { "Build time (UTC)", carplay_build_info.time_utc },
    };
    lv_obj_t *card = panel(parent, 0, 0, SETTINGS_DETAIL_WIDTH - 4, 348, 16,
                           CARPLAY_STYLE_CARD);
    for (unsigned index = 0; index < sizeof(items) / sizeof(items[0]); ++index)
    {
        int y = 12 + 84 * (int)index;
        if (index)
            divider(card, 24, y - 3, 580, 1);
        text(card, items[index].caption, 24, y + 9, 580, &carplay_ui_18, CARPLAY_STYLE_MUTED);
        lv_obj_t *value = text(card, items[index].value, 24, y + 37, 580,
                               &carplay_ui_17, CARPLAY_STYLE_TEXT);
        lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    }
}

static void build_phone_settings(lv_obj_t *parent)
{
    lv_obj_t *appearance = panel(parent, 0, 0, SETTINGS_DETAIL_WIDTH - 4, 102, 16, CARPLAY_STYLE_CARD);
    text(appearance, "Phone appearance", 24, 30, 255, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    text(appearance, "Auto uses daylight on this board", 24, 69, 550, &carplay_ui_14, CARPLAY_STYLE_FAINT);
    const char *choices[] = { "Auto", "Light", "Dark" };
    for (unsigned index = 0; index < 3; ++index)
        phone_appearance_buttons[index] = segment(appearance, choices[index], 303 + 100 * (int)index, 14,
                                                   100, 50, select_phone_appearance, (void *)(uintptr_t)index,
                                                   &carplay_ui_15);
    apply_segments(phone_appearance_buttons, 3, carplay_app_phone_appearance());

    lv_obj_t *media = panel(parent, 0, 114, SETTINGS_DETAIL_WIDTH - 4, 122, 16, CARPLAY_STYLE_CARD);
    text(media, "Now Playing", 24, 12, 370, &carplay_ui_18, CARPLAY_STYLE_MUTED);
    phone_playback = text(media, "", 417, 12, 185, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    phone_title = text(media, "-", 24, 42, 580, &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
    phone_artist = text(media, "", 24, 79, 580, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(phone_title, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(phone_artist, LV_LABEL_LONG_DOT);

    lv_obj_t *route = panel(parent, 0, 248, SETTINGS_DETAIL_WIDTH - 4, 126, 16, CARPLAY_STYLE_CARD);
    phone_route_caption = text(route, "", 24, 12, 370, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    phone_remaining = text(route, "", 417, 12, 185, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    phone_destination = text(route, "-", 24, 42, 580, &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
    phone_road = text(route, "", 24, 79, 580, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(phone_remaining, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(phone_destination, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(phone_road, LV_LABEL_LONG_DOT);
}

#if defined(RTCARPLAY_IAP2)
static void gnss_keyboard_close(lv_event_t *event)
{
    (void)event;
    lv_keyboard_set_textarea(gnss_keyboard, NULL);
    lv_obj_add_flag(gnss_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void gnss_edit(lv_event_t *event)
{
    lv_keyboard_set_textarea(gnss_keyboard, lv_event_get_target_obj(event));
    lv_obj_remove_flag(gnss_keyboard, LV_OBJ_FLAG_HIDDEN);
    settings_status("", false);
}

static void gnss_show_mode(bool moving)
{
    gnss_moving = moving;
    apply_segments(gnss_mode_buttons, 2, moving ? 1U : 0U);
    lv_obj_t *route_fields[] = { gnss_end_latitude, gnss_end_longitude, gnss_speed };
    for (unsigned index = 0; index < sizeof(route_fields) / sizeof(route_fields[0]); ++index)
    {
        if (moving)
            lv_obj_remove_state(route_fields[index], LV_STATE_DISABLED);
        else
            lv_obj_add_state(route_fields[index], LV_STATE_DISABLED);
    }
}

static void gnss_select_mode(lv_event_t *event)
{
    gnss_keyboard_close(NULL);
    gnss_show_mode(lv_event_get_user_data(event) != NULL);
    settings_status("", false);
}

static void gnss_apply(lv_event_t *event)
{
    unsigned mode = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    carplay_gnss_config_t config;
    if (!carplay_gnss_get_config(&config))
    {
        settings_status("GNSS settings busy", true);
        return;
    }
    if (mode != 0)
    {
        config.moving = gnss_moving;
        if (!carplay_gnss_parse(lv_textarea_get_text(gnss_latitude), 90, &config.latitude) ||
            !carplay_gnss_parse(lv_textarea_get_text(gnss_longitude), 180, &config.longitude) ||
            (config.moving &&
             (!carplay_gnss_parse(lv_textarea_get_text(gnss_end_latitude), 90, &config.end_latitude) ||
              !carplay_gnss_parse(lv_textarea_get_text(gnss_end_longitude), 180, &config.end_longitude))))
        {
            settings_status("Invalid coordinates", true);
            return;
        }
        if (config.moving)
        {
            const char *value = lv_textarea_get_text(gnss_speed);
            char *end;
            unsigned long speed = strtoul(value, &end, 10);
            if (end == value || *end || !speed || speed > CARPLAY_GNSS_MAX_SPEED_KMH)
            {
                settings_status("Speed must be 1 to 300 km/h", true);
                return;
            }
            config.speed_kmh = (unsigned)speed;
            if (!carplay_gnss_valid_config(&config))
            {
                settings_status("Choose a different destination", true);
                return;
            }
        }
    }
    if (mode < 2U)
        config.enabled = mode == 1U;
    if (!carplay_gnss_set_config(&config))
    {
        settings_status("GNSS settings busy", true);
        return;
    }
    gnss_keyboard_close(NULL);
    apply_segments(gnss_buttons, 2, config.enabled ? 1U : 0U);
    settings_status("", false);
}

static lv_obj_t *gnss_entry(lv_obj_t *parent, int x, int y, int width, const char *value,
                             bool coordinate)
{
    lv_obj_t *entry = lv_textarea_create(parent);
    lv_obj_set_pos(entry, x, y);
    lv_obj_set_size(entry, width, 40);
    lv_obj_set_style_radius(entry, 8, 0);
    lv_obj_set_style_text_font(entry, &carplay_ui_18, 0);
    carplay_ui_style(entry, CARPLAY_STYLE_INPUT);
    carplay_ui_style_part(entry, CARPLAY_STYLE_INPUT_CURSOR, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_opa(entry, LV_OPA_50, LV_STATE_DISABLED);
    lv_textarea_set_one_line(entry, true);
    lv_textarea_set_max_length(entry, coordinate ? 11 : 3);
    lv_textarea_set_accepted_chars(entry, coordinate ? "+-0123456789." : "0123456789");
    lv_textarea_set_text(entry, value);
    lv_obj_add_event_cb(entry, gnss_edit, LV_EVENT_CLICKED, NULL);
    return entry;
}

static lv_obj_t *gnss_coordinate(lv_obj_t *parent, int x, int y, int32_t initial)
{
    char value[16];
    carplay_gnss_format_coordinate(initial, value, sizeof(value));
    return gnss_entry(parent, x, y, 240, value, true);
}

static void build_gnss_settings(lv_obj_t *parent)
{
    lv_obj_t *card = panel(parent, 0, 0, SETTINGS_DETAIL_WIDTH - 4, 236, 16, CARPLAY_STYLE_CARD);
    gnss_mode_buttons[0] = segment(card, "Fixed point", 24, 12, 142, 38,
                                   gnss_select_mode, NULL, &carplay_ui_15);
    gnss_mode_buttons[1] = segment(card, "A to B", 172, 12, 142, 38,
                                   gnss_select_mode, (void *)1, &carplay_ui_15);
    text(card, "Latitude", 100, 58, 240, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    text(card, "Longitude", 358, 58, 240, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    text(card, "A", 32, 88, 40, &carplay_ui_17, CARPLAY_STYLE_TEXT);
    text(card, "B", 32, 138, 40, &carplay_ui_17, CARPLAY_STYLE_TEXT);
    carplay_gnss_config_t config = { 0 };
    (void)carplay_gnss_get_config(&config);
    gnss_latitude = gnss_coordinate(card, 100, 80, config.latitude);
    gnss_longitude = gnss_coordinate(card, 358, 80, config.longitude);
    gnss_end_latitude = gnss_coordinate(card, 100, 130, config.end_latitude);
    gnss_end_longitude = gnss_coordinate(card, 358, 130, config.end_longitude);
    text(card, "Speed (km/h)", 24, 193, 136, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    char speed[12];
    snprintf(speed, sizeof(speed), "%u", config.speed_kmh);
    gnss_speed = gnss_entry(card, 164, 182, 96, speed, false);
    gnss_buttons[0] = segment(card, "Off", 350, 182, 70, 40, gnss_apply, NULL, &carplay_ui_15);
    gnss_buttons[1] = segment(card, "On", 426, 182, 70, 40, gnss_apply, (void *)1, &carplay_ui_15);
    gnss_apply_button = segment(card, "Apply", 506, 182, 92, 40, gnss_apply, (void *)2, &carplay_ui_15);
    gnss_show_mode(config.moving);
    apply_segments(gnss_buttons, 2, config.enabled ? 1U : 0U);
    gnss_state = text(parent, "", 24, 244, 310, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    gnss_counts = text(parent, "", 350, 244, 248, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    gnss_keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(gnss_keyboard, SETTINGS_DETAIL_WIDTH - 4, 150);
    lv_obj_align(gnss_keyboard, LV_ALIGN_TOP_LEFT, 0, 274);
    lv_obj_set_style_radius(gnss_keyboard, 12, LV_PART_MAIN);
    carplay_ui_style(gnss_keyboard, CARPLAY_STYLE_CARD);
    carplay_ui_style_part(gnss_keyboard, CARPLAY_STYLE_INPUT, LV_PART_ITEMS);
    carplay_ui_style_part(gnss_keyboard, CARPLAY_STYLE_INPUT, LV_PART_ITEMS | LV_STATE_CHECKED);
    carplay_ui_style_part(gnss_keyboard, CARPLAY_STYLE_ACCENT_CARD, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_font(gnss_keyboard, LV_FONT_DEFAULT, LV_PART_ITEMS);
    lv_keyboard_set_mode(gnss_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_event_cb(gnss_keyboard, gnss_keyboard_close, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(gnss_keyboard, gnss_keyboard_close, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(gnss_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void update_gnss_settings(void)
{
    carplay_gnss_config_t config;
    if (!carplay_gnss_get_config(&config))
        return;
    carplay_gnss_status_t status;
    carplay_gnss_get_status(&status);
    const char *state = "Simulation off";
    if (config.enabled)
    {
        if (!status.tunnel)
            state = "Connect wirelessly to test";
        else if (!status.advertised)
            state = "Reconnect wireless CarPlay";
        else if (!status.requested)
            state = "Waiting for GNSS request";
        else if (config.moving && status.motion == CARPLAY_GNSS_ARRIVED)
            state = "Arrived at B";
        else if (config.moving && status.motion == CARPLAY_GNSS_MOVING)
            state = "Moving from A to B";
        else
            state = "Phone requested simulated GNSS";
    }
    if (strcmp(lv_label_get_text(gnss_state), TR(state)))
        lv_label_set_text(gnss_state, state);
    char counts[76];
    snprintf(counts, sizeof(counts), "GGA %u   RMC %u   %s %u", status.gga_queued, status.rmc_queued,
             TR("Failed"), status.failed);
    if (strcmp(lv_label_get_text(gnss_counts), counts))
        lv_label_set_text(gnss_counts, counts);
}
#endif

static void build_settings(lv_obj_t *parent)
{
    static const struct
    {
        const char *caption;
        icon_asset_t icon;
    } tabs[SETTINGS_PAGE_COUNT] = {
        [SETTINGS_DISPLAY] = { "Display", &carplay_icon_sun_29 },
        [SETTINGS_SOUND] = { "Sound", &carplay_icon_volume_29 },
        [SETTINGS_LANGUAGE] = { "Language", &carplay_icon_languages_24 },
        [SETTINGS_ABOUT] = { "About", &carplay_icon_info_26 },
        [SETTINGS_PHONE] = { "Phone controls", &carplay_icon_smartphone_24 },
#if defined(RTCARPLAY_IAP2)
        [SETTINGS_GNSS] = { "GNSS simulation", &carplay_icon_map_pin_24 },
#endif
    };
    build_header(parent, "Settings", open_dashboard);
    lv_obj_t *sidebar = panel(parent, 54, SETTINGS_CONTENT_TOP, SETTINGS_SIDEBAR_WIDTH,
                              SETTINGS_DETAIL_HEIGHT, 18, CARPLAY_STYLE_CARD);
    lv_obj_add_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(sidebar, LV_DIR_VER);
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
    {
        settings_tabs[index] = settings_nav_row(sidebar, tabs[index].caption, tabs[index].icon,
                                                index, select_settings_page);
        settings_captions[index] = lv_obj_get_child(settings_tabs[index], 1);
        lv_obj_t *page = lv_obj_create(parent);
        lv_obj_remove_style_all(page);
        lv_obj_set_pos(page, 338, SETTINGS_CONTENT_TOP);
        lv_obj_set_size(page, SETTINGS_DETAIL_WIDTH, SETTINGS_DETAIL_HEIGHT);
        lv_obj_set_scroll_dir(page, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
        settings_pages[index] = page;
    }
    lv_obj_t *update = settings_nav_row(sidebar, "System update", &carplay_icon_download_26,
                                        SETTINGS_PAGE_COUNT, open_ota);
    lv_obj_t *chevron = icon_mask(update, &carplay_icon_chevron_right_23, 0, 0,
                                 CARPLAY_STYLE_ICON_MUTED);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -11, 0);

    build_display_settings(settings_pages[SETTINGS_DISPLAY]);
    build_slider_card(settings_pages[SETTINGS_SOUND], 0, "Media volume", &carplay_icon_volume_29,
                      &volume_value, &volume_slider, 0, CARPLAY_PREF_VOLUME_MAX, volume_event,
                      "Adjust local audio output");
    build_language_settings(settings_pages[SETTINGS_LANGUAGE]);
    build_about(settings_pages[SETTINGS_ABOUT]);
    build_phone_settings(settings_pages[SETTINGS_PHONE]);
#if defined(RTCARPLAY_IAP2)
    build_gnss_settings(settings_pages[SETTINGS_GNSS]);
#endif
    settings_hint = text(parent, "", 338, 548, 632, &carplay_ui_14, CARPLAY_STYLE_FAINT);
    show_settings_page(SETTINGS_DISPLAY);
}

/* ---------------- OTA ---------------- */

#if defined(RTCARPLAY_OTA)
static void ota_check_now(lv_event_t *event)
{
    (void)event;
    (void)carplay_ota_check(); /* Queue only; never wait in the LVGL task. */
}

static void ota_install_now(lv_event_t *event)
{
    (void)event;
    carplay_ota_status_t ota;
    carplay_ota_status(&ota);
    if (ota.busy || ota.state != CARPLAY_OTA_AVAILABLE)
        return;
    snprintf(ota_confirm_commit, sizeof(ota_confirm_commit), "%s", ota.remote_commit);
    char body[192];
    snprintf(body, sizeof(body), TR("Version %s"), ota.remote[0] ? ota.remote : ota.remote_commit);
    lv_label_set_text_fmt(
        ota_confirm_text, "%s\n\n%s", body,
        TR("Disconnect CarPlay and keep power connected before installing."));
    show_modal(ota_confirm_panel);
}

static void ota_confirm_install(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
    if (carplay_ota_install(ota_confirm_commit) != RT_EOK)
        lv_label_set_text(ota_state, "Update changed. Retry.");
}

static void ota_confirm_cancel(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
}

static const char *ota_state_text(const carplay_ota_status_t *ota, char *buffer, size_t size)
{
    switch (ota->state)
    {
    case CARPLAY_OTA_CHECKING:
        return TR("Checking for updates...");
    case CARPLAY_OTA_UP_TO_DATE:
        return TR("Up to date");
    case CARPLAY_OTA_AVAILABLE:
        return TR("Update available");
    case CARPLAY_OTA_DOWNLOADING:
    case CARPLAY_OTA_APPLYING:
        if (ota->progress >= 0)
        {
            snprintf(buffer, size, "%s  ", TR(ota->state == CARPLAY_OTA_DOWNLOADING ? "Downloading" : "Installing"));
            size_t used = strlen(buffer);
            snprintf(buffer + used, size - used, TR("progress %d%%"), ota->progress);
            return buffer;
        }
        return TR(ota->state == CARPLAY_OTA_DOWNLOADING ? "Downloading" : "Installing");
    case CARPLAY_OTA_REBOOTING:
        return TR("Restarting");
    case CARPLAY_OTA_FAILED:
        return TR("Update failed");
    default:
        return TR("Not checked");
    }
}

static void ota_update(void)
{
    carplay_ota_status_t ota;
    carplay_ota_status(&ota);

    set_text_if_changed(ota_version, ota.current[0] ? ota.current : "unknown");
    char line[160];
    if (ota.remote[0])
        snprintf(line, sizeof(line), "%s -> %s", ota.current[0] ? ota.current : "unknown",
                 ota.remote);
    else
        snprintf(line, sizeof(line), TR("Slot %s"), ota.slot);
    set_text_if_changed(ota_detail, line);

    char state_buffer[96];
    set_text_if_changed(ota_state, ota_state_text(&ota, state_buffer, sizeof(state_buffer)));

    bool show_progress = (ota.state == CARPLAY_OTA_DOWNLOADING ||
                          ota.state == CARPLAY_OTA_APPLYING) &&
                         ota.progress >= 0;
    if (show_progress)
    {
        lv_obj_remove_flag(ota_progress, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(ota_progress, ota.progress, LV_ANIM_OFF);
    }
    else
        lv_obj_add_flag(ota_progress, LV_OBJ_FLAG_HIDDEN);

    bool busy = ota.busy != 0;
    if (!busy && network_ready)
        lv_obj_remove_state(ota_check_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(ota_check_button, LV_STATE_DISABLED);
    if (!busy && network_ready && ota.state == CARPLAY_OTA_AVAILABLE)
    {
        lv_obj_remove_flag(ota_install_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ota_install_button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_flag(ota_install_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(ota_install_button, LV_STATE_DISABLED);
    }
    set_text_if_changed(ota_backend, network_ready ? "Update backend available"
                                                   : "Update backend unavailable");
}
#else
static void ota_update(void)
{
    if (!ota_state)
        return;
    set_text_if_changed(ota_state, "Update backend unavailable");
    lv_obj_add_state(ota_check_button, LV_STATE_DISABLED);
    lv_obj_add_flag(ota_install_button, LV_OBJ_FLAG_HIDDEN);
}
#endif

static void build_ota(lv_obj_t *parent)
{
    build_header(parent, "System update", open_settings);

    panel(parent, 64, 153, 896, 282, 22, CARPLAY_STYLE_CARD);
    panel(parent, 93, 181, 76, 76, 19, CARPLAY_STYLE_TILE);
    icon_mask(parent, &carplay_icon_download_39, 111, 200, CARPLAY_STYLE_ICON_ACCENT);
    text(parent, "Installed version", 194, 199, 400, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    ota_version = text(parent, "unknown", 193, 231, 420, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    ota_detail = text(parent, "", 193, 281, 700, &carplay_ui_14, CARPLAY_STYLE_FAINT);
    divider(parent, 97, 323, 830, 1);

    /* Measured with the compiled subsets, the longest current state string is
     * 279 px in a 290 px slot; scroll is the safety net for future copy. */
    ota_state = text(parent, "Not checked", 97, 354, 290, &carplay_ui_21,
                     CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(ota_state, LV_LABEL_LONG_SCROLL_CIRCULAR);

    ota_progress = lv_bar_create(parent);
    lv_obj_remove_flag(ota_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ota_progress, 97, 388);
    lv_obj_set_size(ota_progress, 540, 6);
    lv_bar_set_range(ota_progress, 0, 100);
    lv_obj_set_style_radius(ota_progress, 3, 0);
    lv_obj_set_style_radius(ota_progress, 3, LV_PART_INDICATOR);
    carplay_ui_style_part(ota_progress, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(ota_progress, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);
    lv_obj_add_flag(ota_progress, LV_OBJ_FLAG_HIDDEN);

#if defined(RTCARPLAY_OTA)
    ota_install_button = button_text(parent, "Install update", 403, 351, 250, 59, 14,
                                     CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                                     &carplay_ui_18, ota_install_now, NULL);
    lv_obj_add_flag(ota_install_button, LV_OBJ_FLAG_HIDDEN);
    ota_check_button = button_text(parent, "Check for updates", 667, 351, 258, 59, 14,
                                   CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                                   &carplay_ui_18, ota_check_now, NULL);
    icon_mask(parent, &carplay_icon_refresh_24, 686, 368, CARPLAY_STYLE_ICON_ACCENT);
#else
    ota_install_button = panel(parent, 403, 351, 250, 59, 14, CARPLAY_STYLE_ACCENT_CARD);
    lv_obj_add_flag(ota_install_button, LV_OBJ_FLAG_HIDDEN);
    ota_check_button = button_text(parent, "Check for updates", 667, 351, 258, 59, 14,
                                   CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                                   &carplay_ui_18, NULL, NULL);
#endif
    lv_obj_add_state(ota_check_button, LV_STATE_DISABLED);

    panel(parent, 64, 457, 896, 88, 17, CARPLAY_STYLE_CARD_ALT);
    icon_mask(parent, &carplay_icon_info_26, 90, 482, CARPLAY_STYLE_ICON_FAINT);
    text(parent, "Use the existing firmware updater", 134, 479, 700, &carplay_ui_17,
         CARPLAY_STYLE_TEXT);
    ota_backend = text(parent, "Update backend available", 134, 511, 700, &carplay_ui_14,
                       CARPLAY_STYLE_MUTED);
}

/* ---------------- modals ---------------- */

static void answer_bluetooth(lv_event_t *event);

static void build_modals(lv_obj_t *screen)
{
    modal_scrim = lv_obj_create(screen);
    lv_obj_set_size(modal_scrim, lv_display_get_horizontal_resolution(NULL),
                    lv_display_get_vertical_resolution(NULL));
    lv_obj_set_style_bg_color(modal_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_scrim, LV_OPA_60, 0);
    lv_obj_set_style_border_width(modal_scrim, 0, 0);
    lv_obj_set_style_pad_all(modal_scrim, 0, 0);
    lv_obj_add_flag(modal_scrim, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(modal_scrim, LV_OBJ_FLAG_SCROLLABLE);

#if defined(RTCARPLAY_OTA)
    ota_confirm_panel = panel(screen, 80, 120, 864, 360, 24, CARPLAY_STYLE_CARD);
    text(ota_confirm_panel, "Install this update and restart?", 24, 26, 800,
         &carplay_ui_21, CARPLAY_STYLE_TEXT);
    ota_confirm_text = text(ota_confirm_panel, "", 24, 76, 800, &carplay_ui_17,
                            CARPLAY_STYLE_MUTED);
    lv_obj_set_style_text_line_space(ota_confirm_text, 8, 0);
    lv_obj_set_height(ota_confirm_text, 180);
    lv_label_set_long_mode(ota_confirm_text, LV_LABEL_LONG_WRAP);
    button_text(ota_confirm_panel, "Cancel", 24, 276, 220, 56, 14, CARPLAY_STYLE_CARD_ALT,
                CARPLAY_STYLE_TEXT, &carplay_ui_18, ota_confirm_cancel, NULL);
    button_text(ota_confirm_panel, "Install and restart", 264, 276, 320, 56, 14,
                CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT, &carplay_ui_18,
                ota_confirm_install, NULL);
    lv_obj_add_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
#endif

    bt_panel = panel(screen, 80, 120, 864, 360, 24, CARPLAY_STYLE_CARD);
    bt_text = text(bt_panel, "", 24, 32, 800, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    lv_obj_set_style_text_line_space(bt_text, 10, 0);
    lv_obj_set_height(bt_text, 200);
    lv_label_set_long_mode(bt_text, LV_LABEL_LONG_WRAP);
    bt_accept = button_text(bt_panel, "Numbers match / Allow", 24, 276, 360, 56, 14,
                            CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                            &carplay_ui_18, answer_bluetooth, NULL);
    button_text(bt_panel, "Cancel pairing", 404, 276, 260, 56, 14, CARPLAY_STYLE_CARD_ALT,
                CARPLAY_STYLE_TEXT, &carplay_ui_18, answer_bluetooth, NULL);
    lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
}

static void answer_bluetooth(lv_event_t *event)
{
    carplay_connection_pairing_reply(visible_pairing_request,
                                     lv_event_get_target(event) == bt_accept);
    lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
}

/* ---------------- live controls ---------------- */

static void runtime_controls(void)
{
    static bool backlight_restored;
    static bool volume_attempted;
    static unsigned last_volume = UINT32_MAX;
    static unsigned last_brightness = UINT32_MAX;

    unsigned stored_brightness = carplay_preferences_get(CARPLAY_PREF_BRIGHTNESS, 72U);
    if (!backlight_restored)
    {
        backlight_restored = true;
        carplay_backlight_restore(stored_brightness);
    }

    if (!volume_attempted && carplay_volume_available())
    {
        /* Persisted media volume is re-applied once to the real codec; the
         * readback below then labels the control with the actual value. */
        volume_attempted = true;
        (void)carplay_volume_set(carplay_preferences_get(CARPLAY_PREF_VOLUME, 60U));
    }
    if (volume_slider && !volume_dragging)
    {
        unsigned actual = 0;
        if (carplay_volume_available() && carplay_volume_get(&actual))
        {
            if (actual != last_volume || lv_obj_has_state(volume_slider, LV_STATE_DISABLED))
            {
                lv_slider_set_value(volume_slider, (int32_t)actual, LV_ANIM_OFF);
                last_volume = actual;
                lv_obj_remove_state(volume_slider, LV_STATE_DISABLED);
                lv_label_set_text_fmt(volume_value, "%u%%", actual);
            }
        }
        else if (!lv_obj_has_state(volume_slider, LV_STATE_DISABLED))
        {
            lv_obj_add_state(volume_slider, LV_STATE_DISABLED);
            /* Short form: at 117 px it is the widest unsupported state that
             * fits the 136 px value slot in both languages (zh 54 px). */
            lv_label_set_text(volume_value, "Not supported");
        }
    }
    if (brightness_slider && !brightness_dragging)
    {
        unsigned actual = 0;
        if (carplay_backlight_available() && carplay_backlight_get(&actual))
        {
            if (actual != last_brightness || lv_obj_has_state(brightness_slider, LV_STATE_DISABLED))
            {
                lv_slider_set_value(brightness_slider, (int32_t)actual, LV_ANIM_OFF);
                last_brightness = actual;
                lv_obj_remove_state(brightness_slider, LV_STATE_DISABLED);
                lv_label_set_text_fmt(brightness_value, "%u%%", actual);
            }
        }
        else if (!lv_obj_has_state(brightness_slider, LV_STATE_DISABLED))
        {
            lv_obj_add_state(brightness_slider, LV_STATE_DISABLED);
            lv_label_set_text(brightness_value, "Not supported");
        }
    }
}

/* ---------------- periodic update ---------------- */

static void update_dashboard(const demo_status_t *status, bool have_status)
{
    uint16_t speed = 0;
    uint8_t soc = 0;
    bool valid = false, simulated = false;
    dashboard_sample(&speed, &soc, &valid, &simulated);

    static bool shown_valid, valid_known;
    static uint16_t shown_speed;
    static uint8_t shown_soc;
    if (!valid && (!valid_known || shown_valid))
    {
        lv_label_set_text(dash_speed, "--");
        lv_label_set_text(dash_soc, "--");
        lv_bar_set_value(dash_bar, 0, LV_ANIM_OFF);
        shown_valid = false;
        valid_known = true;
    }
    else if (valid && (!valid_known || !shown_valid || speed != shown_speed || soc != shown_soc))
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%u", speed);
        set_text_if_changed(dash_speed, buffer);
        snprintf(buffer, sizeof(buffer), "%u", soc);
        set_text_if_changed(dash_soc, buffer);
        lv_bar_set_value(dash_bar, soc, LV_ANIM_OFF);
        shown_speed = speed;
        shown_soc = soc;
        shown_valid = true;
        valid_known = true;
    }

    static bool simulated_shown = true, simulated_known;
    if (!simulated_known || simulated != simulated_shown)
    {
        if (simulated)
            lv_obj_remove_flag(dash_note, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(dash_note, LV_OBJ_FLAG_HIDDEN);
        simulated_shown = simulated;
        simulated_known = true;
    }

    bool connected = have_status && status->stage == DEMO_LIVE;
    static bool shown_connected, connected_known;
    if (!connected_known || connected != shown_connected)
    {
        carplay_ui_style(dash_dot, connected ? CARPLAY_STYLE_DOT_ON : CARPLAY_STYLE_DOT_OFF);
        lv_label_set_text(dash_status, connected ? "Connected" : "Not connected");
        layout_dashboard_status();
        shown_connected = connected;
        connected_known = true;
    }

    char clock[16];
    clock_text(clock, sizeof(clock));
    set_text_if_changed(dash_clock, clock);
    /* Language switches relabel the status without going through the
     * connected/not-connected branch above, so re-check the row every tick;
     * the helper only writes when the measured geometry changed. */
    layout_dashboard_status();
}

static void update_connectivity(const demo_status_t *status, bool have_status)
{
    bool connected = have_status && status->stage == DEMO_LIVE;
    bool busy = have_status && (status->stage == DEMO_PAIR || status->stage == DEMO_NETWORK ||
                                status->stage == DEMO_CONNECT);
    static bool shown_connected, connected_known;
    if (!connected_known || connected != shown_connected)
    {
        carplay_ui_style(conn_dot, connected ? CARPLAY_STYLE_DOT_ON : CARPLAY_STYLE_DOT_OFF);
        lv_label_set_text(conn_state, connected ? "Connected"
                                      : have_status && status->stage == DEMO_PAIR
                                          ? "Connecting"
                                          : "Waiting for phone");
        shown_connected = connected;
        connected_known = true;
    }
    bool disabled = lv_obj_has_state(conn_action, LV_STATE_DISABLED);
    if (busy != disabled)
    {
        if (busy)
            lv_obj_add_state(conn_action, LV_STATE_DISABLED);
        else
            lv_obj_remove_state(conn_action, LV_STATE_DISABLED);
    }
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    /* The strip's "Dashboard" control hands the panel back to the local pages
     * while the session stays live; this action returns to the projection. */
    bool resume = have_status && connected && !status->display_reconfiguring &&
                  !carplay_launcher_video_is_active();
    static bool shown_resume, resume_known;
    if (!resume_known || resume != shown_resume)
    {
        set_text_if_changed(lv_obj_get_child(conn_action, 0),
                            resume ? "Resume CarPlay" : "Connect phone");
        shown_resume = resume;
        resume_known = true;
    }
#endif
}

static void update_settings_state(void)
{
    static unsigned last_theme = UINT32_MAX, last_language = UINT32_MAX;
    static unsigned last_display = UINT32_MAX;
    demo_status_t status = { 0 };
    bool have = carplay_connection_status(&status);

#if defined(RTCARPLAY_IAP2)
    if (!lv_obj_has_flag(settings_pages[SETTINGS_GNSS], LV_OBJ_FLAG_HIDDEN))
        update_gnss_settings();
#endif
    if (!lv_obj_has_flag(settings_pages[SETTINGS_PHONE], LV_OBJ_FLAG_HIDDEN))
    {
        set_phone_text(phone_route_caption, TR("Route guidance"));
        carplay_phone_media_t media = { 0 };
        (void)carplay_app_phone_media(&media);
        const char *title = media.title[0] ? media.title : "-";
        set_phone_text(phone_title, title);
        set_phone_text(phone_artist, media.artist);
        const char *playback = media.has_playing ? (media.playing ? "Playing" : "Not playing") : "";
        set_phone_text(phone_playback, TR(playback));
        const char *destination = media.route_destination[0] ? media.route_destination : "-";
        set_phone_text(phone_destination, destination);
        set_phone_text(phone_road, media.route_road);
        set_phone_text(phone_remaining, media.route_remaining);
    }

    (void)have;
    if (last_theme != theme_choice)
    {
        apply_segments(theme_buttons, 3, theme_choice);
        last_theme = theme_choice;
    }
    unsigned language = carplay_ui_is_chinese() ? 0U : 1U;
    if (last_language != language)
    {
        apply_segments(language_buttons, 2, language);
        last_language = language;
    }
    unsigned display = display_choice;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (have)
        display = status.display_requested == CARPLAY_FULLSCREEN ? 0U : 1U;
#endif
    if (last_display != display)
    {
        apply_segments(display_buttons, 2, display);
        last_display = display;
    }
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (have && status.display_reconfiguring)
        set_text_if_changed(display_note, "Reconnect required to apply the new layout");
#endif
}

static void update_page(lv_timer_t *timer)
{
    (void)timer;
    /* Same GUI startup owner retries a failed worker allocation/schedule. */
    static bool attempted;
    static uint32_t last_attempt;
    uint32_t now = rt_tick_get_millisecond();
    if (!attempted || (uint32_t)(now - last_attempt) >= 1000U)
    {
        attempted = true;
        last_attempt = now;
        (void)carplay_connection_manager_start();
    }

    demo_status_t status = { 0 };
    bool have_status = carplay_connection_status(&status);
    static bool was_live;
    bool live = have_status && status.stage == DEMO_LIVE;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    live = live && !status.display_reconfiguring;
#else
    live = live && carplay_launcher_video_is_active();
#endif
    if (live && !was_live)
    {
        lv_obj_t *modals[] = { bt_panel,
#if defined(RTCARPLAY_OTA)
                               ota_confirm_panel,
#endif
                               NULL };
        for (unsigned i = 0; i < sizeof(modals) / sizeof(modals[0]); ++i)
            if (modals[i])
                lv_obj_add_flag(modals[i], LV_OBJ_FLAG_HIDDEN);
        update_scrim();
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        carplay_fusion_ui_resume();
#endif
    }
    if (have_status)
        was_live = live;

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    dispatch_fusion_touch(carplay_fusion_cancel_touch());
    if (carplay_fusion_ui_update(&status))
        return;
#endif
    /* No invalidation while the phone owns the whole panel. */
    if (live && carplay_launcher_video_is_active())
        return;

    carplay_internet_status_t network;
    carplay_internet_status(&network);
    network_ready = network.available && network.route_ready;

    runtime_controls();
    update_dashboard(&status, have_status);
    update_connectivity(&status, have_status);
    update_settings_state();
    ota_update();

    if (have_status && status.wireless && status.pairing.kind &&
        status.stage != DEMO_CLOSING && status.stage != DEMO_FAILED)
    {
        if (visible_pairing_request != status.pairing.request)
        {
            visible_pairing_request = status.pairing.request;
            if (status.pairing.kind == RTCP_BT_PAIR_CONSENT)
                lv_label_set_text(bt_text, "Allow Bluetooth pairing?");
            else
                lv_label_set_text_fmt(bt_text,
                                      status.pairing.kind == RTCP_BT_PAIR_COMPARE
                                          ? "Confirm this number on your iPhone.\n\n%06u"
                                          : "Enter this number on your iPhone.\n\n%06u",
                                      status.pairing.passkey);
            if (status.pairing.kind == RTCP_BT_PAIR_DISPLAY)
                lv_obj_add_state(bt_accept, LV_STATE_DISABLED);
            else
                lv_obj_remove_state(bt_accept, LV_STATE_DISABLED);
            show_modal(bt_panel);
        }
    }
    else
    {
        visible_pairing_request = 0;
        if (!lv_obj_has_flag(bt_panel, LV_OBJ_FLAG_HIDDEN))
        {
            lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
            update_scrim();
        }
    }
    update_scrim();
}

/* ---------------- init ---------------- */

void carplay_launcher_open_display_settings(void)
{
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_fusion_ui_show_launcher();
#endif
    if (settings_pages[SETTINGS_DISPLAY])
        show_settings_page(SETTINGS_DISPLAY);
    show_page(PAGE_SETTINGS);
}

/* SDK calls this from lv_user_gui_init(), after display registration. */
void carplay_demo_launcher_init(void)
{
    if (pointer_lock != NULL)
        return;
    pointer_lock = rt_mutex_create("cpdemo", RT_IPC_FLAG_PRIO);
    if (pointer_lock == NULL)
    {
        rt_kprintf("[carplay-demo] no status mutex\n");
        return;
    }
    (void)carplay_preferences_init();
    if (carplay_video_sink_set_display_observer(carplay_launcher_video_event,
                                                NULL) != RTCP_OK)
        rt_kprintf("[carplay-demo] video observer registration failed\n");

    carplay_ui_set_dark(carplay_preferences_get(CARPLAY_PREF_THEME, 1) != 0);
    carplay_ui_set_chinese(carplay_preferences_get(CARPLAY_PREF_LANGUAGE, 0) != 0);
    theme_choice = carplay_ui_dark() ? 1U : 0U;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    display_choice = carplay_preferences_get(CARPLAY_PREF_DISPLAY, CARPLAY_FUSION) ==
                             CARPLAY_FULLSCREEN
                         ? 0U
                         : 1U;
#else
    display_choice = 1U;
#endif

    lv_obj_t *screen = lv_screen_active();
    carplay_ui_style(screen, CARPLAY_STYLE_SCREEN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(screen, &carplay_ui_17, 0);
    int width = lv_display_get_horizontal_resolution(NULL);
    int height = lv_display_get_vertical_resolution(NULL);

    for (unsigned i = 0; i < PAGE_COUNT; ++i)
    {
        pages[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(pages[i]);
        lv_obj_set_pos(pages[i], 0, 0);
        lv_obj_set_size(pages[i], width, height);
        lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    build_dashboard(pages[PAGE_DASHBOARD]);
    build_connectivity(pages[PAGE_CONNECTIVITY]);
    build_settings(pages[PAGE_SETTINGS]);
    build_ota(pages[PAGE_OTA]);
    build_modals(screen);
    show_page(PAGE_DASHBOARD);

    lv_indev_t *pointer = lv_indev_create();
    if (pointer)
    {
        lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(pointer, pointer_read);
    }
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_fusion_ui_init(screen, (uint16_t)width, (uint16_t)height);
#endif
    lv_timer_create(update_page, 250, NULL);
    int touch = carplay_touch_panel_start((uint16_t)width, (uint16_t)height,
                                          launcher_panel_touch, NULL);
    if (touch != RTCP_OK)
        rt_kprintf("[carplay-demo] launch page touch error=%d\n", touch);
    int result = carplay_connection_manager_start();
    if (result != RTCP_OK)
        rt_kprintf("[carplay-demo] connection manager error=%d\n", result);
}

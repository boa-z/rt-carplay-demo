/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <rtthread.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lvgl.h"
#if LV_USE_TJPGD
#include "src/libs/tjpgd/tjpgd.h"
#endif
#include "connection_manager.h"
#include "internet_share_lubanlite.h"
#include "carplay_app.h"
#include "video_sink_lubanlite.h"
#include "video_layer_policy.h"
#include "fusion_dashboard.h"
#include "launcher_touch_lubanlite.h"
#include "launcher_layout.h"
#include "rtcp/rtcp.h"
#include "rtcp/input.h"
#include "ui_language.h"
#include "launcher_preferences.h"
#include "carplay_ui_theme.h"
#include "connectivity_page.h"
#include "settings_navigation.h"
#include "system_shell.h"
#include "vehicle_instruments.h"
#include "icons/carplay_icons.h"
#include "audio_volume_lubanlite.h"
#if defined(RTCARPLAY_MICROPHONE)
#include "audio_input_lubanlite.h"
#endif
#include "backlight_lubanlite.h"
#include "carplay_build_info.h"
#if defined(RTCARPLAY_IAP2)
#include "gnss_demo.h"
#endif
#if defined(LPKG_BT_DEVICE_NAME_DYNAMIC) && \
    (defined(RT_USING_DFS) || defined(CARPLAY_PREFERENCES_EXTERNAL_PROVIDER))
#define CARPLAY_BT_NAME_SETTINGS 1
#endif

static void set_phone_text(lv_obj_t *label, const char *value)
{
    if (strcmp(lv_label_get_text(label), value) != 0)
        lv_label_set_text(label, value);
}

#define TR carplay_ui_translate
#if defined(RTCARPLAY_OTA)
#include "ota_lubanlite.h"
#endif
#if defined(RTCARPLAY_WIFI_STA_MAINTENANCE)
#include "wifi_sta_lubanlite.h"
#define CARPLAY_STA_SETTINGS
#endif
#if defined(RTCARPLAY_NETWORK_TIME)
#include "network_time_lubanlite.h"
#endif
#if defined(RT_USING_RTC)
#include <drivers/rtc.h>
#include <sys/time.h>
#include <time.h>
#endif

LV_FONT_DECLARE(carplay_display_220);
LV_FONT_DECLARE(carplay_display_110);
LV_FONT_DECLARE(carplay_ui_34);
LV_FONT_DECLARE(carplay_ui_21);
LV_FONT_DECLARE(carplay_ui_18);
LV_FONT_DECLARE(carplay_ui_17);
LV_FONT_DECLARE(carplay_ui_15);
LV_FONT_DECLARE(carplay_ui_14);
LV_FONT_DECLARE(carplay_display_58);

typedef enum
{
    PAGE_DASHBOARD,
    PAGE_CONNECTIVITY,
    PAGE_SETTINGS,
    PAGE_OTA,
    PAGE_PHONE_DETAILS,
    PAGE_COUNT
} page_t;

typedef enum
{
    SETTINGS_DISPLAY,
    SETTINGS_SOUND,
    SETTINGS_CONNECTIONS,
    SETTINGS_GENERAL,
    SETTINGS_SYSTEM,
    SETTINGS_DEVELOPER,
    SETTINGS_LANGUAGE,
#if defined(CARPLAY_STA_SETTINGS)
    SETTINGS_WIFI,
#endif
#if defined(RTCARPLAY_INTERNET_SHARE)
    SETTINGS_ETHERNET,
#endif
#if defined(RT_USING_RTC)
    SETTINGS_CLOCK,
#endif
    SETTINGS_ABOUT,
#if defined(CARPLAY_BT_NAME_SETTINGS)
    SETTINGS_BLUETOOTH,
#endif
#if defined(RTCARPLAY_IAP2)
    SETTINGS_GNSS,
#endif
    SETTINGS_PAGE_COUNT
} settings_page_t;

enum
{
    SETTINGS_DETAIL_X = CARPLAY_SETTINGS_DETAIL_X,
    SETTINGS_DETAIL_WIDTH = CARPLAY_SETTINGS_DETAIL_WIDTH,
    SETTINGS_CONTENT_TOP = CARPLAY_SETTINGS_CONTENT_TOP,
    SETTINGS_DETAIL_HEIGHT = CARPLAY_SETTINGS_DETAIL_HEIGHT
};

static lv_obj_t *pages[PAGE_COUNT];
static lv_obj_t *modal_scrim;
static carplay_system_shell_t shell;
static page_t current_page;

/* Dashboard */
static lv_obj_t *dash_dot, *dash_status, *dash_clock;
static lv_obj_t *dash_wifi, *dash_ethernet, *status_bluetooth, *status_phone;
static carplay_dashboard_t dashboard;

/* Connectivity */
static carplay_connectivity_view_t connectivity_view;
static lv_obj_t *phone_transport;
static carplay_phone_media_t phone_model;

/* Settings */
static lv_obj_t *volume_value, *volume_slider, *brightness_value, *brightness_slider;
#if defined(RTCARPLAY_MICROPHONE)
static lv_obj_t *microphone_button, *microphone_caption;
#endif
static lv_obj_t *theme_buttons[2], *language_buttons[2], *display_buttons[2];
#if defined(CARPLAY_STA_SETTINGS)
static void wifi_keyboard_close(lv_event_t *event);
#endif
#if defined(RTCARPLAY_DEMO_DEVELOPER_OPTIONS)
static lv_obj_t *fps_buttons[2];
#endif
#if defined(CARPLAY_BT_NAME_SETTINGS)
static lv_obj_t *bluetooth_name, *bluetooth_keyboard;
#endif
static lv_obj_t *phone_appearance_buttons[3];
static carplay_phone_hub_t phone_hub;
static lv_obj_t *phone_status_caption;
_Static_assert((int)CARPLAY_HUB_MANEUVER_ROWS == (int)CARPLAY_PHONE_MANEUVER_ROWS,
               "Phone Hub must fit all maneuver rows");
static lv_obj_t *phone_power, *phone_cellular, *phone_call, *phone_clock, *phone_clock_mode;
LV_FONT_DECLARE(carplay_cjk_full_18);
#if defined(RTCARPLAY_IAP2)
static lv_obj_t *gnss_latitude, *gnss_longitude, *gnss_keyboard, *gnss_state, *gnss_counts;
static lv_obj_t *gnss_end_latitude, *gnss_end_longitude, *gnss_speed;
static lv_obj_t *gnss_buttons[2], *gnss_mode_buttons[2], *gnss_apply_button;
static bool gnss_moving;
#endif
static lv_obj_t *display_note, *settings_hint;
#if defined(RTCARPLAY_INTERNET_SHARE)
static lv_obj_t *ethernet_values[5];
#endif
#if defined(RT_USING_RTC)
static lv_obj_t *clock_current, *clock_date, *clock_time, *clock_zone, *clock_keyboard, *clock_apply;
static lv_obj_t *clock_mode_buttons[2];
#if defined(RTCARPLAY_NETWORK_TIME)
static lv_obj_t *clock_network_button, *clock_network_status;
static lv_obj_t *clock_server_panel, *clock_server_entry, *clock_server_hint, *clock_server_value;
#endif
static void load_clock_settings(void);
#endif
static lv_obj_t *settings_pages[SETTINGS_PAGE_COUNT];
static lv_obj_t *settings_tabs[SETTINGS_PAGE_COUNT];
static lv_obj_t *settings_heading;
static settings_page_t settings_current;
static bool volume_dragging, brightness_dragging;
static unsigned display_choice;

/* OTA */
static lv_obj_t *ota_version, *ota_detail, *ota_state, *ota_progress;
static lv_obj_t *ota_update_button, *ota_backend;
#if defined(RTCARPLAY_OTA)
static lv_obj_t *ota_confirm_panel, *ota_confirm_text;
static lv_obj_t *ota_source_panel, *ota_source_entry, *ota_source_hint, *ota_action_label, *ota_url;
static uint32_t ota_confirm_candidate;
static bool ota_hmi_ready;
#endif

/* Pairing */
static lv_obj_t *bt_panel, *bt_text, *bt_accept;
static uint32_t visible_pairing_request;

static void update_page(lv_timer_t *timer);
static void show_settings_page(settings_page_t target);

/* ---------------- primitives ---------------- */

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                       carplay_style_role_t role)
{
    return carplay_launcher_panel(parent, x, y, w, h, radius, role);
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
        carplay_ui_set_text(obj, value);
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
    lv_obj_t *line = panel(parent, x, y, w, h, 0, CARPLAY_STYLE_LINE_BORDER);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
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
    if (label)
        carplay_ui_set_text(label, value);
}

#if defined(RTCARPLAY_OTA) || defined(RTCARPLAY_NETWORK_TIME)
static lv_obj_t *build_text_keyboard(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(keyboard, width, height);
    lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, x, y);
    carplay_ui_style(keyboard, CARPLAY_STYLE_CARD);
    carplay_ui_style_part(keyboard, CARPLAY_STYLE_INPUT, LV_PART_ITEMS);
    carplay_ui_style_part(keyboard, CARPLAY_STYLE_INPUT, LV_PART_ITEMS | LV_STATE_CHECKED);
    carplay_ui_style_part(keyboard, CARPLAY_STYLE_ACCENT_CARD, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_font(keyboard, LV_FONT_DEFAULT, LV_PART_ITEMS);
    return keyboard;
}
#endif

/* ---------------- page navigation ---------------- */

static void show_page(page_t target)
{
    if (!pages[target])
        return;
#if defined(CARPLAY_STA_SETTINGS)
    if (target != PAGE_SETTINGS)
        wifi_keyboard_close(NULL);
#endif
    for (unsigned i = 0; i < PAGE_COUNT; ++i)
    {
        if (!pages[i])
            continue;
        if (i == (unsigned)target)
            lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    current_page = target;
    unsigned destination = target == PAGE_OTA             ? PAGE_SETTINGS
                           : target == PAGE_PHONE_DETAILS ? PAGE_CONNECTIVITY
                                                          : target;
    carplay_system_shell_select(&shell, destination);
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

static void open_phone_details(lv_event_t *event)
{
    (void)event;
    show_page(PAGE_PHONE_DETAILS);
}

static void navigate(lv_event_t *event)
{
    show_page((page_t)(uintptr_t)lv_event_get_user_data(event));
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
static void update_scrim(void)
{
    if (!modal_scrim)
        return;
    lv_obj_t *modals[] = { bt_panel,
#if defined(RTCARPLAY_NETWORK_TIME)
                           clock_server_panel,
#endif
#if defined(RTCARPLAY_OTA)
                           ota_confirm_panel,
                           ota_source_panel,
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
    long rem = (long)(seconds % 86400) + carplay_app_clock_offset_minutes() * 60L;
    rem = (rem % 86400 + 86400) % 86400;
    snprintf(out, size, "%02ld:%02ld", rem / 3600, rem / 60 % 60);
#else
    snprintf(out, size, "--:--");
#endif
}

#define DASH_CLOCK_RIGHT     992
#define DASH_STATUS_CENTER_Y 20
#define DASH_STATUS_GAP      12
#define DASH_STATUS_MIN_X    560
#define DASH_DOT_SIZE        11
#define DASH_DOT_GAP         12
#define DASH_NET_ICON_SIZE   20

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
    int32_t ethernet_x = DASH_CLOCK_RIGHT - label_width(dash_clock) -
                         DASH_STATUS_GAP - DASH_NET_ICON_SIZE;
    int32_t wifi_x = ethernet_x - DASH_STATUS_GAP - DASH_NET_ICON_SIZE;
    int32_t limit = wifi_x - DASH_STATUS_GAP;
    int32_t width = LV_MIN(label_width(dash_status) + 2, limit - DASH_STATUS_MIN_X);
    int32_t left = limit - width;
    int32_t dot_x = left - DASH_DOT_GAP - DASH_DOT_SIZE;
    int32_t label_top = DASH_STATUS_CENTER_Y - lv_obj_get_style_text_font(dash_status, 0)->line_height / 2;
    if (lv_obj_get_x(dash_ethernet) != ethernet_x)
        lv_obj_set_x(dash_ethernet, ethernet_x);
    if (lv_obj_get_x(dash_wifi) != wifi_x)
        lv_obj_set_x(dash_wifi, wifi_x);
    if (lv_obj_get_x(dash_status) != left || lv_obj_get_y(dash_status) != label_top)
        lv_obj_set_pos(dash_status, left, label_top);
    if (lv_obj_get_x(dash_status) != left || lv_obj_get_width(dash_status) != width)
        lv_obj_set_width(dash_status, width);
    if (lv_obj_get_x(dash_dot) != dot_x)
        lv_obj_set_x(dash_dot, dot_x);
}

static void update_network_icon(lv_obj_t *icon, carplay_style_role_t *shown, bool link,
                                bool addressed)
{
    carplay_style_role_t role = CARPLAY_STYLE_ICON_FAINT;
    if (link)
        role = addressed ? CARPLAY_STYLE_ICON_ACCENT : CARPLAY_STYLE_ICON_MUTED;
    if (*shown != role)
    {
        carplay_ui_style(icon, role);
        *shown = role;
    }
}

static void build_status_bar(lv_obj_t *parent)
{
    divider(parent, 0, CARPLAY_SHELL_STATUS_HEIGHT - 1, 1024, 1);
    status_phone = text(parent, "", 24, 9, 290, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    status_bluetooth = text(parent, "", 322, 9, 190, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    dash_dot = stroke(parent, 0, DASH_STATUS_CENTER_Y - DASH_DOT_SIZE / 2,
                      DASH_DOT_SIZE, DASH_DOT_SIZE, LV_RADIUS_CIRCLE, CARPLAY_STYLE_DOT_OFF);
    dash_clock = text(parent, "--:--", DASH_CLOCK_RIGHT - 84,
                      DASH_STATUS_CENTER_Y - carplay_ui_17.line_height / 2, 84,
                      &carplay_ui_17, CARPLAY_STYLE_MUTED);
    lv_obj_set_style_text_align(dash_clock, LV_TEXT_ALIGN_RIGHT, 0);
    int32_t icon_top = DASH_STATUS_CENTER_Y - DASH_NET_ICON_SIZE / 2;
    dash_ethernet = icon_mask(parent, &carplay_icon_ethernet_20, 0, icon_top, CARPLAY_STYLE_ICON_FAINT);
    dash_wifi = icon_mask(parent, &carplay_icon_wifi_20, 0, icon_top, CARPLAY_STYLE_ICON_FAINT);
    dash_status = text(parent, "Not connected", DASH_STATUS_MIN_X, 0, 210, &carplay_ui_14,
                       CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(dash_status, LV_LABEL_LONG_CLIP);
    layout_dashboard_status();
}

static void build_dashboard(lv_obj_t *parent)
{
    carplay_dashboard_build(&dashboard, parent, open_connectivity);
}

/* Header shared by the three inner pages. */
static void build_header(lv_obj_t *parent, const char *title, lv_event_cb_t go_back)
{
    lv_obj_t *back = panel(parent, 20, 12, 64, 64, 8, CARPLAY_STYLE_CARD_ALT);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    carplay_ui_style_part(back, CARPLAY_STYLE_CARD_ALT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, go_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = icon_mask(back, &carplay_icon_arrow_left_22, 0, 0,
                                CARPLAY_STYLE_ICON_TEXT);
    lv_obj_center(arrow);
    lv_obj_t *heading = text(parent, title, 0, 0, 700, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    lv_obj_align_to(heading, back, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    divider(parent, 32, 83, 960, 1);
}

/* ---------------- connectivity ---------------- */

/* ---------------- settings ---------------- */

static void settings_status(const char *english, bool error)
{
    if (!settings_hint)
        return;
    carplay_ui_set_text(settings_hint, english);
    carplay_ui_style(settings_hint, error ? CARPLAY_STYLE_WARN_TEXT : CARPLAY_STYLE_FAINT);
}

#if defined(RTCARPLAY_MICROPHONE)
static void update_microphone(void)
{
    bool muted = carplay_audio_input_is_muted();
    carplay_ui_set_text(microphone_caption, muted ? "Unmute microphone" : "Mute microphone");
    if (muted)
        lv_obj_add_state(microphone_button, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(microphone_button, LV_STATE_CHECKED);
}

static void select_microphone(lv_event_t *event)
{
    (void)event;
    bool muted = !carplay_audio_input_is_muted();
    carplay_audio_input_mute(muted);
    int result = carplay_preferences_set(CARPLAY_PREF_MIC_MUTED, muted);
    update_microphone();
    settings_status(result ? "Settings not saved." : "Settings saved.", result != 0);
}
#endif

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
    carplay_segments_select(buttons, count, selected);
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
    carplay_ui_set_dark(index == 1);
    int result = carplay_preferences_set(CARPLAY_PREF_THEME, index);
    apply_segments(theme_buttons, 2, index);
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
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    carplay_display_mode_t mode = index ? CARPLAY_FUSION : CARPLAY_FULLSCREEN;
    if (!carplay_connection_set_display_mode(mode))
    {
        settings_status("Settings not saved.", true);
        return;
    }
    display_choice = index;
    apply_segments(display_buttons, 2, display_choice);
    int result = carplay_preferences_error();
    settings_status(result ? "Settings not saved." : "Settings saved.", result != 0);
    carplay_ui_set_text(display_note, "");
#else
    (void)event;
    settings_status("Not supported on this device", true);
#endif
}

#if defined(RTCARPLAY_DEMO_DEVELOPER_OPTIONS)
static void select_fps(lv_event_t *event)
{
    unsigned choice = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    int result = carplay_preferences_set(CARPLAY_PREF_HIGH_FPS, choice);
    if (result == 0)
        apply_segments(fps_buttons, 2, choice);
    settings_status(result ? "Settings not saved." : "Saved. Restart the receiver to apply.", result != 0);
}
#endif

typedef const lv_image_dsc_t *icon_asset_t;

static void build_slider_card(lv_obj_t *parent, int y, const char *title, icon_asset_t icon,
                              lv_obj_t **value_out, lv_obj_t **slider_out,
                              unsigned minimum, unsigned maximum,
                              lv_event_cb_t callback, const char *hint)
{
    lv_obj_t *card = panel(parent, 0, y, SETTINGS_DETAIL_WIDTH - 4, hint ? 128 : 86, 12,
                           CARPLAY_STYLE_CARD);
    if (icon)
        icon_mask(card, icon, 23, 16, CARPLAY_STYLE_ICON_ACCENT);
    text(card, title, icon ? 68 : 22, 16, 380, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    *value_out = text(card, "", SETTINGS_DETAIL_WIDTH - 170, 17, 142,
                      &carplay_ui_18, CARPLAY_STYLE_TEXT);
    lv_obj_set_style_text_align(*value_out, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_pos(slider, 24, hint ? 67 : 54);
    lv_obj_set_size(slider, SETTINGS_DETAIL_WIDTH - 52, 8);
    lv_obj_set_ext_click_area(slider, 18);
    lv_slider_set_range(slider, (int32_t)minimum, (int32_t)maximum);
    /* An 8 px track: the radius must fit its short side, otherwise LVGL's
     * indicator clipping path misplaces the mask and corrupts the layer. */
    lv_obj_set_style_radius(slider, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 4, LV_PART_INDICATOR);
    carplay_ui_style_part(slider, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(slider, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider, 7, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    carplay_ui_style_part(slider, CARPLAY_STYLE_SLIDER_KNOB, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_PRESS_LOST, NULL);
    *slider_out = slider;

    if (hint)
        text(card, hint, 24, 107, 580, &carplay_ui_14, CARPLAY_STYLE_MUTED);
}

static settings_page_t settings_section(settings_page_t target)
{
    switch (target)
    {
#if defined(CARPLAY_BT_NAME_SETTINGS)
    case SETTINGS_BLUETOOTH:
#endif
#if defined(CARPLAY_STA_SETTINGS)
    case SETTINGS_WIFI:
#endif
#if defined(RTCARPLAY_INTERNET_SHARE)
    case SETTINGS_ETHERNET:
#endif
        return SETTINGS_CONNECTIONS;
    case SETTINGS_LANGUAGE:
#if defined(RT_USING_RTC)
    case SETTINGS_CLOCK:
#endif
        return SETTINGS_GENERAL;
    case SETTINGS_ABOUT:
        return SETTINGS_SYSTEM;
#if defined(RTCARPLAY_IAP2)
    case SETTINGS_GNSS:
        return SETTINGS_DEVELOPER;
#endif
    default:
        return target;
    }
}

static const char * const settings_titles[SETTINGS_PAGE_COUNT] = {
    [SETTINGS_DISPLAY] = "Display",
    [SETTINGS_SOUND] = "Sound",
    [SETTINGS_CONNECTIONS] = "Connections",
    [SETTINGS_GENERAL] = "General",
    [SETTINGS_SYSTEM] = "System",
    [SETTINGS_DEVELOPER] = "Developer options",
    [SETTINGS_LANGUAGE] = "Language",
    [SETTINGS_ABOUT] = "About",
#if defined(RT_USING_RTC)
    [SETTINGS_CLOCK] = "Date & time",
#endif
#if defined(CARPLAY_BT_NAME_SETTINGS)
    [SETTINGS_BLUETOOTH] = "Bluetooth",
#endif
#if defined(CARPLAY_STA_SETTINGS)
    [SETTINGS_WIFI] = "Wi-Fi",
#endif
#if defined(RTCARPLAY_INTERNET_SHARE)
    [SETTINGS_ETHERNET] = "Ethernet",
#endif
#if defined(RTCARPLAY_IAP2)
    [SETTINGS_GNSS] = "GNSS simulation",
#endif
};

static void show_settings_page(settings_page_t target)
{
#if defined(CARPLAY_STA_SETTINGS)
    if (target != SETTINGS_WIFI)
        wifi_keyboard_close(NULL);
#endif
    settings_current = target;
    carplay_ui_set_text(settings_heading, settings_titles[target]);
    settings_page_t section = settings_section(target);
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
    {
        bool selected = index == (unsigned)target;
        if (selected)
            lv_obj_remove_flag(settings_pages[index], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(settings_pages[index], LV_OBJ_FLAG_HIDDEN);
        if (settings_tabs[index])
            carplay_settings_tab_select(settings_tabs[index], index == (unsigned)section);
    }
#if defined(RT_USING_RTC)
    if (target == SETTINGS_CLOCK)
        load_clock_settings();
#endif
}

static void select_settings_page(lv_event_t *event)
{
    settings_status("", false);
    show_settings_page((settings_page_t)(uintptr_t)lv_event_get_user_data(event));
}

static void settings_back(lv_event_t *event)
{
    (void)event;
    settings_page_t section = settings_section(settings_current);
    settings_status("", false);
    if (settings_current == section)
        show_page(PAGE_DASHBOARD);
    else
        show_settings_page(section);
}

static void build_display_settings(lv_obj_t *parent)
{
    text(parent, "Screen", 6, 0, 260, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    build_slider_card(parent, 26, "Display brightness", NULL,
                      &brightness_value, &brightness_slider, CARPLAY_PREF_BRIGHTNESS_MIN,
                      CARPLAY_PREF_BRIGHTNESS_MAX, brightness_event, NULL);

    carplay_appearance_build(parent, 123, theme_buttons, select_theme);

    text(parent, "Phone projection", 6, 257, 280, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    lv_obj_t *display = panel(parent, 0, 278, SETTINGS_DETAIL_WIDTH - 4, 120, 12,
                              CARPLAY_STYLE_CARD);
    const int controls_x = SETTINGS_DETAIL_WIDTH - 332;
    text(display, "CarPlay display", 22, 18, 310, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    display_buttons[0] = segment(display, "Fullscreen", controls_x, 6, 150, 48, select_display,
                                 (void *)0, &carplay_ui_15);
    display_buttons[1] = segment(display, "Fusion", controls_x + 154, 6, 150, 48, select_display,
                                 (void *)1, &carplay_ui_15);
    for (unsigned index = 0; index < 2; ++index)
    {
        lv_obj_t *screen = panel(display_buttons[index], 12, 15, 30, 20, 4, CARPLAY_STYLE_BAR_FILL);
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_CLICKABLE);
        if (index)
        {
            lv_obj_t *strip_preview = panel(screen, 23, 0, 7, 20, 0, CARPLAY_STYLE_BAR_BG);
            lv_obj_remove_flag(strip_preview, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_align(lv_obj_get_child(display_buttons[index], 0), LV_ALIGN_CENTER, 16, 0);
    }
    display_note = text(display, "", 22, 41, 310, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(display_note, LV_LABEL_LONG_WRAP);
#if !defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    lv_obj_add_state(display_buttons[0], LV_STATE_DISABLED);
    lv_obj_add_state(display_buttons[1], LV_STATE_DISABLED);
    carplay_ui_set_text(display_note, "Not supported on this device");
#endif

    divider(display, 22, 59, SETTINGS_DETAIL_WIDTH - 48, 1);
    text(display, "Phone appearance", 22, 78, 310, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    text(display, "Auto uses daylight on this board", 22, 100, 310,
         &carplay_ui_14, CARPLAY_STYLE_MUTED);
    const char *choices[] = { "Auto", "Light", "Dark" };
    for (unsigned index = 0; index < 3; ++index)
        phone_appearance_buttons[index] = segment(display, choices[index], controls_x + 102 * (int)index, 66,
                                                  100, 48, select_phone_appearance, (void *)(uintptr_t)index,
                                                  &carplay_ui_15);
    apply_segments(phone_appearance_buttons, 3, carplay_app_phone_appearance());
}

#if defined(RTCARPLAY_DEMO_DEVELOPER_OPTIONS)
static void build_frame_rate(lv_obj_t *parent)
{
    lv_obj_t *fps = panel(parent, 0, 0, SETTINGS_DETAIL_WIDTH - 4, 90, 12, CARPLAY_STYLE_CARD);
    text(fps, "CarPlay frame rate", 24, 19, 265, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    text(fps, "Requested maximum", 24, 48, 265, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    fps_buttons[0] = segment(fps, "30 fps", 303, 20, 150, 50, select_fps, (void *)0,
                             &carplay_ui_15);
    fps_buttons[1] = segment(fps, "60 fps", 453, 20, 150, 50, select_fps, (void *)1,
                             &carplay_ui_15);
    apply_segments(fps_buttons, 2,
                   carplay_preferences_get(CARPLAY_PREF_HIGH_FPS, CARPLAY_PREF_HIGH_FPS_DEFAULT));
}
#endif

#if defined(CARPLAY_BT_NAME_SETTINGS)
static void bluetooth_keyboard_close(lv_event_t *event)
{
    (void)event;
    lv_keyboard_set_textarea(bluetooth_keyboard, NULL);
    lv_obj_add_flag(bluetooth_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void bluetooth_edit(lv_event_t *event)
{
    (void)event;
    lv_keyboard_set_textarea(bluetooth_keyboard, bluetooth_name);
    lv_obj_remove_flag(bluetooth_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void bluetooth_save(lv_event_t *event)
{
    (void)event;
    int result = carplay_preferences_set_bt_name(lv_textarea_get_text(bluetooth_name));
    if (result == 0)
        bluetooth_keyboard_close(NULL);
    const char *message = result == -EINVAL ? "Invalid Bluetooth name" : "Settings not saved.";
    settings_status(result ? message : "Saved. Restart the device to apply.", result != 0);
}

static void build_bluetooth_settings(lv_obj_t *parent)
{
    lv_obj_t *card = panel(parent, 0, 0, SETTINGS_DETAIL_WIDTH - 4, 162, 16, CARPLAY_STYLE_CARD);
    text(card, "Bluetooth name", 24, 20, 580, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    text(card, "1-28 printable ASCII characters", 24, 121, 580, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    bluetooth_name = lv_textarea_create(card);
    lv_textarea_set_one_line(bluetooth_name, true);
    lv_textarea_set_max_length(bluetooth_name, CARPLAY_BT_NAME_MAX);
    lv_obj_set_pos(bluetooth_name, 24, 56);
    lv_obj_set_size(bluetooth_name, 416, 50);
    lv_obj_set_style_text_font(bluetooth_name, &carplay_ui_18, LV_PART_MAIN);
    carplay_ui_style(bluetooth_name, CARPLAY_STYLE_INPUT);
    char name[CARPLAY_BT_NAME_MAX + 1];
    carplay_preferences_bt_name(name);
    lv_textarea_set_text(bluetooth_name, name);
    lv_obj_add_event_cb(bluetooth_name, bluetooth_edit, LV_EVENT_CLICKED, NULL);

    button_text(card, "Apply", 456, 56, 148, 50, 10, CARPLAY_STYLE_ACCENT_CARD,
                CARPLAY_STYLE_ACCENT_TEXT, &carplay_ui_18, bluetooth_save, NULL);
    bluetooth_keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(bluetooth_keyboard, SETTINGS_DETAIL_WIDTH - 4, 198);
    lv_obj_align(bluetooth_keyboard, LV_ALIGN_TOP_LEFT, 0, 174);
    carplay_ui_style(bluetooth_keyboard, CARPLAY_STYLE_CARD);
    carplay_ui_style_part(bluetooth_keyboard, CARPLAY_STYLE_INPUT, LV_PART_ITEMS);
    carplay_ui_style_part(bluetooth_keyboard, CARPLAY_STYLE_INPUT, LV_PART_ITEMS | LV_STATE_CHECKED);
    carplay_ui_style_part(bluetooth_keyboard, CARPLAY_STYLE_ACCENT_CARD, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(bluetooth_keyboard, bluetooth_keyboard_close, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(bluetooth_keyboard, bluetooth_keyboard_close, LV_EVENT_CANCEL, NULL);
    bluetooth_keyboard_close(NULL);
}
#endif

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

static void build_phone_hub(lv_obj_t *parent)
{
    carplay_phone_hub_build(&phone_hub, parent, open_phone_details);
    carplay_connectivity_build(&connectivity_view, parent);
}

static void back_to_phone(lv_event_t *event)
{
    (void)event;
    show_page(PAGE_CONNECTIVITY);
}

static void build_phone_details(lv_obj_t *parent)
{
    build_header(parent, "Phone details", back_to_phone);
    lv_obj_t *status = panel(parent, 24, 112, 976, 222, 12, CARPLAY_STYLE_CARD);
    phone_status_caption = text(status, "", 24, 12, 579, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    phone_power = text(status, "", 24, 42, 265, &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
    phone_cellular = text(status, "", 302, 42, 301, &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
    phone_call = text(status, "", 24, 72, 579, &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
    lv_label_set_long_mode(phone_power, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(phone_cellular, LV_LABEL_LONG_DOT);
    divider(status, 24, 102, 579, 1);
    phone_clock = text(status, "", 24, 115, 325, &carplay_ui_18, CARPLAY_STYLE_TEXT);
    phone_clock_mode = text(status, "", 349, 115, 254, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    lv_obj_set_style_text_align(phone_clock_mode, LV_TEXT_ALIGN_RIGHT, 0);
    phone_transport = text(status, "", 24, 155, 579, &carplay_ui_18, CARPLAY_STYLE_TEXT);
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
        carplay_ui_set_text(gnss_state, state);
    char counts[76];
    snprintf(counts, sizeof(counts), "GGA %u   RMC %u   %s %u", status.gga_queued, status.rmc_queued,
             TR("Failed"), status.failed);
    if (strcmp(lv_label_get_text(gnss_counts), counts))
        carplay_ui_set_text(gnss_counts, counts);
}
#endif

#if defined(RT_USING_RTC)
static bool read_local_clock(struct tm *value)
{
    rt_device_t device = rt_device_find("rtc");
    time_t utc = 0;
    if (!device || rt_device_control(device, RT_DEVICE_CTRL_RTC_GET_TIME, &utc) != RT_EOK || utc <= 0)
        return false;
    int64_t local = (int64_t)utc + carplay_app_clock_offset_minutes() * 60L;
    time_t seconds = (time_t)local;
    if ((int64_t)seconds != local)
        return false;
#if defined(_WIN32)
    return gmtime_s(value, &seconds) == 0;
#else
    return gmtime_r(&seconds, value) != NULL;
#endif
}

static void format_clock_zone(char *out, size_t size)
{
    int offset = carplay_app_clock_offset_minutes();
    int minutes = abs(offset);
    snprintf(out, size, "%c%02d:%02d", offset < 0 ? '-' : '+', minutes / 60, minutes % 60);
}

static void format_local_clock(char *timestamp, size_t size)
{
    struct tm local;
    snprintf(timestamp, size, "--");
    if (read_local_clock(&local))
    {
        char zone[12];
        format_clock_zone(zone, sizeof(zone));
        size_t used = strftime(timestamp, size, "%Y-%m-%d %H:%M:%S", &local);
        if (used)
            snprintf(timestamp + used, size - used, "  UTC%s", zone);
    }
}

static void update_clock_settings(void)
{
    char timestamp[64];
    format_local_clock(timestamp, sizeof(timestamp));
    set_phone_text(clock_current, timestamp);
#if defined(RTCARPLAY_NETWORK_TIME)
    static const char * const messages[] = {
        [CARPLAY_TIME_IDLE] = "",
        [CARPLAY_TIME_SYNCING] = "Synchronizing network time...",
        [CARPLAY_TIME_SYNCED] = "Network time synchronized",
        [CARPLAY_TIME_NO_WAN] = "Connect Ethernet or Wi-Fi and try again.",
        [CARPLAY_TIME_NO_SERVER] = "Time server address could not be resolved.",
        [CARPLAY_TIME_NO_REPLY] = "Time server did not respond.",
        [CARPLAY_TIME_RTC_ERROR] = "Unable to set clock",
        [CARPLAY_TIME_UNAVAILABLE] = "Network time unavailable",
    };
    carplay_time_status_t status = carplay_network_time_status();
    const char *message = messages[status];
    if (strcmp(lv_label_get_text(clock_network_status), TR(message)))
        carplay_ui_set_text(clock_network_status, message);
    if (status == CARPLAY_TIME_SYNCING)
        lv_obj_add_state(clock_network_button, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(clock_network_button, LV_STATE_DISABLED);
    if (status == CARPLAY_TIME_SYNCING || carplay_app_clock_is_automatic())
        lv_obj_add_state(clock_apply, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(clock_apply, LV_STATE_DISABLED);
#endif
}

static void close_clock_keyboard(lv_event_t *event)
{
    (void)event;
    lv_keyboard_set_textarea(clock_keyboard, NULL);
    lv_obj_add_flag(clock_keyboard, LV_OBJ_FLAG_HIDDEN);
}

#if defined(RTCARPLAY_NETWORK_TIME)
static void sync_network_time(lv_event_t *event)
{
    (void)event;
    close_clock_keyboard(NULL);
    carplay_network_time_start();
    update_clock_settings();
}

static void clock_server_close(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(clock_server_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
}

static void clock_server_open(lv_event_t *event)
{
    (void)event;
    close_clock_keyboard(NULL);
    char server[CARPLAY_TIME_SERVER_MAX];
    carplay_network_time_server_get(server);
    lv_textarea_set_text(clock_server_entry, server);
    carplay_ui_set_text(clock_server_hint, "Enter a host name or IPv4 address.");
    show_modal(clock_server_panel);
}

static void clock_server_save(lv_event_t *event)
{
    int result = lv_event_get_user_data(event)
                     ? carplay_network_time_server_reset()
                     : carplay_network_time_server_set(lv_textarea_get_text(clock_server_entry));
    if (result != RT_EOK)
    {
        carplay_ui_set_text(clock_server_hint, result == -RT_EINVAL ? "Invalid server address." : "Could not save the time server.");
        return;
    }
    char server[CARPLAY_TIME_SERVER_MAX];
    carplay_network_time_server_get(server);
    lv_label_set_text(clock_server_value, server);
    clock_server_close(NULL);
}
#endif

static void load_clock_settings(void)
{
    struct tm local;
    char date[16] = "", time_text[16] = "", zone[12];
    if (read_local_clock(&local))
    {
        strftime(date, sizeof(date), "%Y-%m-%d", &local);
        strftime(time_text, sizeof(time_text), "%H:%M:%S", &local);
    }
    format_clock_zone(zone, sizeof(zone));
    lv_textarea_set_text(clock_date, date);
    lv_textarea_set_text(clock_time, time_text);
    lv_textarea_set_text(clock_zone, zone);
    bool automatic = carplay_app_clock_is_automatic();
    apply_segments(clock_mode_buttons, 2, automatic ? 0U : 1U);
    lv_obj_t *manual[] = { clock_date, clock_time, clock_zone, clock_apply };
    for (size_t index = 0; index < sizeof(manual) / sizeof(manual[0]); ++index)
    {
        if (automatic)
            lv_obj_add_state(manual[index], LV_STATE_DISABLED);
        else
            lv_obj_remove_state(manual[index], LV_STATE_DISABLED);
    }
    close_clock_keyboard(NULL);
    update_clock_settings();
}

static void select_clock_mode(lv_event_t *event)
{
    bool automatic = lv_event_get_user_data(event) == NULL;
    if (carplay_app_set_clock_automatic(automatic) != RTCP_OK)
    {
        settings_status("Unable to set clock", true);
        return;
    }
    settings_status("", false);
    load_clock_settings();
}

static void apply_manual_clock(lv_event_t *event)
{
    (void)event;
    const char *date = lv_textarea_get_text(clock_date);
    const char *time_text = lv_textarea_get_text(clock_time);
    const char *zone = lv_textarea_get_text(clock_zone);
    int year, month, day, hour, minute, second, zone_hour, zone_minute;
    int date_end = 0, time_end = 0, zone_end = 0;
    char sign;
    if (strlen(date) != 10 || strlen(time_text) != 8 || strlen(zone) != 6 ||
        sscanf(date, "%4d-%2d-%2d%n", &year, &month, &day, &date_end) != 3 || date_end != 10 ||
        sscanf(time_text, "%2d:%2d:%2d%n", &hour, &minute, &second, &time_end) != 3 || time_end != 8 ||
        sscanf(zone, "%c%2d:%2d%n", &sign, &zone_hour, &zone_minute, &zone_end) != 3 || zone_end != 6 ||
        year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59 ||
        (sign != '+' && sign != '-') || zone_hour < 0 || zone_hour > 14 || zone_minute < 0 || zone_minute > 59 ||
        (zone_hour == 14 && zone_minute != 0))
    {
        settings_status("Invalid date, time or UTC offset", true);
        return;
    }

    struct tm requested = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = hour, .tm_min = minute, .tm_sec = second };
    struct tm checked;
#if defined(_WIN32)
    time_t local_seconds = _mkgmtime(&requested);
    bool converted = local_seconds >= 0 && gmtime_s(&checked, &local_seconds) == 0;
#else
    time_t local_seconds = timegm(&requested);
    bool converted = local_seconds >= 0 && gmtime_r(&local_seconds, &checked) != NULL;
#endif
    if (!converted || checked.tm_year != year - 1900 ||
        checked.tm_mon != month - 1 || checked.tm_mday != day || checked.tm_hour != hour ||
        checked.tm_min != minute || checked.tm_sec != second)
    {
        settings_status("Invalid date, time or UTC offset", true);
        return;
    }

    int offset = (zone_hour * 60 + zone_minute) * (sign == '-' ? -1 : 1);
    if (carplay_app_set_local_clock((int64_t)local_seconds, offset) != RTCP_OK)
    {
        settings_status("Unable to set clock", true);
        return;
    }
    settings_status("Clock updated", false);
    load_clock_settings();
}

static void edit_clock_field(lv_event_t *event)
{
    lv_keyboard_set_textarea(clock_keyboard, lv_event_get_target_obj(event));
    lv_obj_remove_flag(clock_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *clock_field(lv_obj_t *parent, const char *label, int left, int width,
                             const char *placeholder, const char *characters, unsigned max_length)
{
    text(parent, label, left, 132, width, &carplay_ui_15, CARPLAY_STYLE_TEXT);
    lv_obj_t *entry = lv_textarea_create(parent);
    lv_obj_set_pos(entry, left, 160);
    lv_obj_set_size(entry, width, 40);
    carplay_ui_style(entry, CARPLAY_STYLE_INPUT);
    lv_textarea_set_one_line(entry, true);
    lv_textarea_set_accepted_chars(entry, characters);
    lv_textarea_set_max_length(entry, max_length);
    lv_textarea_set_placeholder_text(entry, placeholder);
    lv_obj_add_event_cb(entry, edit_clock_field, LV_EVENT_CLICKED, NULL);
    return entry;
}

static void build_clock_settings(lv_obj_t *parent)
{
    lv_obj_t *card = panel(parent, 0, 0, SETTINGS_DETAIL_WIDTH - 4, 223, 16, CARPLAY_STYLE_CARD);
    text(card, "Local time", 24, 16, 580, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    clock_current = text(card, "", 24, 40, 580, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    clock_mode_buttons[0] = segment(card, "Sync with iPhone", 24, 83, 172, 38,
                                    select_clock_mode, NULL, &carplay_ui_15);
    clock_mode_buttons[1] = segment(card, "Manual", 204, 83, 104, 38,
                                    select_clock_mode, (void *)1, &carplay_ui_15);
#if defined(RTCARPLAY_NETWORK_TIME)
    clock_network_button = button_text(card, "Sync via network", 332, 83, 266, 38, 9,
                                       CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                                       &carplay_ui_15, sync_network_time, NULL);
    clock_network_status = text(parent, "", 24, 238, 580, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    button_text(parent, "Time server", 24, 271, 174, 43, 10,
                CARPLAY_STYLE_CARD_ALT, CARPLAY_STYLE_TEXT, &carplay_ui_15, clock_server_open, NULL);
    char server[CARPLAY_TIME_SERVER_MAX];
    carplay_network_time_server_get(server);
    clock_server_value = text(parent, server, 218, 283, 376, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(clock_server_value, LV_LABEL_LONG_DOT);
#endif
    clock_date = clock_field(card, "Date", 24, 158, "YYYY-MM-DD", "0123456789-", 10);
    clock_time = clock_field(card, "Time", 194, 126, "HH:MM:SS", "0123456789:", 8);
    clock_zone = clock_field(card, "UTC offset", 332, 126, "+08:00", "0123456789+-:", 6);
    clock_apply = segment(card, "Apply", 482, 160, 116, 40, apply_manual_clock, NULL, &carplay_ui_15);

    static const char *keys[] = {
        "1",
        "2",
        "3",
        "-",
        "\n",
        "4",
        "5",
        "6",
        "+",
        "\n",
        "7",
        "8",
        "9",
        ":",
        "\n",
        LV_SYMBOL_CLOSE,
        "0",
        LV_SYMBOL_BACKSPACE,
        LV_SYMBOL_OK,
        "",
    };
    static const lv_buttonmatrix_ctrl_t controls[] = {
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
    };
    clock_keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(clock_keyboard, SETTINGS_DETAIL_WIDTH - 4, 176);
    lv_obj_align(clock_keyboard, LV_ALIGN_TOP_LEFT, 0, 236);
    carplay_ui_style(clock_keyboard, CARPLAY_STYLE_CARD);
    carplay_ui_style_part(clock_keyboard, CARPLAY_STYLE_INPUT, LV_PART_ITEMS);
    carplay_ui_style_part(clock_keyboard, CARPLAY_STYLE_ACCENT_CARD, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_font(clock_keyboard, LV_FONT_DEFAULT, LV_PART_ITEMS);
    lv_keyboard_set_map(clock_keyboard, LV_KEYBOARD_MODE_USER_1, keys, controls);
    lv_keyboard_set_mode(clock_keyboard, LV_KEYBOARD_MODE_USER_1);
    lv_obj_add_event_cb(clock_keyboard, close_clock_keyboard, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(clock_keyboard, close_clock_keyboard, LV_EVENT_CANCEL, NULL);
    close_clock_keyboard(NULL);
}
#endif

#if defined(CARPLAY_STA_SETTINGS)
static lv_obj_t *wifi_mode_button, *wifi_scan_button, *wifi_join_button, *wifi_password;
static lv_obj_t *wifi_keyboard, *wifi_results, *wifi_message, *wifi_ap_status;
static lv_obj_t *wifi_rows[CARPLAY_WIFI_SCAN_MAX];
static lv_obj_t *wifi_empty;
static unsigned wifi_scan_revision, wifi_shown_count, wifi_selected = UINT_MAX;

static void wifi_select_network(lv_event_t *event)
{
    wifi_selected = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    lv_textarea_set_text(wifi_password, "");
}

static void wifi_keyboard_close(lv_event_t *event)
{
    (void)event;
    if (wifi_keyboard && !lv_obj_has_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN))
    {
        lv_keyboard_set_textarea(wifi_keyboard, NULL);
        lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void wifi_keyboard_open(lv_event_t *event)
{
    (void)event;
    lv_keyboard_set_textarea(wifi_keyboard, wifi_password);
    lv_obj_remove_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_action(lv_event_t *event)
{
    carplay_wifi_status_t wifi;
    carplay_wifi_status(&wifi);
    unsigned action = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    int result;
    switch (action)
    {
    case 0:
        result = carplay_wifi_enable(!wifi.enabled);
        break;
    case 1:
        result = carplay_wifi_scan();
        break;
    case 2:
        result = wifi_selected < wifi_shown_count
                     ? carplay_wifi_join(wifi_scan_revision, wifi_selected,
                                         lv_textarea_get_text(wifi_password))
                     : -RT_EINVAL;
        lv_textarea_set_text(wifi_password, "");
        wifi_keyboard_close(NULL);
        break;
    default:
        result = carplay_wifi_forget();
        break;
    }
    settings_status(result == -RT_EBUSY ? "Wi-Fi operation in progress." : result ? "Check Wi-Fi mode, network and password."
                                                                                  : "",
                    result != RT_EOK);
}

static void wifi_update(const carplay_wifi_status_t *wifi)
{
    const char *placeholder = TR("Wi-Fi password");
    if (strcmp(lv_textarea_get_placeholder_text(wifi_password), placeholder))
        lv_textarea_set_placeholder_text(wifi_password, placeholder);
    set_text_if_changed(wifi_ap_status,
                        wifi->ap_active ? "CarPlay access point active" : "CarPlay access point inactive");
    set_text_if_changed(lv_obj_get_child(wifi_mode_button, 0),
                        wifi->enabled ? "Leave STA maintenance" : "Pause CarPlay and enable Wi-Fi");
    if (wifi->busy)
        lv_obj_add_state(wifi_mode_button, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(wifi_mode_button, LV_STATE_DISABLED);
    if (wifi->busy || !wifi->enabled)
        lv_obj_add_state(wifi_scan_button, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(wifi_scan_button, LV_STATE_DISABLED);
    unsigned count = wifi->enabled ? LV_MIN(wifi->count, CARPLAY_WIFI_SCAN_MAX) : 0;
    if (wifi_shown_count != count || wifi_scan_revision != wifi->scan_revision)
    {
        wifi_selected = UINT_MAX;
        lv_textarea_set_text(wifi_password, "");
        wifi_scan_revision = wifi->scan_revision;
        wifi_shown_count = count;
    }
    if (count)
        lv_obj_add_flag(wifi_empty, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(wifi_empty, LV_OBJ_FLAG_HIDDEN);
    for (unsigned index = 0; index < CARPLAY_WIFI_SCAN_MAX; ++index)
    {
        lv_obj_t *row = wifi_rows[index];
        if (index >= count)
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
        if (index < count)
        {
            const carplay_wifi_access_point_t *ap = &wifi->results[index];
            bool connected = wifi->connected && !strcmp(wifi->connected_ssid, ap->ssid);
            char title[128];
            const char *signal = ap->rssi >= -55 ? "||||" : ap->rssi >= -70 ? "|||"
                                                        : ap->rssi >= -80   ? "||"
                                                                            : "|";
            snprintf(title, sizeof(title), "%s  %s\n%s%s%s%s%s", signal, ap->ssid,
                     ap->security, ap->channel > 0 ? " · " : "",
                     ap->channel > 14 ? "5 GHz" : ap->channel > 0 ? "2.4 GHz"
                                                                  : "",
                     connected ? " · " : "", connected ? TR("Connected") : "");
            set_phone_text(lv_obj_get_child(row, 0), title);
            lv_obj_set_state(row, LV_STATE_CHECKED, index == wifi_selected);
        }
    }
    lv_obj_set_state(wifi_join_button, LV_STATE_DISABLED, !wifi->enabled || wifi->busy || wifi_selected >= count);
    char message[128];
    if (wifi->connected)
        snprintf(message, sizeof(message), "%s  %s", wifi->connected_ssid,
                 TR(wifi->addressed ? "IPv4 ready" : "Waiting for DHCP"));
    else
        snprintf(message, sizeof(message), "%s", TR(wifi->message));
    set_text_if_changed(wifi_message, wifi->busy ? "Wi-Fi operation in progress..." : message);
}

static void build_wifi_settings(lv_obj_t *page)
{
    wifi_mode_button = button_text(page, "Pause CarPlay and enable Wi-Fi", 0, 0, 370, 56, 8,
                                   CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                                   &carplay_ui_18, wifi_action, NULL);
    wifi_scan_button = button_text(page, "Scan", 386, 0, 358, 56, 8, CARPLAY_STYLE_CARD,
                                   CARPLAY_STYLE_TEXT, &carplay_ui_18, wifi_action, (void *)1);
    wifi_message = text(page, "", 0, 64, 370, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(wifi_message, LV_LABEL_LONG_WRAP);
    wifi_results = carplay_settings_list(page, 358, 311);
    lv_obj_set_pos(wifi_results, 386, 66);
    wifi_empty = text(wifi_results, "No scan results", 0, 0, 290, &carplay_ui_18, CARPLAY_STYLE_MUTED);
    for (unsigned index = 0; index < CARPLAY_WIFI_SCAN_MAX; ++index)
    {
        wifi_rows[index] = carplay_settings_row(wifi_results, "", NULL, false, wifi_select_network,
                                                (void *)(uintptr_t)index);
        lv_obj_t *label = lv_obj_get_child(wifi_rows[index], 0);
        lv_obj_set_style_text_font(label, &carplay_cjk_full_18, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_add_flag(wifi_rows[index], LV_OBJ_FLAG_HIDDEN);
    }
    wifi_password = lv_textarea_create(page);
    lv_obj_set_pos(wifi_password, 0, 125);
    lv_obj_set_size(wifi_password, 370, 46);
    lv_textarea_set_one_line(wifi_password, true);
    lv_textarea_set_password_mode(wifi_password, true);
    lv_textarea_set_password_show_time(wifi_password, 0);
    lv_textarea_set_max_length(wifi_password, 63);
    lv_obj_set_style_text_font(wifi_password, &carplay_ui_18, LV_PART_MAIN);
    lv_textarea_set_placeholder_text(wifi_password, TR("Wi-Fi password"));
    carplay_ui_style(wifi_password, CARPLAY_STYLE_INPUT);
    carplay_ui_style_part(wifi_password, CARPLAY_STYLE_MUTED, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(wifi_password, wifi_keyboard_open, LV_EVENT_CLICKED, NULL);
    wifi_join_button = button_text(page, "Connect", 0, 180, 175, 51, 8, CARPLAY_STYLE_ACCENT_CARD,
                                   CARPLAY_STYLE_ACCENT_TEXT, &carplay_ui_18, wifi_action, (void *)2);
    wifi_ap_status = text(page, "", 0, 240, 370, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    button_text(page, "Forget network", 187, 180, 183, 51, 8, CARPLAY_STYLE_CARD,
                CARPLAY_STYLE_TEXT, &carplay_ui_18, wifi_action, (void *)3);
    wifi_keyboard = build_text_keyboard(page, 0, 205, 744, 180);
    lv_obj_add_event_cb(wifi_keyboard, wifi_keyboard_close, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wifi_keyboard, wifi_keyboard_close, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(wifi_password, wifi_keyboard_close, LV_EVENT_READY, NULL);
    wifi_keyboard_close(NULL);
}
#endif

#if defined(RTCARPLAY_INTERNET_SHARE)
static void build_ethernet_settings(lv_obj_t *page)
{
    static const char * const captions[] = {
        "Connection status", "IPv4 address", "Subnet mask", "Default gateway", "System DNS"
    };
    lv_obj_t *card = panel(page, 0, 0, SETTINGS_DETAIL_WIDTH - 4, 280, 16, CARPLAY_STYLE_CARD);
    for (unsigned index = 0; index < sizeof(captions) / sizeof(captions[0]); ++index)
    {
        int top = 23 + (int)index * 52;
        text(card, captions[index], 24, top, 242, &carplay_ui_18, CARPLAY_STYLE_MUTED);
        ethernet_values[index] = text(card, "--", 270, top, 334, &carplay_ui_18, CARPLAY_STYLE_TEXT);
        lv_obj_set_style_text_align(ethernet_values[index], LV_TEXT_ALIGN_RIGHT, 0);
        if (index + 1 < sizeof(captions) / sizeof(captions[0]))
            divider(card, 24, top + 34, 580, 1);
    }
}

static void ethernet_update(const carplay_internet_status_t *network)
{
    const char *state = "Network unavailable";
    if (network->available && network->device)
        state = !network->link       ? "Cable disconnected"
                : network->addressed ? "Connected"
                                     : "Waiting for IPv4";
    if (strcmp(lv_label_get_text(ethernet_values[0]), TR(state)) != 0)
        carplay_ui_set_text(ethernet_values[0], state);

    const char * const addresses[] = { network->address, network->mask, network->gateway, network->dns };
    for (unsigned index = 0; index < sizeof(addresses) / sizeof(addresses[0]); ++index)
    {
        const char *value = addresses[index];
        set_text_if_changed(ethernet_values[index + 1],
                            network->addressed && *value && strcmp(value, "0.0.0.0") ? value : "--");
    }
}
#endif

static void build_settings(lv_obj_t *parent)
{
    static const struct
    {
        settings_page_t route;
        icon_asset_t icon;
    } tabs[] = {
        { SETTINGS_CONNECTIONS, &carplay_icon_link_24 },
        { SETTINGS_DISPLAY, &carplay_icon_sun_29 },
        { SETTINGS_SOUND, &carplay_icon_volume_29 },
        { SETTINGS_GENERAL, &carplay_icon_languages_24 },
        { SETTINGS_SYSTEM, &carplay_icon_info_26 },
#if defined(RTCARPLAY_DEMO_DEVELOPER_OPTIONS)
        { SETTINGS_DEVELOPER, &carplay_icon_settings_24 },
#endif
    };
    lv_obj_t *sidebar = carplay_settings_frame_build(parent, &settings_heading, settings_back);
    for (unsigned index = 0; index < sizeof(tabs) / sizeof(tabs[0]); ++index)
    {
        settings_page_t route = tabs[index].route;
        settings_tabs[route] = carplay_settings_tab(sidebar, settings_titles[route],
                                                    tabs[index].icon, select_settings_page,
                                                    (void *)(uintptr_t)route);
    }
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
        settings_pages[index] = carplay_settings_detail_create(parent);

    lv_obj_t *sections[SETTINGS_PAGE_COUNT] = {
        [SETTINGS_CONNECTIONS] = carplay_settings_list(settings_pages[SETTINGS_CONNECTIONS],
                                                       SETTINGS_DETAIL_WIDTH - 4, LV_SIZE_CONTENT),
        [SETTINGS_GENERAL] = carplay_settings_list(settings_pages[SETTINGS_GENERAL],
                                                   SETTINGS_DETAIL_WIDTH - 4, LV_SIZE_CONTENT),
        [SETTINGS_SYSTEM] = carplay_settings_list(settings_pages[SETTINGS_SYSTEM],
                                                  SETTINGS_DETAIL_WIDTH - 4, LV_SIZE_CONTENT),
    };
#if defined(RTCARPLAY_DEMO_DEVELOPER_OPTIONS)
    build_frame_rate(settings_pages[SETTINGS_DEVELOPER]);
#if defined(RTCARPLAY_IAP2)
    sections[SETTINGS_DEVELOPER] = carplay_settings_list(settings_pages[SETTINGS_DEVELOPER],
                                                         SETTINGS_DETAIL_WIDTH - 4, LV_SIZE_CONTENT);
    lv_obj_set_y(sections[SETTINGS_DEVELOPER], 102);
#endif
#endif
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
    {
        settings_page_t section = settings_section((settings_page_t)index);
        if ((unsigned)section != index && sections[section])
            carplay_settings_row(sections[section], settings_titles[index], NULL, true,
                                 select_settings_page, (void *)(uintptr_t)index);
    }
    carplay_settings_row(sections[SETTINGS_SYSTEM], "System update", &carplay_icon_download_26,
                         true, open_ota, NULL);

    build_display_settings(settings_pages[SETTINGS_DISPLAY]);
    build_slider_card(settings_pages[SETTINGS_SOUND], 0, "Media volume", &carplay_icon_volume_29,
                      &volume_value, &volume_slider, 0, CARPLAY_PREF_VOLUME_MAX, volume_event,
                      "Adjust local audio output");
#if defined(RTCARPLAY_MICROPHONE)
    lv_obj_t *sound = carplay_settings_list(settings_pages[SETTINGS_SOUND],
                                            SETTINGS_DETAIL_WIDTH - 4, LV_SIZE_CONTENT);
    lv_obj_set_y(sound, 142);
    microphone_button = carplay_settings_row(sound, "Mute microphone", NULL, false,
                                             select_microphone, NULL);
    microphone_caption = lv_obj_get_child(microphone_button, 0);
    update_microphone();
#endif
    build_language_settings(settings_pages[SETTINGS_LANGUAGE]);
#if defined(RT_USING_RTC)
    build_clock_settings(settings_pages[SETTINGS_CLOCK]);
#endif
    build_about(settings_pages[SETTINGS_ABOUT]);
#if defined(CARPLAY_BT_NAME_SETTINGS)
    build_bluetooth_settings(settings_pages[SETTINGS_BLUETOOTH]);
#endif
#if defined(RTCARPLAY_IAP2)
    build_gnss_settings(settings_pages[SETTINGS_GNSS]);
#endif
#if defined(CARPLAY_STA_SETTINGS)
    build_wifi_settings(settings_pages[SETTINGS_WIFI]);
#endif
#if defined(RTCARPLAY_INTERNET_SHARE)
    build_ethernet_settings(settings_pages[SETTINGS_ETHERNET]);
#endif
    settings_hint = text(parent, "", SETTINGS_DETAIL_X, 461, SETTINGS_DETAIL_WIDTH,
                         &carplay_ui_14, CARPLAY_STYLE_FAINT);
    show_settings_page(SETTINGS_DISPLAY);
}

/* ---------------- OTA ---------------- */

#if defined(RTCARPLAY_OTA)
static void ota_source_open(lv_event_t *event);

static void ota_action(lv_event_t *event)
{
    (void)event;
    carplay_ota_status_t ota;
    carplay_ota_status(&ota);
    if (ota.state == CARPLAY_OTA_DOWNLOADING)
    {
        (void)carplay_ota_cancel();
        return;
    }
    if (ota.busy)
        return;
    if (ota.state != CARPLAY_OTA_READY_TO_INSTALL)
    {
        if (ota.source[0])
            (void)carplay_ota_download();
        else
            ota_source_open(NULL);
        return;
    }
    ota_confirm_candidate = ota.candidate;
    char body[192];
    snprintf(body, sizeof(body), TR("Version %s"), ota.remote);
    carplay_ui_set_text_fmt(
        ota_confirm_text, "%s\n\n%s", body,
        TR("Disconnect CarPlay and keep power connected before installing."));
    show_modal(ota_confirm_panel);
}

static void ota_confirm_install(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
    if (carplay_ota_install(ota_confirm_candidate) != RT_EOK)
        carplay_ui_set_text(ota_state, "Update changed. Retry.");
}

static void ota_confirm_cancel(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
}

static void ota_source_close(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(ota_source_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
}

static void ota_source_open(lv_event_t *event)
{
    (void)event;
    carplay_ota_status_t ota;
    carplay_ota_status(&ota);
    lv_textarea_set_text(ota_source_entry, ota.source);
    carplay_ui_set_text(ota_source_hint, "Enter an HTTP or HTTPS URL to an OTA .cpio file.");
    show_modal(ota_source_panel);
}

static void ota_source_save(lv_event_t *event)
{
    (void)event;
    int result = carplay_ota_source_set(lv_textarea_get_text(ota_source_entry));
    if (result == RT_EOK)
        ota_source_close(NULL);
    else
        carplay_ui_set_text(ota_source_hint, result == -RT_EBUSY ? "Update worker is busy." : "Invalid firmware URL.");
}

static const char *ota_state_text(const carplay_ota_status_t *ota, char *buffer, size_t size)
{
    switch (ota->state)
    {
    case CARPLAY_OTA_VERIFYING:
        return TR("Verifying update");
    case CARPLAY_OTA_READY_TO_INSTALL:
        return TR("Ready to install");
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
    case CARPLAY_OTA_TRIAL_BOOT:
        return "Trial boot pending";
    case CARPLAY_OTA_CONFIRMED:
        return "Boot confirmed";
    case CARPLAY_OTA_FAILED:
        return TR("Update failed");
    default:
        return TR("Ready to download");
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

    if (!ota.busy || ota.state == CARPLAY_OTA_DOWNLOADING)
        lv_obj_remove_state(ota_update_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(ota_update_button, LV_STATE_DISABLED);
    const char *action = "Download update";
    if (ota.state == CARPLAY_OTA_DOWNLOADING)
        action = "Cancel download";
    else if (ota.state == CARPLAY_OTA_READY_TO_INSTALL)
        action = "Install update";
    else if (!ota.source[0])
        action = "Enter firmware URL";
    set_text_if_changed(ota_action_label, action);
    set_text_if_changed(ota_url, ota.source);
    set_text_if_changed(ota_backend, ota.message);
}
#else
static void ota_update(void)
{
    if (!ota_state)
        return;
    set_text_if_changed(ota_state, "Update backend unavailable");
    lv_obj_add_state(ota_update_button, LV_STATE_DISABLED);
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
    ota_state = text(parent, "Ready to download", 97, 354, 290, &carplay_ui_21,
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
    ota_url = text(parent, "", 193, 302, 700, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(ota_url, LV_LABEL_LONG_SCROLL_CIRCULAR);
    ota_update_button = button_text(parent, "Enter firmware URL", 667, 351, 258, 59, 14,
                                    CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                                    &carplay_ui_18, ota_action, NULL);
    ota_action_label = lv_obj_get_child(ota_update_button, 0);
#else
    ota_update_button = button_text(parent, "Download update", 667, 351, 258, 59, 14,
                                    CARPLAY_STYLE_ACCENT_CARD, CARPLAY_STYLE_ACCENT_TEXT,
                                    &carplay_ui_18, NULL, NULL);
#endif
    lv_obj_add_state(ota_update_button, LV_STATE_DISABLED);

    panel(parent, 64, 107, 896, 40, 8, CARPLAY_STYLE_CARD_ALT);
    icon_mask(parent, &carplay_icon_info_26, 84, 114, CARPLAY_STYLE_ICON_FAINT);
    text(parent, "Update status", 123, 117, 190, &carplay_ui_17,
         CARPLAY_STYLE_TEXT);
    ota_backend = text(parent, "Update backend available", 321, 117, 374, &carplay_ui_14,
                       CARPLAY_STYLE_MUTED);
#if defined(RTCARPLAY_OTA)
    lv_label_set_long_mode(ota_backend, LV_LABEL_LONG_DOT);
    button_text(parent, "Firmware URL", 718, 438, 213, 44, 8, CARPLAY_STYLE_ACCENT_CARD,
                CARPLAY_STYLE_ACCENT_TEXT, &carplay_ui_18, ota_source_open, NULL);
#endif
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
    ota_source_panel = panel(screen, 80, 78, 864, 446, 24, CARPLAY_STYLE_CARD);
    text(ota_source_panel, "Firmware URL", 24, 20, 810, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    ota_source_hint = text(ota_source_panel, "", 24, 62, 810, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    ota_source_entry = lv_textarea_create(ota_source_panel);
    lv_obj_set_pos(ota_source_entry, 24, 102);
    lv_obj_set_size(ota_source_entry, 816, 55);
    lv_textarea_set_one_line(ota_source_entry, true);
    lv_textarea_set_max_length(ota_source_entry, CARPLAY_OTA_SOURCE_MAX - 1);
    carplay_ui_style(ota_source_entry, CARPLAY_STYLE_INPUT);
    lv_obj_t *source_keyboard = build_text_keyboard(ota_source_panel, 24, 171, 816, 194);
    lv_keyboard_set_textarea(source_keyboard, ota_source_entry);
    button_text(ota_source_panel, "Cancel", 24, 381, 215, 50, 14, CARPLAY_STYLE_CARD_ALT,
                CARPLAY_STYLE_TEXT, &carplay_ui_18, ota_source_close, NULL);
    button_text(ota_source_panel, "Save", 534, 381, 306, 50, 14, CARPLAY_STYLE_ACCENT_CARD,
                CARPLAY_STYLE_ACCENT_TEXT, &carplay_ui_18, ota_source_save, NULL);
    lv_obj_add_flag(ota_source_panel, LV_OBJ_FLAG_HIDDEN);
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

#if defined(RTCARPLAY_NETWORK_TIME)
    clock_server_panel = panel(screen, 80, 78, 864, 446, 24, CARPLAY_STYLE_CARD);
    text(clock_server_panel, "Time server", 24, 20, 810, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    clock_server_hint = text(clock_server_panel, "", 24, 62, 810, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    clock_server_entry = lv_textarea_create(clock_server_panel);
    lv_obj_set_pos(clock_server_entry, 24, 102);
    lv_obj_set_size(clock_server_entry, 816, 55);
    lv_textarea_set_one_line(clock_server_entry, true);
    lv_textarea_set_max_length(clock_server_entry, CARPLAY_TIME_SERVER_MAX - 1);
    carplay_ui_style(clock_server_entry, CARPLAY_STYLE_INPUT);
    lv_obj_t *server_keyboard = build_text_keyboard(clock_server_panel, 24, 171, 816, 194);
    lv_keyboard_set_textarea(server_keyboard, clock_server_entry);
    button_text(clock_server_panel, "Cancel", 24, 381, 215, 50, 14, CARPLAY_STYLE_CARD_ALT,
                CARPLAY_STYLE_TEXT, &carplay_ui_18, clock_server_close, NULL);
    button_text(clock_server_panel, "Restore default", 254, 381, 265, 50, 14, CARPLAY_STYLE_CARD_ALT,
                CARPLAY_STYLE_TEXT, &carplay_ui_18, clock_server_save, clock_server_panel);
    button_text(clock_server_panel, "Save", 534, 381, 306, 50, 14, CARPLAY_STYLE_ACCENT_CARD,
                CARPLAY_STYLE_ACCENT_TEXT, &carplay_ui_18, clock_server_save, NULL);
    lv_obj_add_flag(clock_server_panel, LV_OBJ_FLAG_HIDDEN);
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
                carplay_ui_set_text_fmt(volume_value, "%u%%", actual);
            }
        }
        else if (!lv_obj_has_state(volume_slider, LV_STATE_DISABLED))
        {
            lv_obj_add_state(volume_slider, LV_STATE_DISABLED);
            /* Short form: at 117 px it is the widest unsupported state that
             * fits the 136 px value slot in both languages (zh 54 px). */
            carplay_ui_set_text(volume_value, "Not supported");
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
                carplay_ui_set_text_fmt(brightness_value, "%u%%", actual);
            }
        }
        else if (!lv_obj_has_state(brightness_slider, LV_STATE_DISABLED))
        {
            lv_obj_add_state(brightness_slider, LV_STATE_DISABLED);
            carplay_ui_set_text(brightness_value, "Not supported");
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

    carplay_instruments_update(&dashboard.instruments, valid, speed, soc);

    static bool simulated_shown = true, simulated_known;
    if (!simulated_known || simulated != simulated_shown)
    {
        if (simulated)
            lv_obj_remove_flag(dashboard.note, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(dashboard.note, LV_OBJ_FLAG_HIDDEN);
        simulated_shown = simulated;
        simulated_known = true;
    }

    char summary[260];
    const char *state = TR(carplay_connectivity_state(have_status ? status : NULL));
    if (have_status && status->stage == DEMO_LIVE)
        snprintf(summary, sizeof(summary), "%s · %s", state, TR(status->wireless ? "Wireless" : "USB"));
    else
        snprintf(summary, sizeof(summary), "%s", state);
    set_phone_text(dashboard.connection, summary);
    const carplay_phone_track_t *track = &phone_model.track;
    set_phone_text(dashboard.media, track->title[0] ? track->title : TR("No media"));
    set_phone_text(dashboard.artist, track->artist);
    set_text_if_changed(dashboard.playback, track->has_playing ? (track->playing ? "Playing" : "Not playing") : "");
    const char *instruction = phone_model.maneuvers[0].present
                                  ? phone_model.maneuvers[0].instruction
                                  : "";
    const char *destination = instruction[0]              ? instruction
                              : phone_model.route_road[0] ? phone_model.route_road
                                                          : phone_model.route_destination;
    snprintf(summary, sizeof(summary), "%s%s%s",
             destination[0] ? destination : TR("No active route"),
             destination[0] && phone_model.route_turn_distance[0] ? " · " : "",
             destination[0] ? phone_model.route_turn_distance : "");
    set_phone_text(dashboard.route, summary);
}

static void update_status_bar(const demo_status_t *status, bool have_status)
{
    bool connected = have_status && status->stage == DEMO_LIVE;
    static bool shown_connected, connected_known;
    if (!connected_known || connected != shown_connected)
    {
        carplay_ui_style(dash_dot, connected ? CARPLAY_STYLE_DOT_ON : CARPLAY_STYLE_DOT_OFF);
        carplay_ui_set_text(dash_status, connected ? "Connected" : "Not connected");
        layout_dashboard_status();
        shown_connected = connected;
        connected_known = true;
    }

    char clock[16];
    clock_text(clock, sizeof(clock));
    set_text_if_changed(dash_clock, clock);
    carplay_ui_set_text(status_bluetooth, have_status && status->bt_connected ? "Bluetooth connected"
                                          : have_status && status->bt_ready   ? "Bluetooth ready"
                                                                              : "");
    char battery[80] = "";
    if (phone_model.has_battery && phone_model.battery <= 100)
        snprintf(battery, sizeof(battery), "%s %u%%%s%s", TR("Phone battery"),
                 (unsigned)phone_model.battery,
                 phone_model.has_charging && phone_model.charging == 1 ? " · " : "",
                 phone_model.has_charging && phone_model.charging == 1 ? TR("Charging") : "");
    set_text_if_changed(status_phone, battery);
    /* Language switches relabel the status without going through the
     * connected/not-connected branch above, so re-check the row every tick;
     * the helper only writes when the measured geometry changed. */
    layout_dashboard_status();
}

#if LV_USE_LODEPNG || LV_USE_TJPGD
#define PHONE_COVER_SIDE 96U

#if LV_USE_TJPGD
typedef struct
{
    const uint8_t *bytes;
    size_t size, offset;
    uint16_t *pixels;
    uint32_t width, height, stride;
} phone_cover_jpeg_t;

static size_t phone_cover_jpeg_read(JDEC *decoder, uint8_t *output, size_t length)
{
    phone_cover_jpeg_t *image = decoder->device;
    if (length > image->size - image->offset)
        length = image->size - image->offset;
    if (output)
        memcpy(output, image->bytes + image->offset, length);
    image->offset += length;
    return length;
}

static int phone_cover_jpeg_block(JDEC *decoder, void *bitmap, JRECT *rect)
{
    phone_cover_jpeg_t *image = decoder->device;
    uint32_t cols = rect->right - rect->left + 1U;
    uint32_t first_row = (rect->top * image->height + decoder->height - 1U) / decoder->height;
    uint32_t end_row = ((rect->bottom + 1U) * image->height + decoder->height - 1U) / decoder->height;
    uint32_t first_col = (rect->left * image->width + decoder->width - 1U) / decoder->width;
    uint32_t end_col = ((rect->right + 1U) * image->width + decoder->width - 1U) / decoder->width;
    for (uint32_t row = first_row; row < end_row; ++row)
    {
        uint32_t source_row = row * decoder->height / image->height - rect->top;
        for (uint32_t col = first_col; col < end_col; ++col)
        {
            uint32_t source_col = col * decoder->width / image->width - rect->left;
            uint32_t index = source_row * cols + source_col;
#if JD_FORMAT == 1
            uint16_t rgb565 = ((const uint16_t *)bitmap)[index];
#elif JD_FORMAT == 2
            uint8_t gray = ((const uint8_t *)bitmap)[index];
            uint16_t rgb565 = (uint16_t)(((gray & 0xf8U) << 8) | ((gray & 0xfcU) << 3) | (gray >> 3));
#else
            const uint8_t *bgr = (const uint8_t *)bitmap + 3U * index;
            uint16_t rgb565 = (uint16_t)(((bgr[2] & 0xf8U) << 8) | ((bgr[1] & 0xfcU) << 3) | (bgr[0] >> 3));
#endif
            image->pixels[row * image->stride + col] = rgb565;
        }
    }
    return 1;
}

static bool phone_cover_jpeg(const uint8_t *bytes, size_t size, lv_image_dsc_t *descriptor)
{
    static _Alignas(LV_DRAW_BUF_ALIGN) uint16_t pixels[PHONE_COVER_SIDE * PHONE_COVER_SIDE];
    static _Alignas(4) uint8_t workspace[4096];
    phone_cover_jpeg_t image = { .bytes = bytes, .size = size, .pixels = pixels };
    JDEC decoder;
    JRESULT result = jd_prepare(&decoder, phone_cover_jpeg_read, workspace, sizeof(workspace), &image);
    if (result != JDR_OK || !decoder.width || !decoder.height ||
        decoder.width > 2048U || decoder.height > 2048U)
    {
        rt_kprintf("[carplay-cover] JPEG header rejected bytes=%u code=%d\n", (unsigned)size, (int)result);
        return false;
    }
    uint32_t longest = decoder.width > decoder.height ? decoder.width : decoder.height;
    uint32_t divisor = longest > PHONE_COVER_SIDE ? longest : PHONE_COVER_SIDE;
    image.width = LV_MAX(1U, decoder.width * PHONE_COVER_SIDE / divisor);
    image.height = LV_MAX(1U, decoder.height * PHONE_COVER_SIDE / divisor);
    image.stride = PHONE_COVER_SIDE;
    result = jd_decomp(&decoder, phone_cover_jpeg_block, 0);
    if (result != JDR_OK)
    {
        rt_kprintf("[carplay-cover] JPEG decode failed %ux%u code=%d\n",
                   (unsigned)decoder.width, (unsigned)decoder.height, (int)result);
        return false;
    }
    *descriptor = (lv_image_dsc_t){
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = image.width, .h = image.height, .stride = image.stride * sizeof(uint16_t) },
        .data_size = image.stride * image.height * sizeof(uint16_t),
        .data = (const uint8_t *)pixels,
    };
    rt_kprintf("[carplay-cover] JPEG decoded %ux%u -> %ux%u\n",
               (unsigned)decoder.width, (unsigned)decoder.height,
               (unsigned)image.width, (unsigned)image.height);
    return true;
}
#endif

#if LV_USE_LODEPNG
static bool cover_png_dimensions(const uint8_t *bytes, size_t size, uint32_t *width, uint32_t *height)
{
    if (size >= 33 && !memcmp(bytes, "\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16))
    {
        *width = ((uint32_t)bytes[16] << 24) | ((uint32_t)bytes[17] << 16) | ((uint32_t)bytes[18] << 8) | bytes[19];
        *height = ((uint32_t)bytes[20] << 24) | ((uint32_t)bytes[21] << 16) | ((uint32_t)bytes[22] << 8) | bytes[23];
        return true;
    }
    return false;
}
#endif

static void show_phone_cover(lv_obj_t *image, lv_obj_t *placeholder,
                             const lv_image_dsc_t *descriptor, unsigned side)
{
    if (!descriptor)
    {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_image_set_src(image, descriptor);
    uint32_t longest = LV_MAX(descriptor->header.w, descriptor->header.h);
    lv_image_set_scale(image, longest > side ? side * LV_SCALE_NONE / longest : LV_SCALE_NONE);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
}

static void update_phone_cover(uint32_t revision)
{
    static _Alignas(4) uint8_t bytes[CARPLAY_PHONE_ARTWORK_MAX];
    static lv_image_dsc_t descriptor;
    static uint32_t shown;
    if (revision == shown)
        return;
    size_t size = revision ? carplay_app_phone_artwork(revision, bytes, sizeof(bytes)) : 0;
    if (revision && !size)
        return;
    lv_image_cache_drop(&descriptor);
    show_phone_cover(phone_hub.cover, phone_hub.cover_placeholder, NULL, PHONE_COVER_SIDE);
    show_phone_cover(dashboard.cover, dashboard.cover_placeholder, NULL, 64);
    shown = 0;
    if (!revision)
        return;

    shown = revision;
    bool ready = false;
#if LV_USE_TJPGD
    if (size > 2U && bytes[0] == 0xff && bytes[1] == 0xd8)
        ready = phone_cover_jpeg(bytes, size, &descriptor);
#endif
#if LV_USE_LODEPNG
    uint32_t width, height;
    if (!ready && cover_png_dimensions(bytes, size, &width, &height) &&
        width && height && width <= 512U && height <= 512U)
    {
        descriptor = (lv_image_dsc_t){
            .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW, .w = width, .h = height },
            .data_size = (uint32_t)size,
            .data = bytes,
        };
        lv_image_decoder_dsc_t decoded;
        if (lv_image_decoder_open(&decoded, &descriptor, NULL) == LV_RESULT_OK)
        {
            lv_image_decoder_close(&decoded);
            ready = true;
        }
    }
#endif
    if (!ready)
    {
        rt_kprintf("[carplay-cover] no thumbnail bytes=%u signature=%02x:%02x\n",
                   (unsigned)size, bytes[0], size > 1U ? bytes[1] : 0U);
        return;
    }
    show_phone_cover(phone_hub.cover, phone_hub.cover_placeholder, &descriptor, PHONE_COVER_SIDE);
    show_phone_cover(dashboard.cover, dashboard.cover_placeholder, &descriptor, 64);
}
#endif

static void update_phone_model(const demo_status_t *status)
{
    static bool active, wireless;
    static uint64_t generation;
    static carplay_phone_media_t snapshot;
    bool live = status && status->stage == DEMO_LIVE;
    if (active && (!live || wireless != status->wireless || generation != status->receiver.generation))
    {
        memset(&phone_model, 0, sizeof(phone_model));
#if LV_USE_LODEPNG || LV_USE_TJPGD
        update_phone_cover(0);
#endif
    }
    active = live;
    if (live)
    {
        wireless = status->wireless;
        generation = status->receiver.generation;
        if (carplay_app_phone_media(&snapshot))
            phone_model = snapshot;
    }
#if LV_USE_LODEPNG || LV_USE_TJPGD
    if (current_page == PAGE_DASHBOARD || current_page == PAGE_CONNECTIVITY)
        update_phone_cover(phone_model.track.artwork_revision);
#endif
}

static void format_elapsed(char *text, size_t size, bool valid, uint32_t milliseconds)
{
    unsigned seconds = milliseconds / 1000U;
    if (!valid)
        snprintf(text, size, "--:--");
    else if (seconds >= 3600)
        snprintf(text, size, "%u:%02u:%02u", seconds / 3600U, seconds / 60U % 60U, seconds % 60U);
    else
        snprintf(text, size, "%u:%02u", seconds / 60U, seconds % 60U);
}

static void update_phone_hub(const demo_status_t *status)
{
    const carplay_phone_media_t *media = &phone_model;
    const carplay_phone_track_t *track = &media->track;
    char detail[300];
    set_phone_text(phone_hub.title, track->title[0] ? track->title : "-");
    snprintf(detail, sizeof(detail), "%s%s%s", track->artist,
             track->artist[0] && track->album[0] ? " / " : "", track->album);
    set_phone_text(phone_hub.artist, detail);
    set_phone_text(phone_hub.playback, track->has_playing ? TR(track->playing ? "Playing" : "Not playing") : "");
    uint32_t duration = track->duration_ms;
    uint32_t elapsed = track->has_duration && track->elapsed_ms > duration ? duration : track->elapsed_ms;
    lv_bar_set_value(phone_hub.progress, track->has_duration && track->has_elapsed && duration ? (int32_t)((uint64_t)elapsed * 1000U / duration) : 0, LV_ANIM_OFF);
    char position[16], length[16];
    format_elapsed(position, sizeof(position), track->has_elapsed, elapsed);
    format_elapsed(length, sizeof(length), track->has_duration, duration);
    snprintf(detail, sizeof(detail), "%s / %s", position, length);
    set_phone_text(phone_hub.position, track->has_elapsed || track->has_duration ? detail : "");
    set_phone_text(phone_hub.media_caption, TR("Now Playing"));
    set_phone_text(phone_hub.route_caption, TR("Route guidance"));
    set_phone_text(phone_status_caption, TR("Phone status"));
    set_phone_text(phone_hub.destination, media->route_destination[0] ? media->route_destination : "-");
    set_phone_text(phone_hub.road, media->route_road);
    set_phone_text(phone_hub.remaining, media->route_remaining);
    set_phone_text(phone_hub.turn_distance, media->route_turn_distance);
    for (unsigned row = 0; row < CARPLAY_PHONE_MANEUVER_ROWS; ++row)
    {
        const carplay_phone_maneuver_t *maneuver = &media->maneuvers[row];
        snprintf(detail, sizeof(detail), "%u. %s%s%s", row + 1,
                 maneuver->instruction[0] ? maneuver->instruction : TR("Upcoming turn"),
                 maneuver->distance[0] ? "  /  " : "", maneuver->distance);
        set_phone_text(phone_hub.maneuvers[row], maneuver->present ? detail : "");
    }
    if (media->lane_count)
        lv_obj_remove_flag(phone_hub.lane_strip, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(phone_hub.lane_strip, LV_OBJ_FLAG_HIDDEN);
    for (size_t lane = 0; lane < CARPLAY_HUB_LANES; ++lane)
    {
        int angle = media->lanes[lane].angle;
        const char *arrow = angle < -15 ? LV_SYMBOL_LEFT : angle > 15 ? LV_SYMBOL_RIGHT
                                                                      : LV_SYMBOL_UP;
        set_phone_text(phone_hub.lanes[lane], lane < media->lane_count ? arrow : "");
        lv_obj_set_style_text_color(phone_hub.lanes[lane],
                                    lv_color_hex(carplay_ui_color(media->lanes[lane].highlighted ? CARPLAY_UI_ACCENT : CARPLAY_UI_MUTED)), 0);
    }

    static const char * const charging[] = { "Not charging", "Charging", "Charged" };
    const char *power = media->has_charging && media->charging < 3 ? TR(charging[media->charging]) : media->has_external_power && media->external_power ? TR("External power")
                                                                                                                                                        : "";
    char battery[20] = "";
    if (media->has_battery && media->battery <= 100)
        snprintf(battery, sizeof(battery), "%u%%", (unsigned)media->battery);
    snprintf(detail, sizeof(detail), "%s%s%s", battery, battery[0] && power[0] ? "  /  " : "", power);
    set_phone_text(phone_power, detail[0] ? detail : TR("Battery unavailable"));
    const char *network = media->carrier[0] ? media->carrier : TR("Cellular");
    if (media->has_airplane && media->airplane)
        snprintf(detail, sizeof(detail), "%s", TR("Airplane mode"));
    else if (media->has_signal)
        snprintf(detail, sizeof(detail), "%s  /  %s %u", network, TR("Signal"), (unsigned)media->signal);
    else
        snprintf(detail, sizeof(detail), "%s", media->carrier[0] ? media->carrier : media->has_cellular && !media->cellular ? TR("Cellular not supported")
                                                                                                                            : TR("Cellular unavailable"));
    set_phone_text(phone_cellular, detail);
    static const char * const call_states[] = {
        "No active call", "Dialing", "Ringing", "Connecting call", "Call active", "Call on hold", "Ending call"
    };
    set_phone_text(phone_call, TR(media->has_call && media->call < sizeof(call_states) / sizeof(call_states[0]) ? call_states[media->call] : "Call status unavailable"));
    carplay_ui_set_text(phone_transport, status && status->stage == DEMO_LIVE
                                             ? (status->wireless ? "iPhone · Wireless" : "iPhone · USB")
                                             : "Not connected");
#if defined(RT_USING_RTC)
    format_local_clock(detail, sizeof(detail));
    set_phone_text(phone_clock, detail);
    const char *clock_source = !carplay_app_clock_is_automatic() ? "Manual clock" : carplay_app_phone_clock_received() ? "iPhone time received"
                                                                                                                       : "Waiting for iPhone time";
    set_phone_text(phone_clock_mode, TR(clock_source));
#else
    set_phone_text(phone_clock, "--");
    set_phone_text(phone_clock_mode, TR("Clock unavailable"));
#endif
}

static void update_settings_state(const demo_status_t *status)
{
    static unsigned last_language = UINT32_MAX;
    static unsigned last_display = UINT32_MAX;

#if defined(RTCARPLAY_MICROPHONE)
    if (settings_current == SETTINGS_SOUND)
        update_microphone();
#endif
#if defined(RT_USING_RTC)
    if (!lv_obj_has_flag(settings_pages[SETTINGS_CLOCK], LV_OBJ_FLAG_HIDDEN))
        update_clock_settings();
#endif
#if defined(RTCARPLAY_IAP2)
    if (!lv_obj_has_flag(settings_pages[SETTINGS_GNSS], LV_OBJ_FLAG_HIDDEN))
        update_gnss_settings();
#endif
    (void)status;
    unsigned language = carplay_ui_is_chinese() ? 0U : 1U;
    if (last_language != language)
    {
        apply_segments(language_buttons, 2, language);
        last_language = language;
    }
    unsigned display = display_choice;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (status)
        display = status->display_requested == CARPLAY_FULLSCREEN ? 0U : 1U;
#endif
    if (last_display != display)
    {
        apply_segments(display_buttons, 2, display);
        last_display = display;
    }
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (status && status->display_reconfiguring)
        set_text_if_changed(display_note, "Reconnect required to apply the new layout");
#endif
}

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static void update_projection(lv_timer_t *timer)
{
    (void)timer;
    static carplay_display_mode_t last_mode = CARPLAY_LOCAL;
    carplay_fusion_snapshot_t display;
    carplay_fusion_snapshot(rt_tick_get_millisecond(), &display);
    if (display.effective != last_mode)
    {
        last_mode = display.effective;
        (void)carplay_fusion_ui_update(NULL);
    }
}
#endif

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
#if defined(RTCARPLAY_NETWORK_TIME)
                               clock_server_panel,
#endif
#if defined(RTCARPLAY_OTA)
                               ota_confirm_panel,
                               ota_source_panel,
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
    for (unsigned index = 0; index < RTCP_TOUCH_CONTACTS; ++index)
    {
        carplay_fusion_touch_t action = carplay_fusion_cancel_touch();
        if (action.owner == CARPLAY_TOUCH_NONE)
            break;
        carplay_launcher_touch_dispatch(action);
    }
    if (carplay_fusion_ui_update(&status))
        return;
#endif
    /* No invalidation while the phone owns the whole panel. */
    if (live && carplay_launcher_video_is_active())
        return;

    update_phone_model(have_status ? &status : NULL);
    carplay_internet_status_t network;
    carplay_internet_status(&network);
    static carplay_style_role_t ethernet_role = CARPLAY_STYLE_ICON_FAINT;
    update_network_icon(dash_ethernet, &ethernet_role, network.link, network.addressed);
#if defined(RTCARPLAY_INTERNET_SHARE)
    ethernet_update(&network);
#endif

    runtime_controls();
    update_status_bar(&status, have_status);
    if (current_page == PAGE_DASHBOARD)
        update_dashboard(&status, have_status);
    if (current_page == PAGE_CONNECTIVITY || current_page == PAGE_PHONE_DETAILS)
    {
        carplay_connectivity_update(&connectivity_view, have_status ? &status : NULL);
        update_phone_hub(have_status ? &status : NULL);
    }
    if (current_page == PAGE_SETTINGS)
        update_settings_state(have_status ? &status : NULL);
    ota_update();
#if defined(RTCARPLAY_OTA)
    if (ota_hmi_ready)
        carplay_ota_boot_health_ready();
#endif
#if defined(CARPLAY_STA_SETTINGS)
    carplay_wifi_status_t wifi;
    carplay_wifi_status(&wifi);
    static carplay_style_role_t wifi_role = CARPLAY_STYLE_ICON_FAINT;
    update_network_icon(dash_wifi, &wifi_role, wifi.ap_active || wifi.connected, wifi.addressed);
    wifi_update(&wifi);
#endif

    if (have_status && status.wireless && status.pairing.kind &&
        status.stage != DEMO_CLOSING && status.stage != DEMO_FAILED)
    {
        if (visible_pairing_request != status.pairing.request)
        {
            visible_pairing_request = status.pairing.request;
            if (status.pairing.kind == RTCP_BT_PAIR_CONSENT)
                carplay_ui_set_text(bt_text, "Allow Bluetooth pairing?");
            else
                carplay_ui_set_text_fmt(bt_text,
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
    if (pages[PAGE_DASHBOARD])
        return;
    if (!carplay_launcher_touch_init())
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
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    display_choice = carplay_preferences_get(CARPLAY_PREF_DISPLAY, CARPLAY_FUSION) ==
                             CARPLAY_FULLSCREEN
                         ? 0U
                         : 1U;
#else
    display_choice = UINT32_MAX;
#endif

    lv_obj_t *screen = lv_screen_active();
    carplay_ui_style(screen, CARPLAY_STYLE_SCREEN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(screen, &carplay_ui_17, 0);
    int width = lv_display_get_horizontal_resolution(NULL);
    int height = lv_display_get_vertical_resolution(NULL);
    lv_obj_t *surface = carplay_launcher_surface(screen, width, height);
    carplay_system_shell_build(&shell, surface, navigate);
    build_status_bar(shell.status);

    for (unsigned i = 0; i < PAGE_COUNT; ++i)
    {
        pages[i] = lv_obj_create(shell.content);
        lv_obj_remove_style_all(pages[i]);
        lv_obj_set_pos(pages[i], 0, 0);
        lv_obj_set_size(pages[i], lv_pct(100), lv_pct(100));
        lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    build_dashboard(pages[PAGE_DASHBOARD]);
    build_phone_hub(pages[PAGE_CONNECTIVITY]);
    build_phone_details(pages[PAGE_PHONE_DETAILS]);
    build_settings(pages[PAGE_SETTINGS]);
    build_ota(pages[PAGE_OTA]);
    build_modals(surface);
    show_page(PAGE_DASHBOARD);

    bool pointer = carplay_launcher_touch_bind();
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_fusion_ui_init(screen, (uint16_t)width, (uint16_t)height);
#endif
    lv_timer_create(update_page, 250, NULL);
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    lv_timer_create(update_projection, 33, NULL);
#endif
    int touch = carplay_launcher_touch_start((uint16_t)width, (uint16_t)height);
    if (touch != RTCP_OK)
        rt_kprintf("[carplay-demo] launch page touch error=%d\n", touch);
    int result = carplay_connection_manager_start();
#if defined(RTCARPLAY_OTA)
    ota_hmi_ready = pointer && touch == RTCP_OK && result == RTCP_OK;
#else
    (void)pointer;
#endif
    if (result != RTCP_OK)
        rt_kprintf("[carplay-demo] connection manager error=%d\n", result);
}

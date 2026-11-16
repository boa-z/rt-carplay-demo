/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LVGL presentation and launcher input only. Connection policy lives in app/. */
#include <rtthread.h>
#include <stdio.h>
#include <string.h>
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
#define lv_label_set_text     carplay_ui_set_text
#define lv_label_set_text_fmt carplay_ui_set_text_fmt
#define TR                    carplay_ui_translate
#if defined(RTCARPLAY_MICROPHONE)
#include "audio_input_lubanlite.h"
#endif
#if defined(RTCARPLAY_OTA)
#include "ota_lubanlite.h"
#endif

static rt_mutex_t pointer_lock;
static lv_obj_t *heading, *description, *pin_label, *badge, *debug_label;
static lv_obj_t *steps[3];
static lv_obj_t *step_labels[3], *path_labels[2], *session_note, *uptime_label;
static lv_obj_t *settings_panel, *settings_note, *wireless_setting, *microphone_setting;
static lv_obj_t *settings_button, *diagnostics_panel, *modal_scrim;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static lv_obj_t *display_panel, *display_note, *display_modes[3];
static carplay_display_mode_t display_choice = CARPLAY_FUSION;
#endif
static lv_obj_t *language_buttons[2];
static void update_page(lv_timer_t *timer);
static lv_obj_t *network_panel, *network_button, *network_summary, *network_detail, *network_check, *network_note;
static lv_obj_t *bt_panel, *bt_text, *bt_accept;
static uint32_t visible_pairing_request;
static bool network_ready;

#if defined(RTCARPLAY_OTA)
static lv_obj_t *ota_version_label, *ota_state_label;
static lv_obj_t *ota_check_button, *ota_install_button;
static lv_obj_t *ota_confirm_panel, *ota_confirm_text;
static char ota_confirm_commit[41];
static const char *ota_notice;
#endif

static lv_obj_t *manage_button, *retry_button, *file_panel, *file_list, *file_note;
static lv_obj_t *confirm_panel, *confirm_text;
static char selected_file[CARPLAY_PAIR_FILENAME_MAX];
typedef struct
{
    uint16_t x, y;
    bool down;
} demo_pointer_t;
static demo_pointer_t pointer_queue[32], pointer_state;
static unsigned pointer_head, pointer_tail;
static bool pointer_active;
static uint16_t pointer_id;

#if defined(RTCARPLAY_AUDIO)
static lv_obj_t *audio_button, *audio_panel, *audio_note, *audio_settings;
static lv_obj_t *audio_play, *audio_mode_buttons[5], *audio_meters[2];
#endif

static void update_scrim(void)
{
    if (!modal_scrim)
        return;
    lv_obj_t *pages[] = {
        settings_panel,
        diagnostics_panel,
        network_panel,
        file_panel,
        bt_panel,
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        display_panel,
#endif
#if defined(RTCARPLAY_OTA)
        ota_confirm_panel,
#endif
#if defined(RTCARPLAY_AUDIO)
        audio_panel,
#endif
    };
    bool visible = false;
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i)
        visible |= pages[i] && !lv_obj_has_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    if (visible)
        lv_obj_remove_flag(modal_scrim, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(modal_scrim, LV_OBJ_FLAG_HIDDEN);
}

static void show_modal(lv_obj_t *page)
{
    lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(page);
    update_scrim();
}

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

static const char *stage_name(demo_stage_t stage)
{
    static const char *names[] = { "Starting", "Waiting for phone", "iAP2 pairing", "Network setup",
                                   "AirPlay pairing", "Video active", "Closing", "Failed" };
    return stage <= DEMO_FAILED ? names[stage] : "Unknown";
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int width, uint32_t color, bool large)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
#if LV_FONT_MONTSERRAT_24
    if (large)
        lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, 0);
#else
    (void)large;
#endif
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y,
                        int width, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 44);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x20344B), 0);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x30465E), 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xE5EEF8), 0);
    lv_obj_t *caption = lv_label_create(obj);
    lv_obj_set_width(caption, width - 20);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    if (callback)
        lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    return obj;
}

#if defined(RTCARPLAY_AUDIO)
static void audio_open_page(lv_event_t *event)
{
    (void)event;
    if (carplay_connection_audio_open())
        show_modal(audio_panel);
}
static void audio_select(lv_event_t *event)
{
    unsigned id = (unsigned)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(event));
    carplay_connection_audio_select(id);
}
static lv_obj_t *audio_control(lv_obj_t *parent, const char *text, int x, int y, int w, unsigned id)
{
    lv_obj_t *b = button(parent, text, x, y, w, audio_select);
    lv_obj_set_user_data(b, (void *)(uintptr_t)id);
    return b;
}
static void audio_update(void)
{
    bool open, ready, exiting;
    audio_test_config_t config;
    int result;
    audio_test_status_t s;
    carplay_connection_audio_snapshot(&open, &ready, &exiting, &config, &result, &s);
    if (!open)
    {
        lv_obj_add_flag(audio_panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char *names[] = { "PCM tone", "PCM silence", "AAC fixture", "Mic level", "Mic loopback" };
    lv_label_set_text_fmt(audio_settings, "Selected: %s | %u Hz | %s | Volume %u%% %s",
                          TR(names[config.mode]), config.mode == AUDIO_TEST_AAC ? 44100 : config.mode >= AUDIO_TEST_MIC_LEVEL ? 48000
                                                                                                                              : config.rate,
                          TR(config.channel == 1 ? "Left" : config.channel == 2 ? "Right"
                                                                                : "Both"),
                          s.volume, s.muted ? TR("MUTED") : "");
    lv_label_set_text_fmt(audio_note,
                          "%s | Error %d\nInput L/R: %u / %u",
                          TR(exiting ? "Stopping..." : !ready  ? "Starting..."
                                                   : s.running ? "Running"
                                                               : "Ready"),
                          result ? result : s.error, s.peak_left, s.peak_right);
    if (ready && !exiting)
        lv_obj_remove_state(audio_play, LV_STATE_DISABLED);
    else
        lv_obj_add_state(audio_play, LV_STATE_DISABLED);
    lv_bar_set_value(audio_meters[0], (int)s.peak_left, LV_ANIM_OFF);
    lv_bar_set_value(audio_meters[1], (int)s.peak_right, LV_ANIM_OFF);
    for (unsigned i = 0; i < 5; ++i) lv_obj_set_style_bg_color(audio_mode_buttons[i],
                                                               lv_color_hex(config.mode == i ? 0x236859 : 0x20344B), 0);
}
#endif

static void files_back(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(file_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(confirm_panel, LV_OBJ_FLAG_HIDDEN);
    carplay_connection_files_close();
    update_scrim();
}
static void files_refresh(lv_event_t *event)
{
    (void)event;
    carplay_connection_files_refresh();
    lv_label_set_text(file_note, "Reading pairing files...");
}
static void files_open(lv_event_t *event)
{
    (void)event;
    if (!carplay_connection_files_open())
        return;
    show_modal(file_panel);
    lv_label_set_text(file_note, "Reading pairing files...");
}
static void retry_connection(lv_event_t *event)
{
    (void)event;
    carplay_connection_retry();
}
static void close_panel(lv_event_t *event)
{
    lv_obj_t *panel = lv_event_get_user_data(event);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
}
static void open_panel(lv_event_t *event)
{
    lv_obj_t *panel = lv_event_get_user_data(event);
    show_modal(panel);
}
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static void select_display(lv_event_t *event)
{
    display_choice = (carplay_display_mode_t)(uintptr_t)lv_event_get_user_data(event);
    for (unsigned i = 0; i < 3; ++i)
        lv_obj_set_style_bg_color(display_modes[i], lv_color_hex(i == (unsigned)display_choice ? 0x236859 : 0x20344B), 0);
    lv_label_set_text(display_note, "Resolution changes reconnect CarPlay.");
}
void carplay_launcher_open_display_settings(void)
{
    demo_status_t status = { 0 };
    if (carplay_connection_status(&status))
        display_choice = status.display_requested;
    /* Pause composition only while the shared menu is visible. The selected
     * receiver geometry and wired/wireless transport do not change here. */
    carplay_fusion_ui_show_launcher();
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    for (unsigned i = 0; i < 3; ++i)
        lv_obj_set_style_bg_color(display_modes[i], lv_color_hex(i == (unsigned)display_choice ? 0x236859 : 0x20344B), 0);
    lv_label_set_text(display_note, status.display_reconfiguring ? "Reconnecting..." : "Resolution changes reconnect CarPlay.");
    show_modal(display_panel);
}
static void open_display(lv_event_t *event)
{
    (void)event;
    carplay_launcher_open_display_settings();
}
static void resume_display(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(display_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
    carplay_fusion_ui_resume();
}
static void apply_display(lv_event_t *event)
{
    if (!carplay_connection_set_display_mode(display_choice))
    {
        lv_label_set_text(display_note, "Close device/audio tools first.");
        return;
    }
    if (carplay_preferences_error())
    {
        lv_label_set_text(display_note, "Settings not saved.");
        return;
    }
    resume_display(event);
}
#endif
static void select_language(lv_event_t *event)
{
    bool chinese = (uintptr_t)lv_event_get_user_data(event) != 0;
    carplay_ui_set_chinese(chinese);
    int result = carplay_preferences_set(CARPLAY_PREF_LANGUAGE, chinese ? 1U : 0U);
    lv_label_set_text(settings_note, result ? "Settings not saved." : "Settings saved.");
    for (unsigned i = 0; i < 2; ++i)
        lv_obj_set_style_bg_color(language_buttons[i], lv_color_hex(i == (unsigned)chinese ? 0x236859 : 0x20344B), 0);
    update_page(NULL);
}

static void toggle_wireless(lv_event_t *event)
{
    (void)event;
    demo_status_t state;
    if (carplay_connection_status(&state) &&
        carplay_connection_set_wireless_enabled(!state.wireless_enabled))
        lv_label_set_text(settings_note, carplay_preferences_error() ? "Settings not saved." : "Settings saved.");
    else
        lv_label_set_text(settings_note, "Wait until the connection is idle, then try again.");
}
static void toggle_microphone(lv_event_t *event)
{
    (void)event;
#if defined(RTCARPLAY_MICROPHONE)
    carplay_audio_input_mute(!carplay_audio_input_is_muted());
    int result = carplay_preferences_set(CARPLAY_PREF_MIC_MUTED, carplay_audio_input_is_muted() ? 1U : 0U);
    lv_label_set_text(settings_note, result ? "Settings not saved." : "Settings saved.");
#endif
}
/* Names are presentation only; deletion still uses the validated exact key. */
static void device_title(const char *name, char *out, size_t size)
{
    size_t n = strlen(name);
    if (!strcmp(name, "Bluetooth bonds"))
        snprintf(out, size, "%s", TR("All Bluetooth phones"));
    else if (!strcmp(name, "rt-carplay-pairings.bin"))
        snprintf(out, size, "%s", TR("CarPlay trusted phones"));
    else if (n > 26 && !strncmp(name, "rt-carplay-usb-", 14))
        snprintf(out, size, TR("Wired iPhone  /  %.8s"), name + n - 12);
    else
        snprintf(out, size, "%s", TR("Saved phone record"));
}
static void answer_bluetooth(lv_event_t *event)
{
    carplay_connection_pairing_reply(visible_pairing_request,
                                     lv_event_get_target(event) == bt_accept);
    lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
}
static void cancel_forget(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(confirm_panel, LV_OBJ_FLAG_HIDDEN);
}
static void confirm_forget(lv_event_t *event)
{
    (void)event;
    carplay_connection_files_forget(selected_file);
    lv_obj_add_flag(confirm_panel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(file_note, "Forgetting selected record and restarting receiver...");
}
static void select_file(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    const char *name = lv_obj_get_user_data(target);
    snprintf(selected_file, sizeof(selected_file), "%s", name);
    char title[96];
    device_title(name, title, sizeof(title));
    lv_label_set_text_fmt(confirm_text,
                          "Forget %s?\n\n%s", title,
                          !strcmp(name, "Bluetooth bonds") ? TR("All Bluetooth phones will need to pair again.") : !strcmp(name, "rt-carplay-pairings.bin") ? TR("All CarPlay phones will need to pair again.")
                                                                                                                                                            : TR("This iPhone may ask you to tap Trust again."));
    lv_obj_remove_flag(confirm_panel, LV_OBJ_FLAG_HIDDEN);
}
static void select_bluetooth_bonds(lv_event_t *event)
{
    lv_obj_set_user_data(lv_event_get_target(event), "Bluetooth bonds");
    select_file(event);
}
static void update_files(void)
{
    static unsigned revision;
    /* UI owns this copy: button user-data stays stable until list rebuilt. */
    static carplay_pair_file_t visible[CARPLAY_PAIR_FILES_MAX];
    unsigned count;
    bool more;
    int result;
    if (!carplay_connection_files_snapshot(&revision, visible, &count, &more, &result))
        return;
    lv_obj_clean(file_list);
    for (unsigned i = 0; i < count; ++i)
    {
        char text[140];
        device_title(visible[i].name, text, sizeof(text));
        lv_obj_t *row = button(file_list, text, 0, (int)i * 50,
                               lv_obj_get_width(file_list) - 20, select_file);
        lv_obj_set_user_data(row, visible[i].name);
    }
    lv_label_set_text_fmt(file_note,
                          "Saved devices: %u%s | Error %d",
                          count, more ? "+" : "", result);
}

static void network_check_now(lv_event_t *event)
{
    (void)event;
    (void)carplay_internet_check(); /* Queue only; never wait in the LVGL task. */
}

#if defined(RTCARPLAY_OTA)
static void ota_check_now(lv_event_t *event)
{
    (void)event;
    ota_notice = carplay_ota_check() == RT_EOK ? NULL : "Update service busy.";
}
/* Nightly channel: the download starts only after this explicit confirmation. */
static void ota_install_now(lv_event_t *event)
{
    (void)event;
    carplay_ota_status_t ota;
    carplay_ota_status(&ota);
    if (ota.busy || ota.state != CARPLAY_OTA_AVAILABLE)
        return;
    snprintf(ota_confirm_commit, sizeof(ota_confirm_commit), "%s", ota.remote_commit);
    char target[48];
    if (ota.remote_commit[0])
        snprintf(target, sizeof(target), "preview build %.8s", ota.remote_commit);
    else
        snprintf(target, sizeof(target), "version %s", ota.remote);
    lv_label_set_text_fmt(ota_confirm_text,
                          "Install %s?\n\nCurrent: %s (%.8s)\n\nDisconnect CarPlay. Keep power connected.\nRestart after installation.",
                          target, ota.current, ota.current_commit);
    lv_obj_remove_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ota_confirm_panel);
    update_scrim();
}
static void ota_confirm_install(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
    ota_notice = carplay_ota_install(ota_confirm_commit) == RT_EOK ? NULL : "Update changed or busy. Check again.";
}
static void ota_confirm_cancel(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    update_scrim();
}
/* Mirrors the OTA service state; runs in the GUI timer only. */
static void ota_update(void)
{
    if (lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN))
        return;

    carplay_ota_status_t ota;
    carplay_ota_status(&ota);
    lv_label_set_text_fmt(ota_version_label, "Firmware %s (%.8s) | %s",
                          ota.current,
                          ota.current_commit[0] ? ota.current_commit : "unknown",
                          ota.slot);
    char text[160];
    switch (ota.state)
    {
    case CARPLAY_OTA_CHECKING:
        snprintf(text, sizeof(text), "Checking the release server...");
        break;
    case CARPLAY_OTA_DOWNLOADING:
    case CARPLAY_OTA_APPLYING:
        if (ota.progress >= 0)
            snprintf(text, sizeof(text), "%s  %d%%", TR(ota.state == CARPLAY_OTA_DOWNLOADING ? "Downloading" : "Installing"), ota.progress);
        else
            snprintf(text, sizeof(text), "%s", TR(ota.state == CARPLAY_OTA_DOWNLOADING ? "Downloading" : "Installing"));
        break;
    case CARPLAY_OTA_UP_TO_DATE:
    case CARPLAY_OTA_AVAILABLE:
    case CARPLAY_OTA_FAILED:
    case CARPLAY_OTA_REBOOTING:
        snprintf(text, sizeof(text), "%s", TR(ota.state == CARPLAY_OTA_UP_TO_DATE ? "Up to date" : ota.state == CARPLAY_OTA_AVAILABLE ? "Update available"
                                                                                               : ota.state == CARPLAY_OTA_FAILED      ? "Update failed"
                                                                                                                                      : "Restarting"));
        break;
    default:
        snprintf(text, sizeof(text),
                 "Not checked yet. Connect Ethernet, then check for updates.");
        break;
    }
    lv_label_set_text(ota_state_label, ota_notice ? ota_notice : text);
    lv_obj_set_style_text_color(ota_state_label, lv_color_hex(0xC1CFDF), 0);
    if (ota.state == CARPLAY_OTA_UP_TO_DATE || ota.state == CARPLAY_OTA_AVAILABLE)
        lv_obj_set_style_text_color(ota_state_label, lv_color_hex(0x58E0BF), 0);
    else if (ota.state == CARPLAY_OTA_FAILED)
        lv_obj_set_style_text_color(ota_state_label, lv_color_hex(0xE08A8A), 0);
    bool busy = ota.busy || !lv_obj_has_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    if (network_ready && !busy)
        lv_obj_remove_state(ota_check_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(ota_check_button, LV_STATE_DISABLED);
    if (network_ready && !busy && ota.state == CARPLAY_OTA_AVAILABLE)
        lv_obj_remove_state(ota_install_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(ota_install_button, LV_STATE_DISABLED);
}
#endif

static void network_update(void)
{
    carplay_internet_status_t n;
    carplay_internet_status(&n);
    network_ready = n.available && n.route_ready;
    const char *state = !n.available ? "Unavailable" : !n.device       ? "No adapter"
                                                   : !n.link           ? "Cable disconnected"
                                                   : !n.addressed      ? "Getting address"
                                                   : !n.route_ready    ? "No gateway"
                                                   : n.checking        ? "Checking..."
                                                   : !n.checked        ? "Not checked"
                                                   : n.reachable       ? "Internet verified"
                                                   : n.age_ms > 45000U ? "Check expired"
                                                                       : "Check failed";
    lv_label_set_text_fmt(network_summary, "Ethernet: %s", TR(state));
    lv_obj_set_style_text_color(network_summary, lv_color_hex(n.reachable ? 0x58E0BF : 0xC1CFDF), 0);
    if (n.available && n.route_ready && !n.checking)
        lv_obj_remove_state(network_check, LV_STATE_DISABLED);
    else
        lv_obj_add_state(network_check, LV_STATE_DISABLED);
    char age[32];
    if (n.checked)
        snprintf(age, sizeof(age), "%lu s", (unsigned long)(n.age_ms / 1000U));
    else
        snprintf(age, sizeof(age), "%s", TR("Never"));
    lv_label_set_text_fmt(network_note, "HTTP 204: %s",
                          n.available ? n.probe_url : "Not built or service unavailable");
    lv_label_set_text_fmt(network_detail,
                          "%s\n\nIPv4       %s\nMask      %s\nGateway %s\nDNS       %s\n\nHTTP 204: %s | %s\nCarPlay sharing: %s",
                          TR(state), n.address, n.mask, n.gateway, n.dns, TR(n.checking ? "Running" : n.checked ? (n.reachable ? "Passed" : "Failed / expired")
                                                                                                                : "Not run"),
                          age, TR(n.sharing ? "Active" : "Inactive"));
}

static void update_page(lv_timer_t *timer)
{
    (void)timer;
    for (unsigned i = 0; i < 2; ++i)
        if (language_buttons[i])
            lv_obj_set_style_bg_color(language_buttons[i], lv_color_hex(i == (unsigned)carplay_ui_is_chinese() ? 0x236859 : 0x20344B), 0);
    /* Same GUI startup owner retries a failed worker allocation/schedule.
     * Successful manager startup is idempotent and creates no second worker. */
    static bool attempted;
    static uint32_t last_attempt;
    uint32_t now = rt_tick_get_millisecond();
    if (!attempted || (uint32_t)(now - last_attempt) >= 1000U)
    {
        attempted = true;
        last_attempt = now;
        (void)carplay_connection_manager_start();
    }
    demo_status_t s = { 0 };
    bool have_status = carplay_connection_status(&s);
    /* Connection success is an edge, not a permanent page lock: a later
     * deliberate visit to the launcher can still edit settings. */
    static bool was_live;
    bool live = have_status && s.stage == DEMO_LIVE;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    live = live && !s.display_reconfiguring;
#else
    live = live && carplay_launcher_video_is_active();
#endif
    if (live && !was_live)
    {
        lv_obj_t *pages[] = {
            settings_panel,
            diagnostics_panel,
            network_panel,
            bt_panel,
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
            display_panel,
#endif
#if defined(RTCARPLAY_OTA)
            ota_confirm_panel,
#endif
        };
        for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i)
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        update_scrim();
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        carplay_fusion_ui_resume();
#endif
    }
    if (have_status)
        was_live = live;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    dispatch_fusion_touch(carplay_fusion_cancel_touch());
    if (carplay_fusion_ui_update(&s))
        return;
#endif
    if (!have_status)
        return;
    if (live && carplay_launcher_video_is_active())
        return; /* No further invalidation/animation on the hidden launcher. */
    network_update();
    update_files();
#if defined(RTCARPLAY_OTA)
    ota_update();
#endif
#if defined(RTCARPLAY_AUDIO)
    audio_update();
#endif
    if (s.wireless && s.pairing.kind && s.stage != DEMO_CLOSING && s.stage != DEMO_FAILED)
    {
        if (visible_pairing_request != s.pairing.request)
        {
            visible_pairing_request = s.pairing.request;
            if (s.pairing.kind == RTCP_BT_PAIR_CONSENT)
                lv_label_set_text(bt_text, "Allow Bluetooth pairing?");
            else
                lv_label_set_text_fmt(bt_text,
                                      s.pairing.kind == RTCP_BT_PAIR_COMPARE ? "%06u\n\nConfirm this number on your iPhone." : "%06u\n\nEnter on your iPhone.", s.pairing.passkey);
            if (s.pairing.kind == RTCP_BT_PAIR_DISPLAY)
                lv_obj_add_state(bt_accept, LV_STATE_DISABLED);
            else
                lv_obj_remove_state(bt_accept, LV_STATE_DISABLED);
            show_modal(bt_panel);
        }
    }
    else
    {
        visible_pairing_request = 0;
        lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
    }
    unsigned seconds = rt_tick_get_millisecond() / 1000U;
    lv_label_set_text_fmt(uptime_label, "UPTIME  %02u:%02u:%02u",
                          seconds / 3600U, seconds / 60U % 60U, seconds % 60U);
    const char *usb = !s.usb_ready ? "Starting USB service" : s.usb ? (s.wireless ? "Phone detected / queued" : "Phone detected")
                                                                    : "Ready for a cable";
    const char *wireless = !s.wireless_enabled ? "Paused in Settings" : !s.wireless  ? "Standby after USB session"
                                                                    : s.bt_connected ? "Phone connected"
                                                                    : s.bt_ready     ? "Discoverable as rt-carplay"
                                                                                     : "Starting Bluetooth / Wi-Fi";
    lv_label_set_text_fmt(path_labels[0], "WIRED USB\n%s", TR(usb));
    lv_label_set_text_fmt(path_labels[1], "WIRELESS\n%s", TR(wireless));
    lv_label_set_text_fmt(session_note, "%s\n%s",
                          TR(s.stage == DEMO_LIVE ? "Session connected" : "One phone at a time"),
                          TR(s.retry_seconds ? "Waiting before automatic retry" : "Automatic discovery is on"));
    lv_label_set_text(lv_obj_get_child(wireless_setting, 0),
                      s.wireless_enabled ? "Wireless discovery: ON" : "Wireless discovery: OFF");
#if defined(RTCARPLAY_MICROPHONE)
    lv_label_set_text(lv_obj_get_child(microphone_setting, 0),
                      carplay_audio_input_is_muted() ? "Microphone: MUTED" : "Microphone: ENABLED");
#endif
    bool idle = s.stage == DEMO_WAIT || s.stage == DEMO_FAILED;
    lv_obj_t *idle_buttons[] = {
        manage_button,
        retry_button,
        wireless_setting,
#if !defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        settings_button,
#endif
    };
    for (unsigned i = 0; i < sizeof(idle_buttons) / sizeof(idle_buttons[0]); ++i)
    {
        if (idle)
            lv_obj_remove_state(idle_buttons[i], LV_STATE_DISABLED);
        else
            lv_obj_add_state(idle_buttons[i], LV_STATE_DISABLED);
    }
#if defined(RTCARPLAY_AUDIO)
    if (idle)
        lv_obj_remove_state(audio_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(audio_button, LV_STATE_DISABLED);
#endif
    const char *title, *detail, *tag;
    switch (s.stage)
    {
    case DEMO_WAIT:
        title = "Ready when you are.";
        detail = s.wireless_enabled ? "Connect USB or pair Bluetooth." : "Connect your iPhone with USB.";
        tag = "AUTO CONNECT";
        break;
    case DEMO_PAIR:
        title = s.wireless ? "Pair your iPhone." : "Trust this display.";
        detail = s.wireless ? "Confirm pairing on your iPhone." : "Unlock iPhone and tap Trust.";
        tag = "VERIFYING";
        break;
    case DEMO_NETWORK:
        title = "Making the connection.";
        detail = s.wireless ? "Joining Wi-Fi..." : "Preparing USB...";
        tag = "CONNECTING";
        break;
    case DEMO_CONNECT:
        title = "Opening CarPlay.";
        detail = "Waiting for video...";
        tag = "ALMOST READY";
        break;
    case DEMO_LIVE:
        title = "Check display handoff.";
        detail = "See Diagnostics.";
        tag = "DISPLAY CHECK";
        break;
    case DEMO_CLOSING:
        title = "Getting ready again.";
        detail = "Closing session...";
        tag = "RETURNING";
        break;
    case DEMO_FAILED:
        title = "Let's reconnect.";
        detail = s.storage_error ? "Pairing storage error." : "Retrying automatically...";
        tag = "RECONNECTING";
        break;
    default:
        title = "Your drive, connected.";
        detail = "Starting...";
        tag = "LAUNCHER 02";
        break;
    }
    lv_label_set_text(heading, title);
    lv_label_set_text(description, detail);
    lv_label_set_text(badge, tag);
    lv_label_set_text_fmt(pin_label, "Bluetooth: rt-carplay  |  CarPlay PIN: %s\nWi-Fi: %s",
                          s.pin[0] ? s.pin : TR("preparing"), s.ssid[0] ? s.ssid : TR(s.wireless_enabled ? "preparing" : "paused"));
    for (unsigned i = 0; i < 3; ++i)
    {
        bool active = (int)s.stage >= DEMO_PAIR + (int)i && s.stage <= DEMO_LIVE;
        lv_obj_set_style_bg_color(steps[i], lv_color_hex(active ? 0x58E0BF : 0x26384B), 0);
    }
    if (!lv_obj_has_flag(diagnostics_panel, LV_OBJ_FLAG_HIDDEN))
    {
        lv_label_set_text_fmt(debug_label,
                              "Connection: %s / %s     Error: %d (last %d)\n"
                              "USB service: %s    Attached: %s    USB error: %d\n"
                              "Wireless: %s    Bluetooth: %s    MFi: %s    Radio error: %d\n"
                              "Network: %s    Wi-Fi: %s\n"
                              "Transport: %u   Authenticated: %u   Session: %u\n"
                              "Video endpoint: %u   Audio endpoint: %u   Input: %u\n"
                              "Decoded frames: %u   Drops: %u   Decode errors: %u\n"
                              "Display handoff: %s    Screen RX: %u    HID sent/failed: %u/%u\n"
                              "Trust cache: %d    Pairing write: %d    Bonds: %u/%u\n"
                              "BT HCI/security: %02x/%u    Invite/ACK/retry: %u/%u/%u\n"
                              "Recovery cycles: %u    Current retry wait: %u s",
                              TR(s.wireless ? "WIRELESS" : "USB"), stage_name(s.stage), s.error, s.last_error,
                              s.usb_ready ? "ready" : "starting", s.usb ? "yes" : "no", s.usb_error,
                              s.wireless_enabled ? "enabled" : "paused", s.bt_connected ? "connected" : s.bt_ready ? "ready"
                                                                                                                   : "inactive",
                              s.bt_authenticated ? "authenticated" : "waiting", s.wireless_error,
                              s.ipv6[0] ? s.ipv6 : "waiting", s.ssid[0] ? s.ssid : "inactive",
                              s.receiver.transport_connected, s.receiver.authenticated, s.receiver.session_established,
                              s.receiver.video_ready, s.receiver.audio_ready, s.receiver.input_ready,
                              s.frames, s.drops, s.errors, carplay_launcher_video_is_active() ? "active" : "launcher visible",
                              s.received, s.hid_sent, s.hid_failed, s.trust, s.storage_error,
                              s.pairing.bond_count, s.pairing.bond_capacity, s.pairing.hci_error, s.pairing.security_error,
                              s.invites, s.invite_acks, s.reinvites, s.reconnects, s.retry_seconds);
    }
    update_scrim();
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x121E2C), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x26384B), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 24, 0);
    lv_obj_set_style_pad_all(obj, 24, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
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
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x090F19), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
#if LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_16, 0);
#endif
    int width = lv_display_get_horizontal_resolution(NULL);
    int height = lv_display_get_vertical_resolution(NULL);
    label(screen, "CARPLAY", 36, 30, 300, 0xEDF5FF, true);
    label(screen, "", 36, 65, 420, 0x8FA5BF, false);
    uptime_label = label(screen, "UPTIME  00:00:00", 410, 40, 230, 0x8FA5BF, false);
    lv_obj_t *card = panel(screen, 36, 110, width - 422, height - 282);
    badge = label(card, "LAUNCHER 02", 0, 0, 480, 0x58E0BF, false);
    heading = label(card, "Your drive, connected.", 0, 44, width - 474, 0xF2F7FF, true);
#if LV_FONT_MONTSERRAT_32
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_32, 0);
#endif
    description = label(card, "Starting USB and wireless discovery.", 0, 102,
                        width - 474, 0xC1CFDF, false);
    lv_obj_set_style_text_line_space(description, 8, 0);
    pin_label = label(card, "", 0, 206, width - 474, 0x8FA5BF, false);
    lv_obj_set_style_text_line_space(pin_label, 6, 0);
    lv_obj_t *paths = panel(screen, width - 366, 110, 330, height - 282);
    label(paths, "CONNECTIONS", 0, 0, 280, 0x58E0BF, false);
    path_labels[0] = label(paths, "WIRED USB\nStarting USB service", 0, 42, 280, 0xE0EAF6, false);
    path_labels[1] = label(paths, "WIRELESS\nStarting Bluetooth / Wi-Fi", 0, 112, 280, 0xE0EAF6, false);
    session_note = label(paths, "One phone at a time\nAutomatic discovery is on", 0, 206, 280, 0x8FA5BF, false);
    for (unsigned i = 0; i < 3; ++i)
    {
        int x = 36 + (int)i * ((width - 72 + 16) / 3);
        steps[i] = lv_obj_create(screen);
        lv_obj_set_size(steps[i], (width - 104) / 3, 4);
        lv_obj_set_pos(steps[i], x, height - 154);
        lv_obj_set_style_border_width(steps[i], 0, 0);
        lv_obj_set_style_bg_color(steps[i], lv_color_hex(0x26384B), 0);
        step_labels[i] = label(screen, i == 0 ? "01  Discover your phone" : i == 1 ? "02  Verify and connect"
                                                                                   : "03  Open CarPlay",
                               x, height - 141, (width - 104) / 3, 0x8FA5BF, false);
    }
    int nav_width = (width - 124) / 5;
    manage_button = button(screen, "Devices", 36, height - 83, nav_width, files_open);
    retry_button = button(screen, "Retry", 36 + 4 * (nav_width + 13), height - 83, nav_width, retry_connection);
    settings_button = button(screen, "Settings", 36 + 2 * (nav_width + 13), height - 83, nav_width, NULL);
    lv_obj_t *diagnostics_button = button(screen, "Diagnostics", 36 + 3 * (nav_width + 13), height - 83, nav_width, NULL);
#if defined(RTCARPLAY_AUDIO)
    audio_button = button(screen, "Audio", 36 + nav_width + 13, height - 83, nav_width, audio_open_page);
    lv_obj_add_state(audio_button, LV_STATE_DISABLED);
#else
    lv_obj_t *unavailable = button(screen, "Audio unavailable", 36 + nav_width + 13, height - 83, nav_width, NULL);
    lv_obj_add_state(unavailable, LV_STATE_DISABLED);
#endif
    network_button = button(screen, "Ethernet: starting...", width - 366, 36, 330, NULL);
    network_summary = lv_obj_get_child(network_button, 0);
    modal_scrim = lv_obj_create(screen);
    lv_obj_remove_style_all(modal_scrim);
    lv_obj_set_size(modal_scrim, width, height);
    lv_obj_set_style_bg_color(modal_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_scrim, LV_OPA_60, 0);
    lv_obj_add_flag(modal_scrim, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(modal_scrim, LV_OBJ_FLAG_SCROLLABLE);
    settings_panel = panel(screen, 30, 25, width - 60, height - 50);
    label(settings_panel, "SETTINGS", 10, 0, 320, 0xF2F7FF, true);
    language_buttons[0] = button(settings_panel, "English", width - 460, 0, 150, NULL);
    language_buttons[1] = button(settings_panel, "中文", width - 296, 0, 150, NULL);
    lv_obj_add_event_cb(language_buttons[0], select_language, LV_EVENT_CLICKED, (void *)0);
    lv_obj_add_event_cb(language_buttons[1], select_language, LV_EVENT_CLICKED, (void *)1);
    lv_obj_set_style_bg_color(language_buttons[0], lv_color_hex(0x236859), 0);
    settings_note = label(settings_panel,
                          "",
                          10, 52, width - 140, 0x8FA5BF, false);
    wireless_setting = button(settings_panel, "Wireless discovery: ON", 10, 178, 330, toggle_wireless);
    microphone_setting = button(settings_panel, "Microphone not enabled", 360, 178, 330, toggle_microphone);
#if !defined(RTCARPLAY_MICROPHONE)
    lv_obj_add_state(microphone_setting, LV_STATE_DISABLED);
#endif
#if defined(RTCARPLAY_OTA)
    label(settings_panel, "FIRMWARE & UPDATES", 10, 264, 500, 0x58E0BF, false);
    ota_version_label = label(settings_panel, "Installed firmware", 10, 296, width - 140, 0xC1CFDF, false);
    ota_state_label = label(settings_panel,
                            "Not checked yet. Connect Ethernet, then check for updates.",
                            10, 324, width - 140, 0x8FA5BF, false);
    ota_check_button = button(settings_panel, "Check for updates", 10, 388, 230, ota_check_now);
    ota_install_button = button(settings_panel, "Install update", 255, 388, 230, ota_install_now);
    lv_obj_add_state(ota_check_button, LV_STATE_DISABLED);
    lv_obj_add_state(ota_install_button, LV_STATE_DISABLED);
#else
    label(settings_panel, "SYSTEM", 10, 280, 500, 0x58E0BF, false);
    lv_obj_t *system = label(settings_panel, "", 10, 316, width - 140, 0xC1CFDF, false);
    lv_label_set_text_fmt(system, "Launcher V2  |  ArtInChip D213  |  LVGL 9\nDisplay: %d x %d  |  One active CarPlay session", width, height);
#endif
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    button(settings_panel, "Display mode", 10, 100, 330, open_display);
    display_panel = panel(screen, 30, 25, width - 60, height - 50);
    label(display_panel, "CARPLAY DISPLAY", 10, 0, width - 140, 0xF2F7FF, true);
    label(display_panel, "USB and wireless",
          10, 52, width - 140, 0xC1CFDF, false);
    const char *mode_names[] = { "Full screen", "Fusion", "Local instruments" };
    const char *mode_sizes[] = { "1024 x 600", "768 x 600 + meters", "Instruments only" };
    for (unsigned i = 0; i < 3; ++i)
    {
        int x = 10 + (int)i * 300;
        display_modes[i] = button(display_panel, mode_names[i], x, 125, 280, NULL);
        lv_obj_set_height(display_modes[i], 100);
        lv_obj_add_event_cb(display_modes[i], select_display, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        label(display_panel, mode_sizes[i], x, 246, 280, 0x8FA5BF, false);
    }
    display_note = label(display_panel, "Resolution changes reconnect CarPlay.", 10, 330, width - 140, 0x8FA5BF, false);
    button(display_panel, "Cancel", 10, 440, 180, resume_display);
    button(display_panel, "Apply", 700, 440, 190, apply_display);
    lv_obj_add_flag(display_panel, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_t *legal = label(settings_panel,
                            "Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI)",
                            10, height - 104, width - 140, 0x8FA5BF, false);
#if LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(legal, &lv_font_montserrat_14, 0);
#else
    (void)legal;
#endif
    lv_obj_t *back = button(settings_panel, "Back",
                            width - 268, 388, 160, NULL);
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    lv_obj_add_event_cb(back, resume_display, LV_EVENT_CLICKED, NULL);
#else
    lv_obj_add_event_cb(back, close_panel, LV_EVENT_CLICKED, settings_panel);
#endif
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(settings_button, open_panel, LV_EVENT_CLICKED, settings_panel);
#if defined(RTCARPLAY_OTA)
    ota_confirm_panel = panel(screen, 80, 120, width - 160, height - 240);
    ota_confirm_text = label(ota_confirm_panel, "", 20, 20, width - 240, 0xFFFFFF, false);
    lv_obj_set_style_text_line_space(ota_confirm_text, 6, 0);
    button(ota_confirm_panel, "Cancel", 20, height - 340, 200, ota_confirm_cancel);
    button(ota_confirm_panel, "Install and reboot", 240, height - 340, 260,
           ota_confirm_install);
    lv_obj_add_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN);
#endif
    diagnostics_panel = panel(screen, 30, 25, width - 60, height - 50);
    label(diagnostics_panel, "CONNECTION DIAGNOSTICS", 10, 0, width - 140, 0xF2F7FF, true);
    debug_label = label(diagnostics_panel, "Waiting for a status snapshot...", 10, 55, width - 140, 0xC1CFDF, false);
#if LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(debug_label, &lv_font_montserrat_14, 0);
#endif
    lv_obj_set_style_text_line_space(debug_label, 8, 0);
    back = button(diagnostics_panel, "Back", 10, height - 165, 160, NULL);
    lv_obj_add_event_cb(back, close_panel, LV_EVENT_CLICKED, diagnostics_panel);
    lv_obj_add_event_cb(diagnostics_button, open_panel, LV_EVENT_CLICKED, diagnostics_panel);
    lv_obj_add_flag(diagnostics_panel, LV_OBJ_FLAG_HIDDEN);
    network_panel = panel(screen, 30, 25, width - 60, height - 50);
    label(network_panel, "VEHICLE NETWORK", 10, 0, 700, 0xF2F7FF, true);
    network_detail = label(network_panel, "Waiting for Ethernet status...", 10, 55, width - 140, 0xC1CFDF, false);
    lv_obj_set_style_text_line_space(network_detail, 4, 0);
    network_note = label(network_panel, "",
                         10, 350, width - 140, 0x8FA5BF, false);
    network_check = button(network_panel, "Check now", 10, height - 165, 200, network_check_now);
    back = button(network_panel, "Back", 230, height - 165, 160, NULL);
    lv_obj_add_event_cb(back, close_panel, LV_EVENT_CLICKED, network_panel);
    lv_obj_add_event_cb(network_button, open_panel, LV_EVENT_CLICKED, network_panel);
    lv_obj_add_flag(network_panel, LV_OBJ_FLAG_HIDDEN);
#if defined(RTCARPLAY_AUDIO)
    audio_panel = panel(screen, 30, 25, width - 60, height - 50);
    label(audio_panel, "SPEAKER & MICROPHONE TEST", 10, 0, width - 130, 0xFFFFFF, true);
    const char *test_modes[] = { "PCM tone", "PCM silence", "AAC tone", "Mic meter", "Mic loopback" };
    for (unsigned i = 0; i < 5; ++i) audio_mode_buttons[i] = audio_control(audio_panel, test_modes[i], 10 + i * 176, 55, 164, i);
#if !defined(LPKG_RT_CARPLAY_AUDIO_CODECS) && !defined(RTCP_AUDIO_CODECS)
    lv_obj_add_state(audio_mode_buttons[2], LV_STATE_DISABLED);
#endif
#if !defined(RTCARPLAY_MICROPHONE)
    lv_obj_add_state(audio_mode_buttons[3], LV_STATE_DISABLED);
    lv_obj_add_state(audio_mode_buttons[4], LV_STATE_DISABLED);
#endif
    audio_control(audio_panel, "44.1 kHz", 10, 115, 140, 10);
    audio_control(audio_panel, "48 kHz", 165, 115, 140, 11);
    audio_control(audio_panel, "Both", 335, 115, 130, 20);
    audio_control(audio_panel, "Left", 480, 115, 130, 21);
    audio_control(audio_panel, "Right", 625, 115, 130, 22);
    audio_control(audio_panel, "Volume -", 10, 175, 150, 30);
    audio_control(audio_panel, "Volume +", 175, 175, 150, 31);
    audio_control(audio_panel, "Mute / unmute", 340, 175, 180, 32);
    audio_settings = label(audio_panel, "", 10, 235, width - 130, 0x58E0BF, false);
    audio_note = label(audio_panel, "Preparing audio tests...", 10, 275, width - 130, 0xC1CFDF, false);
    label(audio_panel, "Input L", 10, 370, 80, 0x8FA5BF, false);
    label(audio_panel, "Input R", 450, 370, 80, 0x8FA5BF, false);
    for (unsigned i = 0; i < 2; ++i)
    {
        audio_meters[i] = lv_bar_create(audio_panel);
        lv_obj_set_pos(audio_meters[i], 95 + (int)i * 440, 372);
        lv_obj_set_size(audio_meters[i], 305, 16);
        lv_bar_set_range(audio_meters[i], 0, 32768);
    }
    audio_play = audio_control(audio_panel, "Start / restart", 10, height - 170, 185, 40);
    audio_control(audio_panel, "Stop", 215, height - 170, 150, 41);
    audio_control(audio_panel, "Back", 385, height - 170, 150, 42);
    lv_obj_add_flag(audio_panel, LV_OBJ_FLAG_HIDDEN);
#endif
    file_panel = panel(screen, 30, 25, width - 60, height - 50);
    label(file_panel, "SAVED DEVICES", 10, 0, 600, 0xFFFFFF, true);
    file_note = label(file_panel, "", 10, 40, width - 110, 0xC1CFDF, false);
    file_list = lv_obj_create(file_panel);
    lv_obj_set_pos(file_list, 10, 100);
    lv_obj_set_size(file_list, width - 120, height - 260);
    lv_obj_set_style_pad_all(file_list, 8, 0);
    lv_obj_set_style_bg_color(file_list, lv_color_hex(0x0E1825), 0);
    lv_obj_set_style_border_color(file_list, lv_color_hex(0x26384B), 0);
    lv_obj_set_style_radius(file_list, 16, 0);
    button(file_panel, "Refresh", 10, height - 145, 150, files_refresh);
    button(file_panel, "Back", 180, height - 145, 150, files_back);
#if DEMO_HAS_WIRELESS
    button(file_panel, "Forget Bluetooth phones", 350, height - 145, 250, select_bluetooth_bonds);
#else
    (void)&select_bluetooth_bonds;
#endif
    confirm_panel = lv_obj_create(file_panel);
    lv_obj_set_pos(confirm_panel, 40, 50);
    lv_obj_set_size(confirm_panel, width - 180, height - 180);
    lv_obj_remove_flag(confirm_panel, LV_OBJ_FLAG_SCROLLABLE);
    confirm_text = label(confirm_panel, "", 10, 10, width - 240, 0xFFFFFF, false);
    button(confirm_panel, "Cancel", 10, height - 285, 160, cancel_forget);
    button(confirm_panel, "Forget record", 200, height - 285, 180, confirm_forget);
    lv_obj_add_flag(confirm_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(file_panel, LV_OBJ_FLAG_HIDDEN);
    bt_panel = panel(screen, 80, 120, width - 160, height - 240);
    bt_text = label(bt_panel, "", 20, 20, width - 240, 0xFFFFFF, true);
    bt_accept = button(bt_panel, "Numbers match / Allow", 20, height - 340, 260, answer_bluetooth);
    button(bt_panel, "Cancel pairing", 300, height - 340, 200, answer_bluetooth);
    lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
    lv_indev_t *pointer = lv_indev_create();
    if (pointer)
    {
        lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(pointer, pointer_read);
    }
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    lv_obj_add_flag(uptime_label, LV_OBJ_FLAG_HIDDEN);
    carplay_fusion_ui_init(screen, (uint16_t)width, (uint16_t)height);
#endif
    carplay_ui_set_chinese(carplay_preferences_get(CARPLAY_PREF_LANGUAGE, 0) != 0);
    lv_timer_create(update_page, 250, NULL);
    int touch = carplay_touch_panel_start((uint16_t)width, (uint16_t)height,
                                          launcher_panel_touch, NULL);
    if (touch != RTCP_OK)
        rt_kprintf("[carplay-demo] launch page touch error=%d\n", touch);
    int result = carplay_connection_manager_start();
    if (result != RTCP_OK)
        rt_kprintf("[carplay-demo] connection manager error=%d\n", result);
}

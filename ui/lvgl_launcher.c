/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LVGL presentation and launcher input only. Connection policy lives in app/. */
#include <rtthread.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "connection_manager.h"
#include "carplay_app.h"
#include "video_sink_lubanlite.h"
#include "video_layer_policy.h"
#include "touch_panel_lubanlite.h"
#include "rtcp/rtcp.h"
#include "rtcp/input.h"

static rt_mutex_t pointer_lock;
static lv_obj_t *heading, *description, *pin_label, *badge, *debug_label;
static lv_obj_t *steps[3];
static lv_obj_t *step_labels[3], *mode_buttons[2];
static lv_obj_t *bt_panel, *bt_text, *bt_accept;
static uint32_t visible_pairing_request;

static lv_obj_t *manage_button, *retry_button, *file_panel, *file_list, *file_note;
static lv_obj_t *confirm_panel, *confirm_text;
static char selected_file[CARPLAY_PAIR_FILENAME_MAX];
typedef struct { uint16_t x, y; bool down; } demo_pointer_t;
static demo_pointer_t pointer_queue[32], pointer_state;
static unsigned pointer_head, pointer_tail;
static bool pointer_active;
static uint16_t pointer_id;

#if defined(RTCARPLAY_AUDIO)
static lv_obj_t *audio_button, *audio_panel, *audio_note, *audio_settings;
static lv_obj_t *audio_play, *audio_mode_buttons[5], *audio_meters[2];
#endif

/* Called only by the existing GT911 reader. No LVGL calls here. */
bool carplay_demo_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y)
{
    if (carplay_launcher_video_is_active() &&
        carplay_video_sink_is_running() && carplay_app_is_running()) {
        if (pointer_lock) {
            (void)rt_mutex_take(pointer_lock, RT_WAITING_FOREVER);
            pointer_head = pointer_tail = 0; pointer_state.down = false; pointer_active = false;
            (void)rt_mutex_release(pointer_lock);
        }
        return false;
    }
    if (!pointer_lock) return true;
    (void)rt_mutex_take(pointer_lock, RT_WAITING_FOREVER);
    if (!pointer_active && phase == RTCP_TOUCH_DOWN) {
        pointer_active = true; pointer_id = id;
    }
    if (!pointer_active || pointer_id != id) {
        (void)rt_mutex_release(pointer_lock); return true;
    }
    if (phase == RTCP_TOUCH_UP) pointer_active = false;
    demo_pointer_t point = { x, y, phase != RTCP_TOUCH_UP };
    unsigned next = (pointer_head + 1U) % 32U;
    if (next == pointer_tail) {
        /* Retain the latest state, including release, if GUI is delayed. */
        pointer_tail = pointer_head;
    }
    pointer_queue[pointer_head] = point;
    pointer_head = next;
    (void)rt_mutex_release(pointer_lock);
    return true;
}

static void launcher_panel_touch(uint8_t phase, uint16_t id, uint16_t x,
                                  uint16_t y, void *context)
{
    (void)context;
    if (!carplay_demo_touch(phase, id, x, y)) {
        (void)carplay_connection_touch(phase, id, x, y);
    }
}

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    (void)rt_mutex_take(pointer_lock, RT_WAITING_FOREVER);
    if (pointer_tail != pointer_head) {
        pointer_state = pointer_queue[pointer_tail];
        pointer_tail = (pointer_tail + 1U) % 32U;
    }
    data->point.x = pointer_state.x; data->point.y = pointer_state.y;
    data->state = pointer_state.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = pointer_tail != pointer_head;
    (void)rt_mutex_release(pointer_lock);
}

static const char *stage_name(demo_stage_t stage)
{
    static const char *names[] = {"Starting", "Waiting for phone", "iAP2 pairing", "Network setup",
                                  "AirPlay pairing", "Video active", "Closing", "Failed"};
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
    if (large) lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, 0);
#else
    (void)large;
#endif
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y,
                         int width, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, width, 44);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x245C80), 0);
    lv_obj_t *caption = lv_label_create(obj);
    lv_label_set_text(caption, text); lv_obj_center(caption);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    return obj;
}

#if defined(RTCARPLAY_AUDIO)
static void audio_open_page(lv_event_t *event)
{
    (void)event;
    if (carplay_connection_audio_open()) lv_obj_remove_flag(audio_panel, LV_OBJ_FLAG_HIDDEN);
}
static void audio_select(lv_event_t *event)
{
    unsigned id = (unsigned)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(event));
    carplay_connection_audio_select(id);
}
static lv_obj_t *audio_control(lv_obj_t *parent,const char *text,int x,int y,int w,unsigned id)
{
    lv_obj_t *b=button(parent,text,x,y,w,audio_select);
    lv_obj_set_user_data(b,(void *)(uintptr_t)id);return b;
}
static void audio_update(void)
{
    bool open, ready, exiting;
    audio_test_config_t config; int result; audio_test_status_t s;
    carplay_connection_audio_snapshot(&open, &ready, &exiting, &config, &result, &s);
    if (!open) { lv_obj_add_flag(audio_panel, LV_OBJ_FLAG_HIDDEN); return; }
    const char *names[]={"PCM tone","PCM silence","AAC fixture","Mic level","Mic loopback"};
    lv_label_set_text_fmt(audio_settings,"Selected: %s | %u Hz | %s | Volume %u%% %s",
      names[config.mode],config.mode==AUDIO_TEST_AAC?44100:
      config.mode>=AUDIO_TEST_MIC_LEVEL?48000:config.rate,
      config.channel==1?"Left":config.channel==2?"Right":"Both",s.volume,s.muted?"MUTED":"");
    lv_label_set_text_fmt(audio_note,
      "%s | Frames %u | Bytes %u | Writes %u\nInput peak L/R: %u / %u (0..32768) | Error %d\n%s",
      exiting?"Stopping / returning":!ready?"Releasing CarPlay audio...":s.running?"TEST RUNNING":"STOPPED / READY",
      s.frames,s.bytes,s.writes,s.peak_left,s.peak_right,result?result:s.error,
      config.mode==AUDIO_TEST_AAC?"Short synthetic AAC clip; tap Start to replay.":
      config.mode==AUDIO_TEST_MIC_LEVEL?"Speak near the microphone; input peaks should change.":
      config.mode==AUDIO_TEST_MIC_LOOP?"Microphone to speaker, max 10 s. Start at low volume.":
      "Direct PCM -> sound0 -> Speaker. Tone/silence lasts up to 10 s.");
    if(ready && !exiting)lv_obj_remove_state(audio_play,LV_STATE_DISABLED);
    else lv_obj_add_state(audio_play,LV_STATE_DISABLED);
    lv_bar_set_value(audio_meters[0],(int)s.peak_left,LV_ANIM_OFF);
    lv_bar_set_value(audio_meters[1],(int)s.peak_right,LV_ANIM_OFF);
    for(unsigned i=0;i<5;++i)lv_obj_set_style_bg_color(audio_mode_buttons[i],
        lv_color_hex(config.mode==i?0x287D68:0x245C80),0);
}
#endif

static void files_back(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(file_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(confirm_panel, LV_OBJ_FLAG_HIDDEN);
    carplay_connection_files_close();
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
    if (!carplay_connection_files_open()) return;
    lv_obj_remove_flag(file_panel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(file_note, "Reading pairing files...");
}
static void retry_connection(lv_event_t *event)
{
    (void)event;
    carplay_connection_retry();
}
static void select_mode(lv_event_t *event)
{
    carplay_connection_select_mode(lv_event_get_target(event) == mode_buttons[1]);
}
static void answer_bluetooth(lv_event_t *event)
{
    carplay_connection_pairing_reply(visible_pairing_request,
        lv_event_get_target(event) == bt_accept);
    lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
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
    lv_label_set_text_fmt(confirm_text,
        "Forget this pairing record?\n\n%s\n\n%s\nDevice identity will be kept.", name,
        !strcmp(name, "Bluetooth bonds") ?
        "Forget this display in iPhone Bluetooth settings, then pair again." :
        !strcmp(name, "rt-carplay-pairings.bin") ?
        "AirPlay controllers will need to pair again." :
        "This iPhone may ask you to tap Trust again.");
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
    unsigned count; bool more; int result;
    if (!carplay_connection_files_snapshot(&revision, visible, &count, &more, &result)) return;
    lv_obj_clean(file_list);
    for (unsigned i = 0; i < count; ++i) {
        char text[140];
        snprintf(text, sizeof(text), "%s (%u bytes)", visible[i].name, (unsigned)visible[i].bytes);
        lv_obj_t *row = button(file_list, text, 0, (int)i * 50, 810, select_file);
        lv_obj_set_user_data(row, visible[i].name);
    }
    lv_label_set_text_fmt(file_note,
        "Records: %u%s | Result: %d\nTap a record to forget it. Identity keys are protected.",
        count, more ? "+ (refresh after deletion for more)" : "", result);
}

static void update_page(lv_timer_t *timer)
{
    (void)timer;
    update_files();
#if defined(RTCARPLAY_AUDIO)
    audio_update();
#endif
    static int last_stage = -1;
    static int last_mode = -1;
    demo_status_t snapshot;
    if (!carplay_connection_status(&snapshot)) return;
    if (snapshot.wireless && snapshot.pairing.kind && snapshot.stage != DEMO_CLOSING &&
        snapshot.stage != DEMO_FAILED) {
        if (visible_pairing_request != snapshot.pairing.request) {
            visible_pairing_request = snapshot.pairing.request;
            if (snapshot.pairing.kind == RTCP_BT_PAIR_CONSENT)
                lv_label_set_text(bt_text, "Allow this Bluetooth pairing?\nConfirm only if you just started pairing on your iPhone.");
            else lv_label_set_text_fmt(bt_text,
                snapshot.pairing.kind == RTCP_BT_PAIR_COMPARE ?
                "Bluetooth pairing: %06u\n\nCheck that your iPhone shows the same number.\nThen confirm on both screens." :
                "Bluetooth pairing: %06u\n\nEnter this number on your iPhone.", snapshot.pairing.passkey);
            if (snapshot.pairing.kind == RTCP_BT_PAIR_DISPLAY) lv_obj_add_state(bt_accept, LV_STATE_DISABLED);
            else lv_obj_remove_state(bt_accept, LV_STATE_DISABLED);
            lv_obj_remove_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        visible_pairing_request = 0;
        lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
    }
    bool mode_update = last_mode != (int)snapshot.wireless;
    if (snapshot.stage == DEMO_LIVE && (int)snapshot.stage == last_stage && !mode_update) return;
    static uint32_t last_debug_ms;
    uint32_t now = (uint32_t)rt_tick_get_millisecond();
    if (debug_label && ((int)snapshot.stage != last_stage || now - last_debug_ms >= 1000U)) {
        const char *trust = snapshot.trust == 2 ? "REUSED" : snapshot.trust == 3 ? "SAVED" :
                            snapshot.trust == 1 ? "NEW" : snapshot.trust < 0 ? "I/O ERROR" : "UNKNOWN";
        lv_label_set_text_fmt(debug_label,
            "Mode: %s\nStage: %s\nUSB: %s\nIP: %s\nTrust record: %s\nAirPlay storage: %s (%d)\nRX / displayed: %u / %u\nDrops / decode errors: %u / %u\nHID sent / failed: %u / %u\nLast error: %d",
            snapshot.wireless ? "Wireless" : "USB",
            stage_name(snapshot.stage),
            snapshot.usb ? "ATTACHED" : "DETACHED", snapshot.ipv6[0] ? snapshot.ipv6 : "waiting",
            trust, snapshot.storage_error ? "FAILED" : "OK", snapshot.storage_error,
            snapshot.received, snapshot.frames, snapshot.drops, snapshot.errors,
            snapshot.hid_sent, snapshot.hid_failed, snapshot.last_error);
        last_debug_ms = now;
        if (snapshot.wireless)
            lv_label_set_text_fmt(pin_label,
                "Wi-Fi: %s\nBluetooth: %s | MFi: %s\nBT last HCI/security: %02x/%u | Bonds %u/%u: %s\nWi-Fi sent: %u | Availability: %u\nInvite: %u (+%u retries) | Link ACK: %u",
                snapshot.ssid[0] ? snapshot.ssid : "starting",
                snapshot.bt_connected ? "connected" : snapshot.bt_ready ? "discoverable" : "starting",
                snapshot.bt_authenticated ? "authenticated" : snapshot.bt_identified ? "identified" : "waiting",
                snapshot.pairing.hci_error, snapshot.pairing.security_error,
                snapshot.pairing.bond_count, snapshot.pairing.bond_capacity,
                snapshot.pairing.storage_error ? "I/O ERROR" : snapshot.pairing.persistent ? "storage enabled" : "RAM only",
                snapshot.wifi_configs, snapshot.availability,
                snapshot.invites, snapshot.reinvites, snapshot.invite_acks);
    }
    if ((int)snapshot.stage == last_stage && !mode_update) return;
    last_stage = (int)snapshot.stage;
    last_mode = snapshot.wireless;
    lv_obj_set_style_bg_color(mode_buttons[0], lv_color_hex(snapshot.wireless ? 0x245C80 : 0x287D68), 0);
    lv_obj_set_style_bg_color(mode_buttons[1], lv_color_hex(snapshot.wireless ? 0x287D68 : 0x245C80), 0);
    for (unsigned i = 0; i < 2; ++i) {
        if (snapshot.stage == DEMO_CLOSING || snapshot.stage == DEMO_LIVE || (i && !DEMO_HAS_WIRELESS))
            lv_obj_add_state(mode_buttons[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(mode_buttons[i], LV_STATE_DISABLED);
    }
    lv_label_set_text(step_labels[0], snapshot.wireless ? "01  Pair Bluetooth" : "01  Connect USB");
    lv_label_set_text(step_labels[1], snapshot.wireless ? "02  Join CarPlay Wi-Fi" : "02  Trust your display");
#if defined(RTCARPLAY_AUDIO)
    if(snapshot.stage==DEMO_WAIT || snapshot.stage==DEMO_FAILED)lv_obj_remove_state(audio_button,LV_STATE_DISABLED);
    else lv_obj_add_state(audio_button,LV_STATE_DISABLED);
#endif
    if (snapshot.stage == DEMO_WAIT || snapshot.stage == DEMO_FAILED)
        lv_obj_remove_state(manage_button, LV_STATE_DISABLED);
    else lv_obj_add_state(manage_button, LV_STATE_DISABLED);
    if (snapshot.stage == DEMO_FAILED) lv_obj_remove_state(retry_button, LV_STATE_DISABLED);
    else lv_obj_add_state(retry_button, LV_STATE_DISABLED);
    const char *title, *detail, *tag;
    switch (snapshot.stage) {
    case DEMO_WAIT:
        title = "Connect your iPhone";
        detail = snapshot.wireless ?
            "On your iPhone, open Settings > Bluetooth.\nPair with rt-carplay and allow CarPlay." :
            "Plug an unlocked iPhone into the USB port.\nCarPlay will connect automatically.";
        tag = "READY"; break;
    case DEMO_PAIR:
        title = snapshot.wireless ? "Pairing over Bluetooth" : "Trust this connection";
        detail = snapshot.wireless ?
            "Keep Bluetooth and Wi-Fi enabled on your iPhone.\nAccept pairing and CarPlay prompts on the phone." :
            "Keep your iPhone unlocked.\nTap Trust on the phone if asked. This may take a moment.";
        tag = "PAIRING"; break;
    case DEMO_NETWORK:
        title = "Preparing the connection";
        detail = snapshot.wireless ?
            "Authentication completed.\nWaiting for your iPhone to join CarPlay Wi-Fi..." :
            "Your iPhone is paired.\nSetting up the wired connection...";
        tag = "CONNECTING"; break;
    case DEMO_CONNECT:
        title = "Starting CarPlay";
        detail = "Allow CarPlay on your iPhone if asked.\nYour apps will appear here shortly.";
        tag = "CONNECTING"; break;
    case DEMO_LIVE:
        title = "CarPlay connected";
        detail = snapshot.wireless ?
            "Use the touch screen to interact with CarPlay.\nDisconnect CarPlay on your iPhone to return." :
            "Use the touch screen to interact with CarPlay.\nUnplug your iPhone to return to this page.";
        tag = "CONNECTED"; break;
    case DEMO_CLOSING:
        title = "Returning to the launch page";
        detail = "Closing the previous connection...";
        tag = "PLEASE WAIT"; break;
    case DEMO_FAILED:
        title = "Unable to connect";
        detail = snapshot.storage_error ?
            "AirPlay could not save its pairing record.\nCheck storage diagnostics, then reconnect your iPhone." :
            snapshot.wireless ?
            "Check Bluetooth and Wi-Fi on your iPhone.\nTap Retry, or choose USB. See diagnostics for the error." :
            "Unplug your iPhone, unlock it, then reconnect.\nSee connection diagnostics for the last error.";
        tag = "RECONNECT"; break;
    default:
        title = "Getting ready";
        detail = snapshot.wireless ? "Starting CarPlay Wi-Fi and Bluetooth..." :
                                    "Preparing the wired CarPlay receiver...";
        tag = "STARTING"; break;
    }
    lv_label_set_text(heading, title);
    lv_label_set_text(description, detail);
    lv_label_set_text(badge, tag);
    if (!snapshot.wireless && snapshot.stage >= DEMO_WAIT && snapshot.stage <= DEMO_CONNECT)
        lv_label_set_text_fmt(pin_label, "Pairing PIN: %s", snapshot.pin);
    else if (!snapshot.wireless) lv_label_set_text(pin_label, "");
    for (unsigned i = 0; i < 3; ++i) {
        bool active = (int)snapshot.stage >= DEMO_PAIR + (int)i && snapshot.stage <= DEMO_LIVE;
        lv_obj_set_style_bg_color(steps[i], lv_color_hex(active ? 0x38D9A9 : 0x2A3A50), 0);
    }
    /* Nothing animates or invalidates while live: video owns the visible layer. */
}

/* SDK calls this from lv_user_gui_init(), after display registration. */
void carplay_demo_launcher_init(void)
{
    if (pointer_lock != NULL) return;
    pointer_lock = rt_mutex_create("cpdemo", RT_IPC_FLAG_PRIO);
    if (pointer_lock == NULL) { rt_kprintf("[carplay-demo] no status mutex\n"); return; }
    if (carplay_video_sink_set_display_observer(carplay_launcher_video_event,
                                               NULL) != RTCP_OK)
        rt_kprintf("[carplay-demo] video observer registration failed\n");
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    int width = lv_display_get_horizontal_resolution(NULL);
    int height = lv_display_get_vertical_resolution(NULL);
    label(screen, "CARPLAY", 56, 38, width - 500, 0xE7EEF8, true);
    label(screen, "Choose USB or wireless to connect your iPhone.", 56, 78, width - 112, 0x91A7C0, false);
    mode_buttons[0] = button(screen, "USB", width - 410, 32, 160, select_mode);
    mode_buttons[1] = button(screen, DEMO_HAS_WIRELESS ? "Wireless" : "Wireless unavailable",
                             width - 234, 32, 178, select_mode);
    if (!DEMO_HAS_WIRELESS) lv_obj_add_state(mode_buttons[1], LV_STATE_DISABLED);
    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_pos(card, 56, 134);
    lv_obj_set_size(card, width - 424, height - 238);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x142238), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3A50), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_pad_all(card, 30, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    badge = label(card, "STARTING", 0, 0, width - 500, 0x38D9A9, false);
    heading = label(card, "Getting ready", 0, 48, width - 500, 0xFFFFFF, true);
    description = label(card, "Preparing the wired CarPlay receiver...", 0, 98,
                        width - 500, 0xB3C4DA, false);
    lv_obj_set_style_text_line_space(description, 10, 0);
    pin_label = label(card, "", 0, 170, width - 500, 0x91A7C0, false);
    lv_obj_t *debug_card = lv_obj_create(screen);
    lv_obj_set_pos(debug_card, width - 352, 134);
    lv_obj_set_size(debug_card, 296, height - 238);
    lv_obj_set_style_bg_color(debug_card, lv_color_hex(0x142238), 0);
    lv_obj_set_style_border_color(debug_card, lv_color_hex(0x2A3A50), 0);
    lv_obj_set_style_border_width(debug_card, 1, 0);
    lv_obj_set_style_radius(debug_card, 24, 0);
    lv_obj_set_style_pad_all(debug_card, 18, 0);
    lv_obj_remove_flag(debug_card, LV_OBJ_FLAG_SCROLLABLE);
    label(debug_card, "CONNECTION DIAGNOSTICS", 0, 0, 260, 0x38D9A9, false);
    debug_label = label(debug_card, "Waiting for diagnostics...", 0, 32, 260, 0xB3C4DA, false);
#if LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(debug_label, &lv_font_montserrat_12, 0);
#endif
    lv_obj_set_style_text_line_space(debug_label, 7, 0);
    for (unsigned i = 0; i < 3; ++i) {
        steps[i] = lv_obj_create(screen);
        lv_obj_set_size(steps[i], (width - 144) / 3, 4);
        lv_obj_set_pos(steps[i], 56 + (int)i * ((width - 144) / 3 + 16), height - 76);
        lv_obj_set_style_border_width(steps[i], 0, 0);
        lv_obj_set_style_bg_color(steps[i], lv_color_hex(0x2A3A50), 0);
    }
    step_labels[0] = label(screen, "01  Connect USB", 56, height - 57, 250, 0x91A7C0, false);
    step_labels[1] = label(screen, "02  Trust your display", width / 3 + 32, height - 57, 280, 0x91A7C0, false);
    step_labels[2] = label(screen, "03  Touch to explore", width * 2 / 3 + 8, height - 57, 280, 0x91A7C0, false);
    manage_button = button(screen, "Pair files", 56, height - 145, 180, files_open);
    retry_button = button(screen, "Retry connection", 252, height - 145, 200, retry_connection);
    lv_obj_add_state(manage_button, LV_STATE_DISABLED);
    lv_obj_add_state(retry_button, LV_STATE_DISABLED);
#if defined(RTCARPLAY_AUDIO)
    audio_button=button(screen,"Audio tests",470,height-145,170,audio_open_page);
    lv_obj_add_state(audio_button,LV_STATE_DISABLED);
    audio_panel=lv_obj_create(screen);
    lv_obj_set_pos(audio_panel,30,25);lv_obj_set_size(audio_panel,width-60,height-50);
    lv_obj_set_style_bg_color(audio_panel,lv_color_hex(0x142238),0);
    lv_obj_remove_flag(audio_panel,LV_OBJ_FLAG_SCROLLABLE);
    label(audio_panel,"SPEAKER & MICROPHONE TEST",10,0,width-130,0xFFFFFF,true);
    const char *test_modes[]={"PCM tone","PCM silence","AAC tone","Mic meter","Mic loopback"};
    for(unsigned i=0;i<5;++i)audio_mode_buttons[i]=audio_control(audio_panel,test_modes[i],10+i*176,55,164,i);
#if !defined(LPKG_RT_CARPLAY_AUDIO_CODECS) && !defined(RTCP_AUDIO_CODECS)
    lv_obj_add_state(audio_mode_buttons[2],LV_STATE_DISABLED);
#endif
#if !defined(RTCARPLAY_MICROPHONE)
    lv_obj_add_state(audio_mode_buttons[3],LV_STATE_DISABLED);lv_obj_add_state(audio_mode_buttons[4],LV_STATE_DISABLED);
#endif
    audio_control(audio_panel,"44.1 kHz",10,115,140,10);
    audio_control(audio_panel,"48 kHz",165,115,140,11);
    audio_control(audio_panel,"Both",335,115,130,20);
    audio_control(audio_panel,"Left",480,115,130,21);
    audio_control(audio_panel,"Right",625,115,130,22);
    audio_control(audio_panel,"Volume -",10,175,150,30);
    audio_control(audio_panel,"Volume +",175,175,150,31);
    audio_control(audio_panel,"Mute / unmute",340,175,180,32);
    audio_settings=label(audio_panel,"",10,235,width-130,0x38D9A9,false);
    audio_note=label(audio_panel,"Preparing audio tests...",10,275,width-130,0xB3C4DA,false);
    label(audio_panel,"Input L",10,370,80,0x91A7C0,false);
    label(audio_panel,"Input R",450,370,80,0x91A7C0,false);
    for(unsigned i=0;i<2;++i) {
        audio_meters[i]=lv_bar_create(audio_panel);
        lv_obj_set_pos(audio_meters[i],95+(int)i*440,372);
        lv_obj_set_size(audio_meters[i],305,16);lv_bar_set_range(audio_meters[i],0,32768);
    }
    audio_play=audio_control(audio_panel,"Start / restart",10,height-170,185,40);
    audio_control(audio_panel,"Stop",215,height-170,150,41);
    audio_control(audio_panel,"Back",385,height-170,150,42);
    lv_obj_add_flag(audio_panel,LV_OBJ_FLAG_HIDDEN);
#endif
    file_panel = lv_obj_create(screen);
    lv_obj_set_pos(file_panel, 30, 25); lv_obj_set_size(file_panel, width - 60, height - 50);
    lv_obj_set_style_bg_color(file_panel, lv_color_hex(0x142238), 0);
    lv_obj_remove_flag(file_panel, LV_OBJ_FLAG_SCROLLABLE);
    label(file_panel, "PAIRING FILES", 10, 0, 600, 0xFFFFFF, true);
    file_note = label(file_panel, "", 10, 40, width - 110, 0xB3C4DA, false);
    file_list = lv_obj_create(file_panel);
    lv_obj_set_pos(file_list, 10, 100); lv_obj_set_size(file_list, width - 120, height - 260);
    lv_obj_set_style_pad_all(file_list, 8, 0);
    button(file_panel, "Refresh", 10, height - 145, 150, files_refresh);
    button(file_panel, "Back", 180, height - 145, 150, files_back);
#if DEMO_HAS_WIRELESS
    button(file_panel, "Forget Bluetooth pairs", 350, height - 145, 250, select_bluetooth_bonds);
#else
    (void)&select_bluetooth_bonds;
#endif
    confirm_panel = lv_obj_create(file_panel);
    lv_obj_set_pos(confirm_panel, 40, 50); lv_obj_set_size(confirm_panel, width - 180, height - 180);
    lv_obj_remove_flag(confirm_panel, LV_OBJ_FLAG_SCROLLABLE);
    confirm_text = label(confirm_panel, "", 10, 10, width - 240, 0xFFFFFF, false);
    button(confirm_panel, "Cancel", 10, height - 285, 160, cancel_forget);
    button(confirm_panel, "Forget record", 200, height - 285, 180, confirm_forget);
    lv_obj_add_flag(confirm_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(file_panel, LV_OBJ_FLAG_HIDDEN);
    bt_panel = lv_obj_create(screen);
    lv_obj_set_pos(bt_panel, 80, 120); lv_obj_set_size(bt_panel, width - 160, height - 240);
    lv_obj_remove_flag(bt_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bt_panel, lv_color_hex(0x142238), 0);
    bt_text = label(bt_panel, "", 20, 20, width - 240, 0xFFFFFF, true);
    bt_accept = button(bt_panel, "Numbers match / Allow", 20, height - 340, 260, answer_bluetooth);
    button(bt_panel, "Cancel pairing", 300, height - 340, 200, answer_bluetooth);
    lv_obj_add_flag(bt_panel, LV_OBJ_FLAG_HIDDEN);
    lv_indev_t *pointer = lv_indev_create();
    if (pointer) {
        lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(pointer, pointer_read);
    }
    lv_timer_create(update_page, 250, NULL);
    int touch = carplay_touch_panel_start((uint16_t)width, (uint16_t)height,
                                          launcher_panel_touch, NULL);
    if (touch != RTCP_OK) rt_kprintf("[carplay-demo] launch page touch error=%d\n", touch);
    int result = carplay_connection_manager_start();
    if (result != RTCP_OK) rt_kprintf("[carplay-demo] connection manager error=%d\n", result);
}

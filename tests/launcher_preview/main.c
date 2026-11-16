/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual LVGL 9 rendering of the V4 pages. Connection, audio, backlight and
 * OTA values are modeled for layout and routing tests; they are never hardware
 * acceptance evidence. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../../ui/lvgl_launcher.c"

static uint8_t pixels[1024 * 600 * 4];
static demo_status_t model;
static unsigned preview_ms = 183000;

/* ---- modeled hosts ---- */
static bool volume_supported = true;
static unsigned model_volume = 60;
static unsigned volume_writes, volume_requests;
static bool backlight_supported = true;
static unsigned model_backlight = 72;
static unsigned backlight_writes;
static bool files_opened, microphone_muted;
static unsigned file_version, replies;
static carplay_internet_status_t network_model;
static carplay_ota_status_t ota_model;
static unsigned ota_checks, ota_installs;
static unsigned manager_calls;
static unsigned stored_preferences[CARPLAY_PREF_COUNT];
static bool preferences_error;

uint32_t rt_tick_get_millisecond(void)
{
    return preview_ms;
}
rt_mutex_t rt_mutex_create(const char *n, int f)
{
    (void)n;
    (void)f;
    return (void *)1;
}
int rt_mutex_take(rt_mutex_t m, int w)
{
    (void)m;
    (void)w;
    return 0;
}
int rt_mutex_release(rt_mutex_t m)
{
    (void)m;
    return 0;
}
int rt_kprintf(const char *f, ...)
{
    (void)f;
    return 0;
}

int carplay_preferences_init(void)
{
    return 0;
}
unsigned carplay_preferences_get(carplay_preference_t key, unsigned fallback)
{
    return stored_preferences[key] ? stored_preferences[key] : fallback;
}
int carplay_preferences_set(carplay_preference_t key, unsigned value)
{
    stored_preferences[key] = value;
    return 0;
}
int carplay_preferences_error(void)
{
    return preferences_error;
}

bool carplay_volume_available(void)
{
    return volume_supported;
}
bool carplay_volume_get(unsigned *percent)
{
    if (!volume_supported || !percent)
        return false;
    ++volume_requests;
    *percent = model_volume;
    return true;
}
bool carplay_volume_set(unsigned percent)
{
    if (!volume_supported || percent > 100)
        return false;
    ++volume_writes;
    model_volume = percent;
    return true;
}

bool carplay_backlight_available(void)
{
    return backlight_supported;
}
bool carplay_backlight_get(unsigned *percent)
{
    if (!backlight_supported || !percent)
        return false;
    *percent = model_backlight;
    return true;
}
bool carplay_backlight_set(unsigned percent)
{
    if (!backlight_supported || percent < CARPLAY_BRIGHTNESS_MIN || percent > 100)
        return false;
    ++backlight_writes;
    model_backlight = percent;
    return true;
}
void carplay_backlight_restore(unsigned percent)
{
    if (backlight_supported)
        model_backlight = percent;
}

void carplay_ota_status(carplay_ota_status_t *out)
{
    *out = ota_model;
}
int carplay_ota_check(void)
{
    ++ota_checks;
    ota_model.state = CARPLAY_OTA_CHECKING;
    ota_model.busy = true;
    return 0;
}
int carplay_ota_install(const char *commit)
{
    if (strcmp(commit, ota_model.remote_commit))
        return -1;
    ++ota_installs;
    ota_model.state = CARPLAY_OTA_DOWNLOADING;
    ota_model.busy = true;
    return 0;
}

void carplay_internet_status(carplay_internet_status_t *out)
{
    *out = network_model;
}
bool carplay_internet_check(void)
{
    return true;
}

bool carplay_connection_status(demo_status_t *s)
{
    *s = model;
    return true;
}
int carplay_connection_manager_start(void)
{
    return ++manager_calls <= 2 ? RTCP_ENOMEM : RTCP_OK;
}
int carplay_touch_panel_start(uint16_t w, uint16_t h, carplay_touch_panel_cb_t c, void *p)
{
    assert(w == 1024 && h == 600 && c);
    (void)p;
    return RTCP_OK;
}
bool carplay_launcher_video_is_active(void)
{
    return false;
}
void carplay_launcher_video_event(bool a, void *p)
{
    (void)a;
    (void)p;
}
bool carplay_video_sink_is_running(void)
{
    return false;
}
int carplay_video_sink_set_display_observer(carplay_video_display_cb_t c, void *p)
{
    assert(c);
    (void)p;
    return RTCP_OK;
}
bool carplay_app_is_running(void)
{
    return false;
}
int carplay_connection_touch(uint8_t p, uint16_t i, uint16_t x, uint16_t y)
{
    (void)p;
    (void)i;
    (void)x;
    (void)y;
    return RTCP_ESTATE;
}
bool carplay_connection_set_wireless_enabled(bool enabled)
{
    model.wireless_enabled = enabled;
    return true;
}
void carplay_audio_input_mute(bool muted)
{
    microphone_muted = muted;
}
bool carplay_audio_input_is_muted(void)
{
    return microphone_muted;
}
void carplay_connection_retry(void)
{
    ++model.reconnects;
}
void carplay_connection_pairing_reply(uint32_t r, bool a)
{
    (void)r;
    (void)a;
    ++replies;
}
bool carplay_connection_files_open(void)
{
    files_opened = true;
    ++file_version;
    return true;
}
void carplay_connection_files_close(void)
{
    files_opened = false;
}
void carplay_connection_files_refresh(void)
{
    ++file_version;
}
void carplay_connection_files_forget(const char *n)
{
    (void)n;
    ++file_version;
}
bool carplay_connection_files_snapshot(unsigned *rev,
                                       carplay_pair_file_t f[CARPLAY_PAIR_FILES_MAX], unsigned *count,
                                       bool *more, int *result)
{
    (void)f;
    if (!files_opened || *rev == file_version)
        return false;
    *rev = file_version;
    *count = 0;
    *more = false;
    *result = RTCP_OK;
    return true;
}

/* ---- helpers ---- */
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *data)
{
    (void)area;
    (void)data;
    lv_display_flush_ready(d);
}
static void screenshot(const char *directory, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    lv_refr_now(NULL);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n1024 600\n255\n");
    for (unsigned i = 0; i < 1024 * 600; ++i)
    {
        uint8_t rgb[] = { pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4] };
        assert(fwrite(rgb, 1, sizeof(rgb), f) == sizeof(rgb));
    }
    assert(fclose(f) == 0);
}
static void inside(lv_obj_t *object)
{
    lv_area_t a;
    lv_obj_get_coords(object, &a);
    assert(a.x1 >= 0 && a.y1 >= 0 && a.x2 < 1024 && a.y2 < 600);
}

static lv_obj_t *find_caption(lv_obj_t *parent, const char *caption)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
    {
        lv_obj_t *obj = lv_obj_get_child(parent, (int32_t)i);
        if (lv_obj_check_type(obj, &lv_label_class) &&
            !strcmp(lv_label_get_text(obj), caption))
            return obj;
        lv_obj_t *nested = find_caption(obj, caption);
        if (nested)
            return nested;
    }
    return NULL;
}

/* Walks up from a label to the clickable card that owns it. */
static lv_obj_t *owning_card(lv_obj_t *label)
{
    lv_obj_t *obj = lv_obj_get_parent(label);
    while (obj)
    {
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE))
            return obj;
        obj = lv_obj_get_parent(obj);
    }
    assert(!"no clickable card");
    return NULL;
}

static void click_caption(lv_obj_t *page, const char *caption)
{
    lv_obj_t *label = find_caption(page, caption);
    assert(label);
    lv_obj_send_event(owning_card(label), LV_EVENT_CLICKED, NULL);
}

/* The back control is the first clickable child the header creates. */
static void click_back(lv_obj_t *page)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(page); ++i)
    {
        lv_obj_t *child = lv_obj_get_child(page, (int32_t)i);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
        {
            lv_obj_send_event(child, LV_EVENT_CLICKED, NULL);
            return;
        }
    }
    assert(!"no back control");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    lv_display_t *d = lv_display_create(1024, 600);
    assert(d);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);

    model.stage = DEMO_WAIT;
    model.automatic = model.wireless = true;
    model.usb_ready = model.wireless_enabled = model.bt_ready = true;
    snprintf(model.pin, sizeof(model.pin), "3939");

    carplay_demo_launcher_init();
    assert(manager_calls == 1);
    update_page(NULL);
    assert(manager_calls == 2);
    preview_ms += 1000;
    update_page(NULL);
    assert(manager_calls == 3);
    update_page(NULL);
    assert(manager_calls == 3); /* One retry per second. */

    /* Dashboard: reference sample, explicit demo label, four visible actions. */
    screenshot(argv[1], "01-dashboard_dark_en");
    assert(!strcmp(lv_label_get_text(dash_speed), "26"));
    assert(!strcmp(lv_label_get_text(dash_soc), "78"));
    assert(!lv_obj_has_flag(dash_note, LV_OBJ_FLAG_HIDDEN));
    inside(dash_speed);
    inside(dash_soc);
    inside(dash_bar);
    assert(lv_bar_get_value(dash_bar) == 78);

    /* Connectivity: idle -> action is enabled, Android Auto row is inert. */
    click_caption(pages[PAGE_DASHBOARD], "Connectivity");
    assert(!lv_obj_has_flag(pages[PAGE_CONNECTIVITY], LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(pages[PAGE_DASHBOARD], LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "02-connectivity_dark_en");
    assert(!strcmp(lv_label_get_text(conn_state), "Waiting for phone"));
    assert(!lv_obj_has_state(conn_action, LV_STATE_DISABLED));
    inside(conn_state);
    inside(conn_action);
    unsigned retries = model.reconnects;
    lv_obj_send_event(conn_action, LV_EVENT_CLICKED, NULL);
    assert(model.reconnects == retries + 1);

    /* Settings: both sliders reach the real backends and persist on release. */
    click_back(pages[PAGE_CONNECTIVITY]);
    assert(!lv_obj_has_flag(pages[PAGE_DASHBOARD], LV_OBJ_FLAG_HIDDEN));
    click_caption(pages[PAGE_DASHBOARD], "Settings");
    assert(!lv_obj_has_flag(pages[PAGE_SETTINGS], LV_OBJ_FLAG_HIDDEN));
    update_page(NULL);
    screenshot(argv[1], "03-settings_dark_en");
    assert(!strcmp(lv_label_get_text(volume_value), "Volume 60%"));
    assert(!strcmp(lv_label_get_text(brightness_value), "Brightness 72%"));

    lv_slider_set_value(volume_slider, 25, LV_ANIM_OFF);
    lv_obj_send_event(volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(volume_writes && model_volume == 25);
    lv_obj_send_event(volume_slider, LV_EVENT_RELEASED, NULL);
    assert(stored_preferences[CARPLAY_PREF_VOLUME] == 25);
    lv_slider_set_value(volume_slider, 0, LV_ANIM_OFF);
    lv_obj_send_event(volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(volume_slider, LV_EVENT_RELEASED, NULL);
    assert(model_volume == 0 && stored_preferences[CARPLAY_PREF_VOLUME] == 0);
    lv_slider_set_value(volume_slider, 100, LV_ANIM_OFF);
    lv_obj_send_event(volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(volume_slider, LV_EVENT_RELEASED, NULL);
    assert(model_volume == 100 && stored_preferences[CARPLAY_PREF_VOLUME] == 100);

    lv_slider_set_value(brightness_slider, CARPLAY_BRIGHTNESS_MIN, LV_ANIM_OFF);
    lv_obj_send_event(brightness_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(brightness_slider, LV_EVENT_RELEASED, NULL);
    assert(model_backlight == CARPLAY_BRIGHTNESS_MIN);
    assert(stored_preferences[CARPLAY_PREF_BRIGHTNESS] == CARPLAY_BRIGHTNESS_MIN);
    lv_slider_set_value(brightness_slider, 75, LV_ANIM_OFF);
    lv_obj_send_event(brightness_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(brightness_slider, LV_EVENT_RELEASED, NULL);
    assert(model_backlight == 75 && stored_preferences[CARPLAY_PREF_BRIGHTNESS] == 75);
    assert(carplay_ui_dark());

    /* Theme and language switch immediately and persist. */
    lv_obj_send_event(theme_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(!carplay_ui_dark() && stored_preferences[CARPLAY_PREF_THEME] == 0);
    screenshot(argv[1], "03-settings_light_en");
    lv_obj_send_event(theme_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(carplay_ui_dark());
    lv_obj_send_event(language_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(carplay_ui_is_chinese() && stored_preferences[CARPLAY_PREF_LANGUAGE] == 1);
    screenshot(argv[1], "03-settings_dark_zh");
    assert(find_caption(pages[PAGE_SETTINGS], "媒体音量"));
    assert(find_caption(pages[PAGE_SETTINGS], "屏幕亮度"));
    assert(find_caption(pages[PAGE_SETTINGS], "外观"));
    assert(find_caption(pages[PAGE_SETTINGS], "语言"));
    assert(find_caption(pages[PAGE_SETTINGS], "CarPlay 显示"));
    assert(find_caption(pages[PAGE_SETTINGS], "系统更新"));
    lv_obj_send_event(language_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(!carplay_ui_is_chinese());

    /* Display mode without the compositor is refused, not faked. */
    lv_obj_send_event(display_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(strstr(lv_label_get_text(settings_hint), "Not supported"));
    screenshot(argv[1], "03-settings_dark_en");

    /* OTA page: real state strings, no progress without a real percentage. */
    click_caption(pages[PAGE_SETTINGS], "System update");
    assert(!lv_obj_has_flag(pages[PAGE_OTA], LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "04-ota_dark_en");
    assert(!strcmp(lv_label_get_text(ota_state), "Not checked"));
    assert(lv_obj_has_state(ota_check_button, LV_STATE_DISABLED));
    assert(lv_obj_has_flag(ota_progress, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(ota_install_button, LV_OBJ_FLAG_HIDDEN));

    network_model.available = network_model.route_ready = true;
    update_page(NULL);
    assert(!lv_obj_has_state(ota_check_button, LV_STATE_DISABLED));
    lv_obj_send_event(ota_check_button, LV_EVENT_CLICKED, NULL);
    assert(ota_checks == 1);
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(ota_state), "Checking the update service..."));

    snprintf(ota_model.current, sizeof(ota_model.current), "1.0.0");
    snprintf(ota_model.current_commit, sizeof(ota_model.current_commit),
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    snprintf(ota_model.slot, sizeof(ota_model.slot), "A");
    ota_model.state = CARPLAY_OTA_AVAILABLE;
    ota_model.busy = false;
    snprintf(ota_model.remote, sizeof(ota_model.remote), "1.0.1");
    snprintf(ota_model.remote_commit, sizeof(ota_model.remote_commit),
             "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(ota_state), "Update available"));
    assert(!lv_obj_has_flag(ota_install_button, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "04-ota-available_dark_en");

    lv_obj_send_event(ota_install_button, LV_EVENT_CLICKED, NULL);
    assert(!ota_installs && !lv_obj_has_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "04-ota-confirm_dark_en");
    ota_confirm_cancel(NULL);
    assert(!ota_installs);
    lv_obj_send_event(ota_install_button, LV_EVENT_CLICKED, NULL);
    ota_confirm_install(NULL);
    assert(ota_installs == 1);

    ota_model.state = CARPLAY_OTA_APPLYING;
    ota_model.progress = 60;
    ota_model.busy = true;
    update_page(NULL);
    assert(!lv_obj_has_flag(ota_progress, LV_OBJ_FLAG_HIDDEN));
    assert(strstr(lv_label_get_text(ota_state), "progress 60%"));
    screenshot(argv[1], "04-ota-applying_dark_en");
    ota_model.state = CARPLAY_OTA_FAILED;
    ota_model.busy = false;
    ota_model.progress = -1;
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(ota_state), "Update failed"));
    assert(lv_obj_has_flag(ota_progress, LV_OBJ_FLAG_HIDDEN));

    /* Chinese covers every page, not only Settings. */
    carplay_ui_set_chinese(true);
    screenshot(argv[1], "04-ota_dark_zh");
    click_back(pages[PAGE_OTA]);
    screenshot(argv[1], "01-dashboard_dark_zh");
    assert(find_caption(pages[PAGE_DASHBOARD], "仪表"));
    assert(find_caption(pages[PAGE_DASHBOARD], "当前速度"));
    assert(find_caption(pages[PAGE_DASHBOARD], "剩余电量"));
    assert(find_caption(pages[PAGE_DASHBOARD], "车机互联"));
    assert(find_caption(pages[PAGE_DASHBOARD], "系统设置"));
    click_caption(pages[PAGE_DASHBOARD], "车机互联");
    screenshot(argv[1], "02-connectivity_dark_zh");
    assert(find_caption(pages[PAGE_CONNECTIVITY], "等待连接"));
    carplay_ui_set_chinese(false);
    lv_obj_send_event(theme_buttons[0], LV_EVENT_CLICKED, NULL);
    screenshot(argv[1], "02-connectivity_light_en");

    /* A live session locks the local UI behind the phone video. */
    model.stage = DEMO_LIVE;
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(dash_status), "Connected"));
    assert(!strcmp(lv_label_get_text(conn_state), "Connected"));
    model.stage = DEMO_WAIT;
    update_page(NULL);

    /* Pairing consent still routes to the connection owner. */
    model.stage = DEMO_PAIR;
    model.wireless = true;
    model.pairing.kind = RTCP_BT_PAIR_COMPARE;
    model.pairing.request = 42;
    model.pairing.passkey = 121314;
    update_page(NULL);
    assert(!lv_obj_has_flag(bt_panel, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "05-pairing_dark_en");
    lv_obj_send_event(bt_accept, LV_EVENT_CLICKED, NULL);
    assert(replies == 1 && lv_obj_has_flag(bt_panel, LV_OBJ_FLAG_HIDDEN));

    /* Missing audio backend: the control is disabled rather than faked. */
    volume_supported = false;
    model.stage = DEMO_WAIT;
    update_page(NULL);
    lv_obj_send_event(pages[PAGE_DASHBOARD], LV_EVENT_CLICKED, NULL);
    carplay_launcher_open_display_settings();
    update_page(NULL);
    assert(lv_obj_has_state(volume_slider, LV_STATE_DISABLED));
    assert(!strcmp(lv_label_get_text(volume_value), "Not supported on this device"));
    screenshot(argv[1], "03-settings-no-audio_dark_en");

    puts("V4 LVGL pages: dashboard, connectivity, settings, OTA, pairing and "
         "unsupported-backend states rendered at 1024x600; modeled values only");
    return 0;
}

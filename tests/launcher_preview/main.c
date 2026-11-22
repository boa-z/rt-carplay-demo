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

const carplay_build_info_t carplay_build_info = {
    .sdk_version = "1.3.3",
    .rtcarplay_version = RTCP_VERSION_STRING,
    .commit = "0123456789abcdef0123456789abcdef01234567-dirty",
    .time_utc = "2026-11-21T16:42:19Z",
};

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
static char stored_bt_name[CARPLAY_BT_NAME_MAX + 1] = CARPLAY_BT_NAME_DEFAULT;
static bool preferences_error;
static rtcp_appearance_setting_t model_phone_appearance;
static carplay_phone_media_t model_phone_media;

void carplay_app_set_phone_appearance(rtcp_appearance_setting_t appearance)
{
    model_phone_appearance = appearance;
}
rtcp_appearance_setting_t carplay_app_phone_appearance(void)
{
    return model_phone_appearance;
}
bool carplay_app_phone_media(carplay_phone_media_t *media)
{
    *media = model_phone_media;
    return true;
}

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
void carplay_preferences_bt_name(char name[CARPLAY_BT_NAME_MAX + 1])
{
    memcpy(name, stored_bt_name, sizeof(stored_bt_name));
}
int carplay_preferences_set_bt_name(const char *name)
{
    if (!*name)
        return -EINVAL;
    snprintf(stored_bt_name, sizeof(stored_bt_name), "%s", name);
    return 0;
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

static void assert_settings_page(settings_page_t expected)
{
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
    {
        bool selected = index == (unsigned)expected;
        assert(lv_obj_has_flag(settings_pages[index], LV_OBJ_FLAG_HIDDEN) == !selected);
        assert(lv_obj_get_scroll_dir(settings_pages[index]) == LV_DIR_VER);
    }
}

static void assert_header_row(lv_obj_t *page, const char *caption)
{
    lv_obj_t *heading = find_caption(page, caption);
    lv_obj_t *back = lv_obj_get_child(page, 0);
    assert(heading && lv_obj_has_flag(back, LV_OBJ_FLAG_CLICKABLE));
    lv_obj_update_layout(page);
    lv_area_t heading_area, back_area;
    lv_obj_get_coords(heading, &heading_area);
    lv_obj_get_coords(back, &back_area);
    assert(heading_area.x1 > back_area.x2);
    int heading_center = (heading_area.y1 + heading_area.y2) / 2;
    assert(heading_center >= back_area.y1 && heading_center <= back_area.y2);
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

LV_FONT_DECLARE(carplay_cjk_16);
LV_FONT_DECLARE(carplay_cjk_18);
LV_FONT_DECLARE(carplay_cjk_24);
LV_FONT_DECLARE(carplay_cjk_30);

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
    assert_header_row(pages[PAGE_CONNECTIVITY], "Connectivity");
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
    assert_settings_page(SETTINGS_DISPLAY);
    update_page(NULL);
    screenshot(argv[1], "03-settings_dark_en");
    lv_obj_t *display_cards[] = {
        lv_obj_get_parent(theme_buttons[0]),
        lv_obj_get_parent(display_buttons[0]),
        lv_obj_get_parent(fps_buttons[0]),
    };
    lv_area_t display_page_area;
    lv_obj_get_coords(settings_pages[SETTINGS_DISPLAY], &display_page_area);
    for (unsigned index = 0; index < sizeof(display_cards) / sizeof(display_cards[0]); ++index)
    {
        lv_area_t card_area;
        lv_obj_get_coords(display_cards[index], &card_area);
        assert(lv_area_get_height(&card_area) == lv_obj_get_height(display_cards[0]));
        assert(card_area.y1 >= display_page_area.y1 && card_area.y2 <= display_page_area.y2);
    }
    lv_obj_send_event(fps_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(stored_preferences[CARPLAY_PREF_HIGH_FPS] == 1);
    assert(!strcmp(lv_label_get_text(settings_hint), "Saved. Restart the receiver to apply."));
    screenshot(argv[1], "03-settings-frame-rate_dark_en");
    assert(!strcmp(lv_label_get_text(volume_value), "60%"));
    assert(!strcmp(lv_label_get_text(brightness_value), "72%"));

    lv_obj_scroll_to_view(settings_tabs[SETTINGS_BLUETOOTH], LV_ANIM_OFF);
    click_caption(pages[PAGE_SETTINGS], "Bluetooth");
    assert_settings_page(SETTINGS_BLUETOOTH);
    assert(!strcmp(lv_textarea_get_text(bluetooth_name), CARPLAY_BT_NAME_DEFAULT));
    lv_obj_send_event(bluetooth_name, LV_EVENT_CLICKED, NULL);
    assert(!lv_obj_has_flag(bluetooth_keyboard, LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(settings_pages[SETTINGS_BLUETOOTH]);
    lv_area_t bluetooth_keyboard_area, bluetooth_page_area;
    lv_obj_get_coords(bluetooth_keyboard, &bluetooth_keyboard_area);
    lv_obj_get_coords(settings_pages[SETTINGS_BLUETOOTH], &bluetooth_page_area);
    assert(bluetooth_keyboard_area.y1 == bluetooth_page_area.y1 + 174);
    assert(bluetooth_keyboard_area.y2 <= bluetooth_page_area.y2);
    screenshot(argv[1], "03-settings-bluetooth-keyboard_dark_en");
    lv_textarea_set_text(bluetooth_name, "");
    click_caption(settings_pages[SETTINGS_BLUETOOTH], "Apply");
    assert(!strcmp(stored_bt_name, CARPLAY_BT_NAME_DEFAULT));
    lv_textarea_set_text(bluetooth_name, "D213 CarPlay");
    click_caption(settings_pages[SETTINGS_BLUETOOTH], "Apply");
    assert(!strcmp(stored_bt_name, "D213 CarPlay"));
    assert(lv_obj_has_flag(bluetooth_keyboard, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "03-settings-bluetooth_dark_en");

    click_caption(pages[PAGE_SETTINGS], "Sound");
    assert_settings_page(SETTINGS_SOUND);
    screenshot(argv[1], "03-settings-sound_dark_en");
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

    click_caption(pages[PAGE_SETTINGS], "Display");
    assert_settings_page(SETTINGS_DISPLAY);
    assert(!strcmp(lv_label_get_text(volume_value), "100%"));
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
    assert_header_row(pages[PAGE_SETTINGS], "Settings");
    lv_obj_send_event(theme_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(carplay_ui_dark());
    click_caption(pages[PAGE_SETTINGS], "Language");
    assert_settings_page(SETTINGS_LANGUAGE);
    lv_obj_send_event(language_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(carplay_ui_is_chinese() && stored_preferences[CARPLAY_PREF_LANGUAGE] == 1);
    screenshot(argv[1], "03-settings_dark_zh");
    assert_header_row(pages[PAGE_SETTINGS], "系统设置");
    assert(lv_obj_get_height(settings_pages[SETTINGS_LANGUAGE]) > 386);
    assert(find_caption(pages[PAGE_SETTINGS], "媒体音量"));
    assert(find_caption(pages[PAGE_SETTINGS], "屏幕亮度"));
    assert(find_caption(pages[PAGE_SETTINGS], "外观"));
    assert(find_caption(pages[PAGE_SETTINGS], "语言"));
    assert(find_caption(pages[PAGE_SETTINGS], "CarPlay 显示"));
    assert(find_caption(pages[PAGE_SETTINGS], "系统更新"));
    click_caption(pages[PAGE_SETTINGS], "关于");
    assert_settings_page(SETTINGS_ABOUT);
    assert(find_caption(settings_pages[SETTINGS_ABOUT], "构建哈希"));
    assert(find_caption(settings_pages[SETTINGS_ABOUT], "构建时间 (UTC)"));
    screenshot(argv[1], "03-settings-about_dark_zh");
    click_caption(pages[PAGE_SETTINGS], "手机控制");
    assert_settings_page(SETTINGS_PHONE);
    lv_obj_send_event(phone_appearance_buttons[RTCP_APPEARANCE_LIGHT], LV_EVENT_CLICKED, NULL);
    assert(model_phone_appearance == RTCP_APPEARANCE_LIGHT && carplay_ui_dark());
    model_phone_media = (carplay_phone_media_t){ .title = "Test Track", .artist = "Test Artist", .has_playing = true, .playing = true, .route_destination = "Central Station", .route_road = "Main Street", .route_remaining = "2 km" };
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(phone_title), "Test Track"));
    assert(!strcmp(lv_label_get_text(phone_artist), "Test Artist"));
    assert(!strcmp(lv_label_get_text(phone_playback), "播放中"));
    assert(!strcmp(lv_label_get_text(phone_destination), "Central Station"));
    assert(!strcmp(lv_label_get_text(phone_road), "Main Street"));
    assert(!strcmp(lv_label_get_text(phone_remaining), "2 km"));
    assert(find_caption(settings_pages[SETTINGS_PHONE], "路线引导"));
    model_phone_media = (carplay_phone_media_t){ .title = "龘\U00020087 Live", .artist = "麤樂隊", .route_destination = "龘龍站" };
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(phone_title), "龘\U00020087 Live"));
    assert(lv_obj_get_style_text_font(phone_title, 0) == &carplay_cjk_full_18);
    assert(lv_obj_get_style_text_font(phone_destination, 0) == &carplay_cjk_full_18);
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&carplay_cjk_full_18, &glyph, 0x9f98, 0) && !glyph.is_placeholder);
    assert(lv_font_get_glyph_dsc(&carplay_cjk_full_18, &glyph, 0x20087, 0) && !glyph.is_placeholder);
    const lv_font_t *clock_fonts[] = { &carplay_cjk_16, &carplay_cjk_18, &carplay_cjk_24, &carplay_cjk_30 };
    const uint32_t clock_glyphs[] = { 0x4e0e, 0x504f, 0x540c, 0x5730, 0x671f, 0x6b65, 0x6cd5, 0x79fb, 0x949f };
    for (size_t font = 0; font < sizeof(clock_fonts) / sizeof(clock_fonts[0]); ++font)
        for (size_t index = 0; index < sizeof(clock_glyphs) / sizeof(clock_glyphs[0]); ++index)
            assert(lv_font_get_glyph_dsc(clock_fonts[font], &glyph, clock_glyphs[index], 0) &&
                   !glyph.is_placeholder);
    carplay_ui_set_chinese(false);
    update_page(NULL);
    assert(find_caption(settings_pages[SETTINGS_PHONE], "Route guidance"));
    assert(!strcmp(lv_label_get_text(phone_artist), "麤樂隊"));
    assert(lv_obj_get_style_text_font(phone_artist, 0) == &carplay_cjk_full_18);
    carplay_ui_set_chinese(true);
    update_page(NULL);
    screenshot(argv[1], "03-settings-phone_dark_zh");
    memset(&model_phone_media, 0, sizeof(model_phone_media));
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(phone_title), "-") && !*lv_label_get_text(phone_playback));
    assert(!strcmp(lv_label_get_text(phone_destination), "-") && !*lv_label_get_text(phone_road) &&
           !*lv_label_get_text(phone_remaining));
    lv_obj_scroll_to_view(settings_tabs[SETTINGS_GNSS], LV_ANIM_OFF);
    click_caption(pages[PAGE_SETTINGS], "模拟定位");
    assert_settings_page(SETTINGS_GNSS);
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(gnss_state), "模拟定位已关闭"));
    lv_obj_send_event(gnss_latitude, LV_EVENT_CLICKED, NULL);
    assert(!lv_obj_has_flag(gnss_keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_color_eq(lv_obj_get_style_bg_color(gnss_latitude, LV_PART_MAIN),
                       lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE2))));
    assert(lv_color_eq(lv_obj_get_style_bg_color(gnss_keyboard, LV_PART_ITEMS),
                       lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE2))));
    lv_color_t dark_input = lv_obj_get_style_bg_color(gnss_latitude, LV_PART_MAIN);
    carplay_ui_set_dark(false);
    assert(!lv_color_eq(dark_input, lv_obj_get_style_bg_color(gnss_latitude, LV_PART_MAIN)));
    assert(lv_color_eq(lv_obj_get_style_bg_color(gnss_keyboard, LV_PART_ITEMS),
                       lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE2))));
    carplay_ui_set_dark(true);
    lv_textarea_set_text(gnss_latitude, "-33.450000");
    lv_textarea_set_text(gnss_longitude, "-70.675000");
    screenshot(argv[1], "03-settings-gnss-keyboard_dark_zh");
    inside(gnss_keyboard);
    lv_obj_send_event(gnss_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(lv_obj_has_flag(gnss_keyboard, LV_OBJ_FLAG_HIDDEN));
    carplay_gnss_config_t gnss;
    assert(carplay_gnss_get_config(&gnss) && gnss.enabled && gnss.latitude == -33450000 &&
           gnss.longitude == -70675000);
    lv_textarea_set_text(gnss_latitude, "91");
    lv_obj_send_event(gnss_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(!strcmp(lv_label_get_text(settings_hint), "坐标格式或范围有误"));
    lv_textarea_set_text(gnss_latitude, "-33.450000");
    lv_obj_send_event(gnss_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(carplay_gnss_get_config(&gnss) && !gnss.enabled);
    update_page(NULL);
    screenshot(argv[1], "03-settings-gnss_dark_zh");
    lv_obj_send_event(gnss_mode_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(!lv_obj_has_state(gnss_end_latitude, LV_STATE_DISABLED));
    lv_textarea_set_text(gnss_end_latitude, "-33.450000");
    lv_textarea_set_text(gnss_end_longitude, "-70.674000");
    lv_textarea_set_text(gnss_speed, "0");
    lv_obj_send_event(gnss_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(!strcmp(lv_label_get_text(settings_hint), "速度须为 1 至 300 km/h"));
    lv_textarea_set_text(gnss_speed, "36");
    lv_obj_send_event(gnss_speed, LV_EVENT_CLICKED, NULL);
    screenshot(argv[1], "03-settings-gnss-route_dark_zh");
    inside(gnss_keyboard);
    lv_obj_send_event(gnss_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(carplay_gnss_get_config(&gnss) && gnss.enabled && gnss.moving &&
           gnss.end_latitude == -33450000 && gnss.end_longitude == -70674000 && gnss.speed_kmh == 36);
    lv_obj_send_event(gnss_apply_button, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(gnss_buttons[0], LV_EVENT_CLICKED, NULL);
    click_caption(pages[PAGE_SETTINGS], "语言");
    lv_obj_send_event(language_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(!carplay_ui_is_chinese());

    /* Display mode without the compositor is refused, not faked. */
    click_caption(pages[PAGE_SETTINGS], "Display");
    lv_obj_send_event(display_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(strstr(lv_label_get_text(settings_hint), "Not supported"));
    screenshot(argv[1], "03-settings_dark_en");

    click_caption(pages[PAGE_SETTINGS], "About");
    assert_settings_page(SETTINGS_ABOUT);
    const char *about_values[] = {
        carplay_build_info.sdk_version,
        carplay_build_info.rtcarplay_version,
        carplay_build_info.commit,
        carplay_build_info.time_utc,
    };
    for (unsigned index = 0; index < sizeof(about_values) / sizeof(about_values[0]); ++index)
    {
        lv_obj_t *value = find_caption(settings_pages[SETTINGS_ABOUT], about_values[index]);
        assert(value);
        inside(value);
    }
    screenshot(argv[1], "03-settings-about_dark_en");
    carplay_launcher_open_display_settings();
    assert_settings_page(SETTINGS_DISPLAY);
    click_caption(pages[PAGE_SETTINGS], "About");

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
    assert(!strcmp(lv_label_get_text(ota_state), "Checking for updates..."));

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
    assert_header_row(pages[PAGE_OTA], "系统更新");
    click_back(pages[PAGE_OTA]);
    assert(!lv_obj_has_flag(pages[PAGE_SETTINGS], LV_OBJ_FLAG_HIDDEN));
    assert_settings_page(SETTINGS_ABOUT);
    click_back(pages[PAGE_SETTINGS]);
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
    assert(!strcmp(lv_label_get_text(volume_value), "Not supported"));
    screenshot(argv[1], "03-settings-no-audio_dark_en");

    puts("V4 LVGL pages: dashboard, connectivity, settings, OTA, pairing and "
         "unsupported-backend states rendered at 1024x600; modeled values only");
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual LVGL 9 rendering of the Cockpit pages. Connection, audio, backlight and
 * OTA values are modeled for layout and routing tests; they are never hardware
 * acceptance evidence. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../../ui/lvgl_launcher.c"
#include "touch_panel_lubanlite.h"
#if LV_USE_SDL
#include <SDL2/SDL.h>
#endif

static uint8_t *pixels;
static int preview_width = 1024, preview_height = 600;
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
static carplay_wifi_status_t wifi_model;
static carplay_ota_status_t ota_model;
static unsigned ota_installs;
static unsigned manager_calls;
static unsigned stored_preferences[CARPLAY_PREF_COUNT];
static char stored_bt_name[CARPLAY_BT_NAME_MAX + 1] = CARPLAY_BT_NAME_DEFAULT;
static bool preferences_error;
static rtcp_appearance_setting_t model_phone_appearance;
static carplay_phone_media_t model_phone_media;
static bool cover_read_busy;
static bool clock_received;
static bool clock_automatic = true;
static int clock_offset;
static time_t rtc_seconds = 1795279339;
static carplay_time_status_t network_time_model;
static char time_server_model[CARPLAY_TIME_SERVER_MAX] = "time.cloudflare.com";
static char time_server_requested[CARPLAY_TIME_SERVER_MAX];

void carplay_network_time_start(void)
{
    network_time_model = CARPLAY_TIME_SYNCING;
    strcpy(time_server_requested, time_server_model);
}

carplay_time_status_t carplay_network_time_status(void)
{
    return network_time_model;
}

void carplay_network_time_server_get(char server[CARPLAY_TIME_SERVER_MAX])
{
    strcpy(server, time_server_model);
}

int carplay_network_time_server_set(const char *server)
{
    if (!*server || strstr(server, "://"))
        return -RT_EINVAL;
    strcpy(time_server_model, server);
    return RT_EOK;
}

int carplay_network_time_server_reset(void)
{
    return carplay_network_time_server_set("time.cloudflare.com");
}

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

size_t carplay_app_phone_artwork(uint32_t revision, uint8_t *buffer, size_t capacity)
{
    if (cover_read_busy)
        return 0;
    if (revision == 3)
    {
        FILE *file = fopen(CARPLAY_PREVIEW_JPEG, "rb");
        assert(file);
        size_t length = fread(buffer, 1, capacity, file);
        assert(feof(file) && !ferror(file));
        fclose(file);
        return length;
    }
    static const uint8_t png[] = {
        0x89,
        0x50,
        0x4e,
        0x47,
        0x0d,
        0x0a,
        0x1a,
        0x0a,
        0x00,
        0x00,
        0x00,
        0x0d,
        0x49,
        0x48,
        0x44,
        0x52,
        0x00,
        0x00,
        0x00,
        0x20,
        0x00,
        0x00,
        0x00,
        0x20,
        0x08,
        0x02,
        0x00,
        0x00,
        0x00,
        0xfc,
        0x18,
        0xed,
        0xa3,
        0x00,
        0x00,
        0x00,
        0x2a,
        0x49,
        0x44,
        0x41,
        0x54,
        0x78,
        0x9c,
        0x63,
        0x30,
        0xae,
        0xd8,
        0x43,
        0x53,
        0xc4,
        0x30,
        0x6a,
        0xc1,
        0xa8,
        0x05,
        0xa3,
        0x16,
        0x8c,
        0x5a,
        0x30,
        0x6a,
        0xc1,
        0xa8,
        0x05,
        0xa3,
        0x16,
        0x8c,
        0x5a,
        0x30,
        0x6a,
        0xc1,
        0xa8,
        0x05,
        0xa3,
        0x16,
        0x0c,
        0x15,
        0x0b,
        0x00,
        0xa6,
        0x2f,
        0x9c,
        0x4c,
        0xbb,
        0x5b,
        0xd5,
        0xf9,
        0x00,
        0x00,
        0x00,
        0x00,
        0x49,
        0x45,
        0x4e,
        0x44,
        0xae,
        0x42,
        0x60,
        0x82,
    };
    if (capacity < sizeof(png) || (revision != 1 && revision != 2))
        return 0;
    memcpy(buffer, png, sizeof(png));
    if (revision == 2)
        buffer[16] = 1;
    return sizeof(png);
}

int carplay_app_clock_offset_minutes(void)
{
    return clock_offset;
}
bool carplay_app_clock_is_automatic(void)
{
    return clock_automatic;
}
bool carplay_app_phone_clock_received(void)
{
    return clock_received;
}
int carplay_app_set_clock_automatic(bool automatic)
{
    clock_automatic = automatic;
    return 0;
}
int carplay_app_set_local_clock(int64_t local_seconds, int offset_minutes)
{
    rtc_seconds = (time_t)(local_seconds - offset_minutes * 60);
    clock_offset = offset_minutes;
    return 0;
}
rt_device_t rt_device_find(const char *name)
{
    return !strcmp(name, "rtc") ? &rtc_seconds : NULL;
}
int rt_device_control(rt_device_t device, int command, void *argument)
{
    if (device != &rtc_seconds || command != RT_DEVICE_CTRL_RTC_GET_TIME)
        return -1;
    *(time_t *)argument = rtc_seconds;
    return RT_EOK;
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
void carplay_ota_boot_health_ready(void)
{
}
int carplay_ota_download(void)
{
    assert(ota_model.source[0]);
    ota_model.state = CARPLAY_OTA_DOWNLOADING;
    ota_model.busy = true;
    return 0;
}
int carplay_ota_install(uint32_t candidate)
{
    if (candidate != ota_model.candidate)
        return -1;
    ++ota_installs;
    ota_model.state = CARPLAY_OTA_APPLYING;
    ota_model.busy = true;
    return 0;
}
int carplay_ota_cancel(void)
{
    ota_model.state = CARPLAY_OTA_IDLE;
    ota_model.busy = false;
    return 0;
}
int carplay_ota_source_set(const char *url)
{
    if (strncmp(url, "https://", 8) && strncmp(url, "http://", 7))
        return -RT_EINVAL;
    snprintf(ota_model.source, sizeof(ota_model.source), "%s", url);
    ++ota_model.candidate;
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

void carplay_wifi_status(carplay_wifi_status_t *out)
{
    *out = wifi_model;
}

int carplay_wifi_enable(bool enabled)
{
    wifi_model.enabled = enabled;
    if (enabled)
        wifi_model.ap_active = false;
    wifi_model.connected = wifi_model.addressed = false;
    return RT_EOK;
}

int carplay_wifi_scan(void)
{
    wifi_model.scan_revision++;
    wifi_model.count = 1;
    wifi_model.results[0] = (carplay_wifi_access_point_t){
        .ssid = "Workshop",
        .security = "WPA2",
        .channel = 36,
        .rssi = -42
    };
    return RT_EOK;
}

int carplay_wifi_join(unsigned revision, unsigned index, const char *password)
{
    assert(revision == wifi_model.scan_revision && index == 0 && !strcmp(password, "preview-pass"));
    wifi_model.connected = true;
    strcpy(wifi_model.connected_ssid, wifi_model.results[index].ssid);
    return RT_EOK;
}

int carplay_wifi_forget(void)
{
    wifi_model.connected = wifi_model.addressed = false;
    wifi_model.connected_ssid[0] = '\0';
    return RT_EOK;
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
static carplay_touch_panel_cb_t preview_touch;

int carplay_touch_panel_start(uint16_t w, uint16_t h, carplay_touch_panel_cb_t c, void *p)
{
    assert(w == preview_width && h == preview_height && c);
    preview_touch = c;
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
    fprintf(f, "P6\n%d %d\n255\n", preview_width, preview_height);
    for (int i = 0; i < preview_width * preview_height; ++i)
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

static void assert_dashboard_header(void)
{
    lv_obj_update_layout(pages[PAGE_DASHBOARD]);
    lv_area_t dot, status, wifi, ethernet, clock;
    lv_obj_get_coords(dash_dot, &dot);
    lv_obj_get_coords(dash_status, &status);
    lv_obj_get_coords(dash_wifi, &wifi);
    lv_obj_get_coords(dash_ethernet, &ethernet);
    lv_obj_get_coords(dash_clock, &clock);
    assert(dot.x2 < status.x1);
    assert(status.x2 < wifi.x1 && wifi.x2 < ethernet.x1);
    assert(ethernet.x2 < clock.x2 - label_width(dash_clock) + 1);
    const lv_area_t *elements[] = { &dot, &status, &wifi, &ethernet, &clock };
    for (unsigned index = 0; index < sizeof(elements) / sizeof(elements[0]); ++index)
    {
        const lv_area_t *area = elements[index];
        assert(abs(area->y1 + area->y2 + 1 - 2 * DASH_STATUS_CENTER_Y) <= 1);
        assert(area->y1 >= 0 && area->y2 < 54);
    }
}

static void tap(int32_t x, int32_t y)
{
    lv_indev_t *pointer = lv_indev_get_next(NULL);
    assert(pointer);
    preview_touch(RTCP_TOUCH_DOWN, 0, (uint16_t)x, (uint16_t)y, NULL);
    lv_indev_read(pointer);
    preview_touch(RTCP_TOUCH_UP, 0, (uint16_t)x, (uint16_t)y, NULL);
    lv_indev_read(pointer);
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

static void open_setting(settings_page_t route)
{
    show_page(PAGE_SETTINGS);
    settings_page_t section = settings_section(route);
    click_caption(lv_obj_get_parent(settings_tabs[section]), TR(settings_titles[section]));
    if (route != section)
        click_caption(settings_pages[section], TR(settings_titles[route]));
    assert_settings_page(route);
}

static void assert_header_row(lv_obj_t *page, const char *caption)
{
    lv_obj_t *heading = find_caption(page, caption);
    lv_obj_t *back = NULL;
    for (uint32_t index = 0; index < lv_obj_get_child_count(page); ++index)
    {
        lv_obj_t *child = lv_obj_get_child(page, (int32_t)index);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
        {
            back = child;
            break;
        }
    }
    assert(heading && back);
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

static void seed_models(void)
{
    model.stage = DEMO_WAIT;
    model.automatic = model.wireless = true;
    model.usb_ready = model.wireless_enabled = model.bt_ready = true;
    snprintf(model.pin, sizeof(model.pin), "3939");
}

static void tap_object(lv_obj_t *object)
{
    lv_obj_update_layout(object);
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    lv_point_t center = { (bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2 };
    lv_obj_transform_point(object, &center, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
    assert(center.x >= 0 && center.x < preview_width &&
           center.y >= 0 && center.y < preview_height);
    tap(center.x, center.y);
}

static void render_layout(const char *directory)
{
    carplay_demo_launcher_init();
    update_page(NULL);
    screenshot(directory, "dashboard_dark_en");
    tap_object(shell.tabs[PAGE_SETTINGS]);
    assert(!lv_obj_has_flag(pages[PAGE_SETTINGS], LV_OBJ_FLAG_HIDDEN));
    screenshot(directory, "settings_dark_en");
    tap_object(theme_buttons[0]);
    assert(!carplay_ui_dark());
    carplay_ui_set_chinese(true);
    screenshot(directory, "settings_light_zh");
    tap_object(settings_tabs[SETTINGS_CONNECTIONS]);
    assert_settings_page(SETTINGS_CONNECTIONS);
    tap_object(shell.tabs[PAGE_CONNECTIVITY]);
    assert(!lv_obj_has_flag(pages[PAGE_CONNECTIVITY], LV_OBJ_FLAG_HIDDEN));
    model.stage = DEMO_LIVE;
    update_page(NULL);
    screenshot(directory, "connectivity_light_zh");
}

#if LV_USE_SDL
static int interactive(void)
{
    if (!lv_sdl_window_create(preview_width, preview_height) || !lv_sdl_mouse_create())
        return EXIT_FAILURE;
    carplay_demo_launcher_init();
    bool previous[5] = { false };
    const SDL_Scancode keys[] = { SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3,
                                  SDL_SCANCODE_F4, SDL_SCANCODE_F5 };
    int speed = 26, charge = 78;
    bool valid = true;
    puts("F1 theme, F2 language, F3 connection, F4 vehicle, F5 update status, Esc exit");
    for (;;)
    {
        preview_ms = SDL_GetTicks();
        lv_timer_handler();
        const uint8_t *down = SDL_GetKeyboardState(NULL);
        if (down[SDL_SCANCODE_ESCAPE])
            return EXIT_SUCCESS;
        for (unsigned index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index)
        {
            bool pressed = down[keys[index]] != 0;
            if (pressed && !previous[index])
            {
                switch (keys[index])
                {
                case SDL_SCANCODE_F1:
                    carplay_ui_set_dark(!carplay_ui_dark());
                    break;
                case SDL_SCANCODE_F2:
                    carplay_ui_set_chinese(!carplay_ui_is_chinese());
                    break;
                case SDL_SCANCODE_F3:
                    model.stage = model.stage == DEMO_WAIT      ? DEMO_CONNECT
                                  : model.stage == DEMO_CONNECT ? DEMO_LIVE
                                  : model.stage == DEMO_LIVE    ? DEMO_FAILED
                                                                : DEMO_WAIT;
                    break;
                case SDL_SCANCODE_F4:
                    valid = speed != 66;
                    speed = speed == 66 ? 6 : speed + 20;
                    charge = charge <= 18 ? 78 : charge - 20;
                    break;
                case SDL_SCANCODE_F5:
                    ota_model.state = ota_model.state == CARPLAY_OTA_DOWNLOADING
                                          ? CARPLAY_OTA_READY_TO_INSTALL
                                          : CARPLAY_OTA_DOWNLOADING;
                    ota_model.progress = ota_model.state == CARPLAY_OTA_DOWNLOADING ? 55 : 100;
                    break;
                default:
                    break;
                }
            }
            previous[index] = pressed;
        }
        if (!lv_obj_has_flag(pages[PAGE_DASHBOARD], LV_OBJ_FLAG_HIDDEN))
            carplay_instruments_update(&dashboard_instruments, valid, (unsigned)speed, (unsigned)charge);
        SDL_Delay(10);
    }
}
#endif

static bool set_resolution(const char *width, const char *height)
{
    preview_width = atoi(width);
    preview_height = atoi(height);
    return (preview_width == 800 && preview_height == 480) ||
           (preview_width == 1024 && preview_height == 600) ||
           (preview_width == 1280 && preview_height == 720) ||
           (preview_width == 1920 && preview_height == 1080);
}

#include "benchmark.inc"

int main(int argc, char **argv)
{
    bool layout = argc == 5 && !strcmp(argv[1], "--layout") &&
                  set_resolution(argv[3], argv[4]);
    bool benchmark = argc == 2 && !strcmp(argv[1], "--benchmark");
    lv_init();
    seed_models();
#if LV_USE_SDL
    if ((argc == 2 || (argc == 4 && set_resolution(argv[2], argv[3]))) &&
        !strcmp(argv[1], "--interactive"))
        return interactive();
#endif
    if (argc != 2 && !layout)
    {
        fputs("Usage: launcher_preview DIR | --layout DIR WIDTH HEIGHT | --benchmark | --interactive [WIDTH HEIGHT]\n",
              stderr);
        return EXIT_FAILURE;
    }
    lv_display_t *d = lv_display_create(preview_width, preview_height);
    assert(d);
    size_t framebuffer_size = (size_t)preview_width * (size_t)preview_height * 4;
    pixels = malloc(framebuffer_size);
    assert(pixels);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d, pixels, NULL, framebuffer_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);
    if (benchmark)
    {
        run_benchmark();
        lv_deinit();
        free(pixels);
        return 0;
    }
    if (layout)
    {
        render_layout(argv[2]);
        lv_deinit();
        free(pixels);
        return 0;
    }
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
    assert_dashboard_header();
    lv_color_t disconnected_color = lv_obj_get_style_image_recolor(dash_ethernet, 0);
    assert(lv_color_eq(disconnected_color, lv_obj_get_style_image_recolor(dash_wifi, 0)));
    network_model.available = network_model.device = network_model.link = true;
    update_page(NULL);
    lv_color_t link_color = lv_obj_get_style_image_recolor(dash_ethernet, 0);
    assert(!lv_color_eq(disconnected_color, link_color));
    assert(!strcmp(lv_label_get_text(dash_speed), "26"));
    assert(!strcmp(lv_label_get_text(dash_soc), "78"));
    assert(!lv_obj_has_flag(dash_note, LV_OBJ_FLAG_HIDDEN));
    inside(dash_speed);
    inside(dash_soc);
    inside(dash_bar);
    assert(lv_bar_get_value(dash_bar) == 78);

    /* Connectivity exposes the capabilities reported by the connection owner. */
    show_page(PAGE_CONNECTIVITY);
    update_page(NULL);
    assert(!lv_obj_has_flag(pages[PAGE_CONNECTIVITY], LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(pages[PAGE_DASHBOARD], LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "02-connectivity_dark_en");
    assert(!strcmp(lv_label_get_text(connectivity_view.state), "Waiting for phone"));
    assert(find_caption(pages[PAGE_CONNECTIVITY], "Phone Hub"));
    assert(!lv_obj_has_state(connectivity_view.action, LV_STATE_DISABLED));
    inside(connectivity_view.state);
    inside(connectivity_view.action);
    unsigned retries = model.reconnects;
    lv_obj_send_event(connectivity_view.action, LV_EVENT_CLICKED, NULL);
    assert(model.reconnects == retries + 1);

    /* Settings: both sliders reach the real backends and persist on release. */
    tap_object(shell.tabs[PAGE_DASHBOARD]);
    assert(!lv_obj_has_flag(pages[PAGE_DASHBOARD], LV_OBJ_FLAG_HIDDEN));
    tap_object(shell.tabs[PAGE_SETTINGS]);
    assert(!lv_obj_has_flag(pages[PAGE_SETTINGS], LV_OBJ_FLAG_HIDDEN));
    assert_settings_page(SETTINGS_DISPLAY);
    open_setting(SETTINGS_ETHERNET);
    assert(!strcmp(lv_label_get_text(ethernet_values[0]), "Waiting for IPv4"));
    assert(!strcmp(lv_label_get_text(ethernet_values[1]), "--"));
    network_model.addressed = true;
    strcpy(network_model.address, "192.168.2.3");
    strcpy(network_model.mask, "255.255.255.0");
    strcpy(network_model.gateway, "192.168.2.1");
    strcpy(network_model.dns, "192.168.2.53");
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(ethernet_values[0]), "Connected"));
    assert(!strcmp(lv_label_get_text(ethernet_values[1]), network_model.address));
    assert(!strcmp(lv_label_get_text(ethernet_values[2]), network_model.mask));
    assert(!strcmp(lv_label_get_text(ethernet_values[3]), network_model.gateway));
    assert(!strcmp(lv_label_get_text(ethernet_values[4]), network_model.dns));
    lv_color_t addressed_color = lv_obj_get_style_image_recolor(dash_ethernet, 0);
    assert(!lv_color_eq(link_color, addressed_color));
    screenshot(argv[1], "03-settings-ethernet_dark_en");
    carplay_ui_set_chinese(true);
    assert(find_caption(pages[PAGE_SETTINGS], "有线网络"));
    assert(!strcmp(lv_label_get_text(ethernet_values[0]), "已连接"));
    screenshot(argv[1], "03-settings-ethernet_dark_zh");
    carplay_ui_set_chinese(false);
    network_model.link = network_model.addressed = false;
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(ethernet_values[0]), "Cable disconnected"));
    for (unsigned index = 1; index < 5; ++index)
        assert(!strcmp(lv_label_get_text(ethernet_values[index]), "--"));
    assert(lv_color_eq(disconnected_color, lv_obj_get_style_image_recolor(dash_ethernet, 0)));

    open_setting(SETTINGS_WIFI);
    assert(lv_obj_has_state(wifi_scan_button, LV_STATE_DISABLED));
    wifi_model.ap_active = true;
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(wifi_ap_status), "CarPlay access point active"));
    assert(lv_color_eq(link_color, lv_obj_get_style_image_recolor(dash_wifi, 0)));
    const lv_font_t *wifi_font = lv_obj_get_style_text_font(wifi_ap_status, 0);
    carplay_ui_set_chinese(true);
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(wifi_ap_status), "CarPlay 热点已开启"));
    assert(!strcmp(lv_textarea_get_placeholder_text(wifi_password), "Wi-Fi 密码"));
    assert(lv_obj_get_style_text_font(wifi_ap_status, 0) == wifi_font);
    lv_font_glyph_dsc_t hotspot_glyph;
    assert(lv_font_get_glyph_dsc(wifi_font, &hotspot_glyph, 0x70ed, 0) && !hotspot_glyph.is_placeholder);
    assert(hotspot_glyph.resolved_font == &carplay_cjk_18);
    screenshot(argv[1], "03-settings-wifi-ap_dark_zh");
    carplay_ui_set_chinese(false);
    lv_obj_send_event(wifi_mode_button, LV_EVENT_CLICKED, NULL);
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(wifi_ap_status), "CarPlay access point inactive"));
    assert(!lv_obj_has_state(wifi_scan_button, LV_STATE_DISABLED));
    lv_obj_send_event(wifi_scan_button, LV_EVENT_CLICKED, NULL);
    update_page(NULL);
    const char *network_row = lv_label_get_text(lv_obj_get_child(wifi_rows[0], 0));
    assert(strstr(network_row, "Workshop"));
    assert(strstr(network_row, "WPA2") && strstr(network_row, "5 GHz"));
    tap_object(wifi_rows[0]);
    assert(wifi_selected == 0);
    assert(lv_textarea_get_password_mode(wifi_password));
    lv_obj_send_event(theme_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(!carplay_ui_dark() && stored_preferences[CARPLAY_PREF_THEME] == 0);
    assert(lv_color_eq(lv_obj_get_style_bg_color(theme_buttons[0], 0),
                       lv_color_hex(carplay_ui_color(CARPLAY_UI_SOFT))));
    assert(lv_color_eq(lv_obj_get_style_bg_color(wifi_results, 0),
                       lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE))));
    lv_obj_update_layout(pages[PAGE_SETTINGS]);
    lv_area_t password_area, keyboard_area, detail_area;
    lv_obj_get_coords(wifi_password, &password_area);
    tap(password_area.x1 + 30, (password_area.y1 + password_area.y2) / 2);
    assert(!lv_obj_has_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(wifi_keyboard) == wifi_password);
    lv_obj_update_layout(pages[PAGE_SETTINGS]);
    lv_obj_get_coords(wifi_keyboard, &keyboard_area);
    lv_obj_get_coords(settings_pages[SETTINGS_WIFI], &detail_area);
    assert(keyboard_area.y1 > password_area.y2 && keyboard_area.y2 <= detail_area.y2);
    tap(keyboard_area.x1 + 93, keyboard_area.y1 + 20);
    assert(!strcmp(lv_textarea_get_text(wifi_password), "q"));
    screenshot(argv[1], "03-settings-wifi-keyboard_light_en");
    lv_obj_send_event(wifi_keyboard, LV_EVENT_READY, NULL);
    assert(lv_obj_has_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(wifi_keyboard) == NULL);
    lv_obj_send_event(theme_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(carplay_ui_dark() && stored_preferences[CARPLAY_PREF_THEME] == 1);
    tap(password_area.x1 + 30, (password_area.y1 + password_area.y2) / 2);
    assert(!lv_obj_has_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "03-settings-wifi-keyboard_dark_en");
    lv_obj_send_event(wifi_keyboard, LV_EVENT_CANCEL, NULL);
    assert(lv_obj_has_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN));
    lv_textarea_set_text(wifi_password, "preview-pass");
    lv_obj_send_event(wifi_join_button, LV_EVENT_CLICKED, NULL);
    update_page(NULL);
    assert(!strcmp(lv_textarea_get_text(wifi_password), ""));
    assert(lv_color_eq(link_color, lv_obj_get_style_image_recolor(dash_wifi, 0)));
    wifi_model.addressed = true;
    update_page(NULL);
    screenshot(argv[1], "03-settings-wifi_dark_en");
    assert(lv_color_eq(addressed_color, lv_obj_get_style_image_recolor(dash_wifi, 0)));
    click_back(pages[PAGE_SETTINGS]);
    assert_settings_page(SETTINGS_CONNECTIONS);
    click_back(pages[PAGE_SETTINGS]);
    assert_dashboard_header();
    screenshot(argv[1], "01-dashboard-wifi_dark_en");
    tap_object(shell.tabs[PAGE_SETTINGS]);
    open_setting(SETTINGS_CLOCK);
    click_caption(settings_pages[SETTINGS_CLOCK], "Time server");
    assert(!lv_obj_has_flag(clock_server_panel, LV_OBJ_FLAG_HIDDEN));
    lv_textarea_set_text(clock_server_entry, "https://bad.test");
    click_caption(clock_server_panel, "Save");
    assert(!lv_obj_has_flag(clock_server_panel, LV_OBJ_FLAG_HIDDEN));
    lv_textarea_set_text(clock_server_entry, "192.168.1.20");
    screenshot(argv[1], "03-settings-time-server_dark_en");
    click_caption(clock_server_panel, "Save");
    assert(lv_obj_has_flag(clock_server_panel, LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(clock_server_value), "192.168.1.20"));
    lv_obj_send_event(clock_network_button, LV_EVENT_CLICKED, NULL);
    assert(!strcmp(time_server_requested, "192.168.1.20"));
    assert(lv_obj_has_state(clock_network_button, LV_STATE_DISABLED));
    network_time_model = CARPLAY_TIME_NO_WAN;
    update_page(NULL);
    assert(!lv_obj_has_state(clock_network_button, LV_STATE_DISABLED));
    assert(!strcmp(lv_label_get_text(clock_network_status), "Connect Ethernet or Wi-Fi and try again."));
    lv_obj_send_event(clock_network_button, LV_EVENT_CLICKED, NULL);
    network_time_model = CARPLAY_TIME_SYNCED;
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(clock_network_status), "Network time synchronized"));
    assert(clock_automatic);
    screenshot(argv[1], "03-settings-network-time_dark_en");
    click_caption(settings_pages[SETTINGS_CLOCK], "Time server");
    click_caption(clock_server_panel, "Restore default");
    assert(!strcmp(time_server_model, "time.cloudflare.com"));
    open_setting(SETTINGS_DISPLAY);
    update_page(NULL);
    screenshot(argv[1], "03-settings_dark_en");
    lv_obj_t *display_cards[] = {
        lv_obj_get_parent(theme_buttons[0]),
        lv_obj_get_parent(display_buttons[0]),
    };
    lv_area_t display_page_area;
    lv_obj_get_coords(settings_pages[SETTINGS_DISPLAY], &display_page_area);
    for (unsigned index = 0; index < sizeof(display_cards) / sizeof(display_cards[0]); ++index)
    {
        lv_area_t card_area;
        lv_obj_get_coords(display_cards[index], &card_area);
        assert(card_area.y1 >= display_page_area.y1 && card_area.y1 < display_page_area.y2);
    }
    open_setting(SETTINGS_DEVELOPER);
    lv_obj_send_event(fps_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(stored_preferences[CARPLAY_PREF_HIGH_FPS] == 1);
    assert(!strcmp(lv_label_get_text(settings_hint), "Saved. Restart the receiver to apply."));
    screenshot(argv[1], "03-settings-frame-rate_dark_en");
    assert(!strcmp(lv_label_get_text(volume_value), "60%"));
    assert(!strcmp(lv_label_get_text(brightness_value), "72%"));

    open_setting(SETTINGS_BLUETOOTH);
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

    open_setting(SETTINGS_SOUND);
    click_caption(settings_pages[SETTINGS_SOUND], "Mute microphone");
    assert(microphone_muted && stored_preferences[CARPLAY_PREF_MIC_MUTED] == 1);
    click_caption(settings_pages[SETTINGS_SOUND], "Unmute microphone");
    assert(!microphone_muted && stored_preferences[CARPLAY_PREF_MIC_MUTED] == 0);
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

    open_setting(SETTINGS_DISPLAY);
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
    open_setting(SETTINGS_LANGUAGE);
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
    open_setting(SETTINGS_ABOUT);
    assert(find_caption(settings_pages[SETTINGS_ABOUT], "构建哈希"));
    assert(find_caption(settings_pages[SETTINGS_ABOUT], "构建时间 (UTC)"));
    screenshot(argv[1], "03-settings-about_dark_zh");
    open_setting(SETTINGS_DISPLAY);
    lv_obj_send_event(phone_appearance_buttons[RTCP_APPEARANCE_LIGHT], LV_EVENT_CLICKED, NULL);
    assert(model_phone_appearance == RTCP_APPEARANCE_LIGHT && carplay_ui_dark());
    tap_object(shell.tabs[PAGE_CONNECTIVITY]);
    model.stage = DEMO_LIVE;
    model_phone_media = (carplay_phone_media_t){
        .track = { .title = "Test Track", .artist = "Test Artist", .has_playing = true, .playing = true, .has_duration = true, .has_elapsed = true, .duration_ms = 180000, .elapsed_ms = 90000, .artwork_revision = 1 },
        .route_destination = "Central Station",
        .route_road = "Main Street",
        .route_remaining = "2 km",
        .route_turn_distance = "400 m",
        .maneuvers = { { .index = 7, .present = true, .instruction = "Turn right", .distance = "400 m" },
                       { .index = 3, .present = true, .instruction = "Keep left" } },
        .lane_count = 2,
        .lanes = { { .angle = -90 }, { .angle = 90, .highlighted = true } },
        .has_battery = true,
        .battery = 75,
        .has_charging = true,
        .charging = 1,
        .has_signal = true,
        .signal = 3,
        .carrier = "Preview Wireless",
        .has_call = true,
        .call = 4,
    };
    clock_received = true;
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(phone_title), "Test Track"));
    assert(!strcmp(lv_label_get_text(phone_artist), "Test Artist"));
    assert(!strcmp(lv_label_get_text(phone_playback), "播放中"));
    assert(!strcmp(lv_label_get_text(phone_destination), "Central Station"));
    assert(!strcmp(lv_label_get_text(phone_road), "Main Street"));
    assert(!strcmp(lv_label_get_text(phone_remaining), "2 km"));
    assert(find_caption(pages[PAGE_CONNECTIVITY], "路线引导"));
    assert(lv_bar_get_value(phone_progress) == 500);
    assert(!lv_obj_has_flag(phone_cover, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(phone_lane_strip, LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(phone_turn_distance), "400 m"));
    assert(strstr(lv_label_get_text(phone_maneuvers[0]), "Turn right"));
    assert(strstr(lv_label_get_text(phone_maneuvers[1]), "Keep left"));
    assert(strstr(lv_label_get_text(phone_power), "75%"));
    assert(strstr(lv_label_get_text(phone_cellular), "Preview Wireless"));
    assert(!strcmp(lv_label_get_text(phone_call), TR("Call active")));
    assert(!strcmp(lv_label_get_text(phone_clock), "2026-11-21 16:42:19  UTC+00:00"));
    assert(!strcmp(lv_label_get_text(phone_clock_mode), TR("iPhone time received")));
    screenshot(argv[1], "02-phone-hub-live_dark_zh");
    tap_object(shell.tabs[PAGE_DASHBOARD]);
    update_page(NULL);
    assert(strstr(lv_label_get_text(home_media), "Test Track"));
    assert(!lv_obj_has_flag(home_cover, LV_OBJ_FLAG_HIDDEN));
    assert(lv_image_get_src(home_cover) == lv_image_get_src(phone_cover));
    assert(strstr(lv_label_get_text(home_route), "Turn right"));
    screenshot(argv[1], "01-home-media-route_dark_zh");
    tap_object(shell.tabs[PAGE_CONNECTIVITY]);
    model_phone_media.track.artwork_revision = 3;
    cover_read_busy = true;
    update_page(NULL);
    assert(!lv_obj_has_flag(phone_cover, LV_OBJ_FLAG_HIDDEN));
    cover_read_busy = false;
    update_page(NULL);
    assert(!lv_obj_has_flag(phone_cover, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(phone_cover_placeholder, LV_OBJ_FLAG_HIDDEN));
    const lv_image_dsc_t *cover_image = lv_image_get_src(phone_cover);
    assert(cover_image->header.cf == LV_COLOR_FORMAT_RGB565);
    assert(cover_image->header.w == 96U && cover_image->header.h == 52U);
    assert(lv_image_get_scale(phone_cover) == LV_SCALE_NONE);
    const uint16_t *cover_pixels = (const uint16_t *)cover_image->data;
    unsigned colors = 0;
    for (uint32_t row = 0; row < cover_image->header.h; ++row)
        for (uint32_t col = 0; col < cover_image->header.w; ++col)
            colors += cover_pixels[row * cover_image->header.stride / 2U + col] != cover_pixels[0];
    assert(colors > 100U);
    lv_refr_now(NULL);
    lv_area_t cover_area;
    lv_obj_get_coords(phone_cover, &cover_area);
    static const uint8_t fixture_rgb[][3] = { { 77, 72, 136 }, { 120, 119, 101 }, { 162, 162, 100 } };
    for (unsigned sample = 1; sample <= 3; ++sample)
    {
        unsigned col = cover_image->header.w * sample / 4U;
        unsigned row = cover_image->header.h * sample / 4U;
        unsigned screen_x = (unsigned)cover_area.x1 + col;
        unsigned screen_y = (unsigned)cover_area.y1 + row;
        assert(screen_x < 1024U && screen_y < 600U);
        const uint8_t *pixel = pixels + (screen_y * 1024U + screen_x) * 4U;
        assert(abs((int)pixel[2] - fixture_rgb[sample - 1][0]) <= 12);
        assert(abs((int)pixel[1] - fixture_rgb[sample - 1][1]) <= 12);
        assert(abs((int)pixel[0] - fixture_rgb[sample - 1][2]) <= 12);
    }
    click_caption(pages[PAGE_CONNECTIVITY], TR("Phone details"));
    assert(current_page == PAGE_PHONE_DETAILS);
    screenshot(argv[1], "02-phone-details_dark_zh");
    click_back(pages[PAGE_PHONE_DETAILS]);
    assert(current_page == PAGE_CONNECTIVITY);
    model_phone_media = (carplay_phone_media_t){
        .track = { .title = "龘\U00020087 Live", .artist = "麤樂隊", .artwork_revision = 2 },
        .route_destination = "龘龍站",
    };
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(phone_title), "龘\U00020087 Live"));
    assert(lv_obj_get_style_text_font(phone_title, 0) == &carplay_cjk_full_18);
    assert(lv_obj_get_style_text_font(phone_destination, 0) == &carplay_cjk_full_18);
    assert(lv_obj_has_flag(phone_cover, LV_OBJ_FLAG_HIDDEN));
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&carplay_cjk_full_18, &glyph, 0x9f98, 0) && !glyph.is_placeholder);
    assert(lv_font_get_glyph_dsc(&carplay_cjk_full_18, &glyph, 0x20087, 0) && !glyph.is_placeholder);
    assert(lv_font_get_glyph_dsc(&carplay_ui_18, &glyph, 0x20087, 0) && !glyph.is_placeholder);
    assert(glyph.resolved_font == &carplay_cjk_full_18);
    assert(lv_font_get_glyph_dsc(&carplay_ui_18, &glyph, 'A', 0) && glyph.resolved_font == &carplay_ui_18);
    const lv_font_t *clock_fonts[] = { &carplay_cjk_16, &carplay_cjk_18, &carplay_cjk_24, &carplay_cjk_30 };
    const uint32_t clock_glyphs[] = { 0x4e0e, 0x504f, 0x540c, 0x5730, 0x671f, 0x6b65, 0x6cd5, 0x79fb, 0x949f };
    for (size_t font = 0; font < sizeof(clock_fonts) / sizeof(clock_fonts[0]); ++font)
        for (size_t index = 0; index < sizeof(clock_glyphs) / sizeof(clock_glyphs[0]); ++index)
            assert(lv_font_get_glyph_dsc(clock_fonts[font], &glyph, clock_glyphs[index], 0) &&
                   !glyph.is_placeholder);
    carplay_ui_set_chinese(false);
    update_page(NULL);
    assert(find_caption(pages[PAGE_CONNECTIVITY], "Route guidance"));
    assert(!strcmp(lv_label_get_text(phone_artist), "麤樂隊"));
    assert(lv_obj_get_style_text_font(phone_artist, 0) == &carplay_cjk_full_18);
    carplay_ui_set_chinese(true);
    update_page(NULL);
    model.stage = DEMO_WAIT;
    memset(&model_phone_media, 0, sizeof(model_phone_media));
    clock_received = false;
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(phone_title), "-") && !*lv_label_get_text(phone_playback));
    assert(!strcmp(lv_label_get_text(phone_destination), "-") && !*lv_label_get_text(phone_road) &&
           !*lv_label_get_text(phone_remaining));
    assert(lv_obj_has_flag(phone_lane_strip, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(home_cover, LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(phone_power), TR("Battery unavailable")));
    assert(!strcmp(lv_label_get_text(phone_clock_mode), TR("Waiting for iPhone time")));
    open_setting(SETTINGS_GNSS);
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
    open_setting(SETTINGS_LANGUAGE);
    lv_obj_send_event(language_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(!carplay_ui_is_chinese());

    /* Display mode without the compositor is refused, not faked. */
    open_setting(SETTINGS_DISPLAY);
    lv_obj_send_event(display_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(strstr(lv_label_get_text(settings_hint), "Not supported"));
    screenshot(argv[1], "03-settings_dark_en");

    open_setting(SETTINGS_ABOUT);
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
    open_setting(SETTINGS_SYSTEM);

    /* OTA page: real state strings, no progress without a real percentage. */
    click_caption(settings_pages[SETTINGS_SYSTEM], "System update");
    assert(!lv_obj_has_flag(pages[PAGE_OTA], LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "04-ota_dark_en");
    assert(!strcmp(lv_label_get_text(ota_state), "Ready to download"));
    assert(!lv_obj_has_state(ota_update_button, LV_STATE_DISABLED));
    assert(lv_obj_has_flag(ota_progress, LV_OBJ_FLAG_HIDDEN));

    click_caption(pages[PAGE_OTA], "Enter firmware URL");
    assert(!lv_obj_has_flag(ota_source_panel, LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(ota_source_panel);
    for (uint32_t index = 0; index < lv_obj_get_child_count(ota_source_panel); ++index)
    {
        lv_obj_t *child = lv_obj_get_child(ota_source_panel, (int32_t)index);
        if (lv_obj_check_type(child, &lv_keyboard_class))
            inside(child);
    }
    lv_textarea_set_text(ota_source_entry, "http://192.168.1.20:8080/d213/ota.cpio");
    screenshot(argv[1], "04-ota-source-http_dark_en");
    click_caption(ota_source_panel, "Save");
    assert(lv_obj_has_flag(ota_source_panel, LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(ota_model.source, "http://192.168.1.20:8080/d213/ota.cpio"));

    network_model.available = network_model.route_ready = true;
    update_page(NULL);
    snprintf(ota_model.current, sizeof(ota_model.current), "1.0.0");
    snprintf(ota_model.current_commit, sizeof(ota_model.current_commit),
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    snprintf(ota_model.slot, sizeof(ota_model.slot), "A");
    lv_obj_send_event(ota_update_button, LV_EVENT_CLICKED, NULL);
    assert(ota_model.state == CARPLAY_OTA_DOWNLOADING);
    update_page(NULL);
    lv_obj_send_event(ota_update_button, LV_EVENT_CLICKED, NULL);
    assert(ota_model.state == CARPLAY_OTA_IDLE);
    ota_model.state = CARPLAY_OTA_READY_TO_INSTALL;
    snprintf(ota_model.remote, sizeof(ota_model.remote), "1.0.1");
    update_page(NULL);
    lv_obj_send_event(ota_update_button, LV_EVENT_CLICKED, NULL);
    assert(!ota_installs && !lv_obj_has_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "04-ota-confirm_dark_en");
    ota_confirm_cancel(NULL);
    assert(!ota_installs);
    lv_obj_send_event(ota_update_button, LV_EVENT_CLICKED, NULL);
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
    assert_settings_page(SETTINGS_SYSTEM);
    click_back(pages[PAGE_SETTINGS]);
    update_page(NULL);
    screenshot(argv[1], "01-dashboard_dark_zh");
    assert_dashboard_header();
    assert(find_caption(pages[PAGE_DASHBOARD], "仪表"));
    assert(find_caption(pages[PAGE_DASHBOARD], "当前速度"));
    assert(find_caption(pages[PAGE_DASHBOARD], "剩余电量"));
    assert(find_caption(shell.navigation, TR("Phone Hub")));
    assert(find_caption(shell.navigation, "系统设置"));
    tap_object(shell.tabs[PAGE_CONNECTIVITY]);
    update_page(NULL);
    screenshot(argv[1], "02-connectivity_dark_zh");
    assert(find_caption(pages[PAGE_CONNECTIVITY], "等待连接"));
    carplay_ui_set_chinese(false);
    lv_obj_send_event(theme_buttons[0], LV_EVENT_CLICKED, NULL);
    screenshot(argv[1], "02-connectivity_light_en");

    /* A live session locks the local UI behind the phone video. */
    model.stage = DEMO_LIVE;
    update_page(NULL);
    assert(!strcmp(lv_label_get_text(dash_status), "Connected"));
    assert(!strcmp(lv_label_get_text(connectivity_view.state), "Connected"));
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

    puts("Cockpit LVGL pages: dashboard, connectivity, settings, OTA, pairing and "
         "unsupported-backend states rendered at 1024x600; modeled values only");
    lv_deinit();
    free(pixels);
    return 0;
}

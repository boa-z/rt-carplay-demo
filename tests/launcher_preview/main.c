/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual LVGL 9 rendering and UI events. Connection/media values are modeled
 * only for layout and request-routing tests, never hardware acceptance. */
#include <assert.h>
#include <stdlib.h>
#include "../../ui/lvgl_launcher.c"

static demo_status_t model;
static uint8_t pixels[1024 * 600 * 4];
static bool files_opened, audio_opened, microphone_muted;
static unsigned file_version, replies, audio_selection;
static carplay_internet_status_t network_model;
static unsigned network_requests;
static bool preview_video_active;
static carplay_ota_status_t ota_model;
static unsigned ota_checks, ota_installs;
void carplay_ota_status(carplay_ota_status_t *out) { *out = ota_model; }
int carplay_ota_check(void) { ++ota_checks; ota_model.state = CARPLAY_OTA_CHECKING; ota_model.busy = true; return 0; }
int carplay_ota_install(const char *commit) {
    if (strcmp(commit, ota_model.remote_commit)) return -1;
    ++ota_installs; ota_model.state = CARPLAY_OTA_DOWNLOADING; ota_model.busy = true; return 0;
}
void carplay_internet_status(carplay_internet_status_t *out) { *out = network_model; }
bool carplay_internet_check(void) { ++network_requests; network_model.checking = true; return true; }
static unsigned preview_ms = 183000, manager_calls;
uint32_t rt_tick_get_millisecond(void) { return preview_ms; }
rt_mutex_t rt_mutex_create(const char *n, int f) { (void)n; (void)f; return (void *)1; }
int rt_mutex_take(rt_mutex_t m, int w) { (void)m; (void)w; return 0; }
int rt_mutex_release(rt_mutex_t m) { (void)m; return 0; }
int rt_kprintf(const char *f, ...) { (void)f; return 0; }
bool carplay_connection_status(demo_status_t *s) { *s = model; return true; }
int carplay_connection_manager_start(void)
{ return ++manager_calls <= 2 ? RTCP_ENOMEM : RTCP_OK; }
int carplay_touch_panel_start(uint16_t w, uint16_t h, carplay_touch_panel_cb_t c, void *p)
{ assert(w == 1024 && h == 600 && c); (void)p; return RTCP_OK; }
bool carplay_launcher_video_is_active(void) { return preview_video_active; }
void carplay_launcher_video_event(bool a, void *p) { (void)a; (void)p; }
bool carplay_video_sink_is_running(void) { return false; }
int carplay_video_sink_set_display_observer(carplay_video_display_cb_t c, void *p)
{ assert(c); (void)p; return RTCP_OK; }
bool carplay_app_is_running(void) { return false; }
int carplay_connection_touch(uint8_t p, uint16_t i, uint16_t x, uint16_t y)
{ (void)p; (void)i; (void)x; (void)y; return RTCP_ESTATE; }
bool carplay_connection_set_wireless_enabled(bool enabled)
{ model.wireless_enabled = enabled; return true; }
void carplay_audio_input_mute(bool muted) { microphone_muted = muted; }
bool carplay_audio_input_is_muted(void) { return microphone_muted; }
void carplay_connection_retry(void) { ++model.reconnects; }
void carplay_connection_pairing_reply(uint32_t r, bool a)
{ assert(r == 42 && a); ++replies; }
bool carplay_connection_files_open(void) { files_opened = true; ++file_version; return true; }
void carplay_connection_files_close(void) { files_opened = false; }
void carplay_connection_files_refresh(void) { ++file_version; }
void carplay_connection_files_forget(const char *n)
{ assert(!strcmp(n, "rt-carplay-pairings.bin")); ++file_version; }
bool carplay_connection_files_snapshot(unsigned *rev,
    carplay_pair_file_t f[CARPLAY_PAIR_FILES_MAX], unsigned *count, bool *more, int *result)
{
    if (!files_opened || *rev == file_version) return false;
    *rev = file_version; *count = 2; *more = false; *result = RTCP_OK;
    snprintf(f[0].name, sizeof(f[0].name), "rt-carplay-pairings.bin");
    snprintf(f[1].name, sizeof(f[1].name), "rt-carplay-usb-00008101001234567890.bin");
    f[0].bytes = 200; f[1].bytes = 2000;
    return true;
}
bool carplay_connection_audio_open(void) { audio_opened = true; return true; }
void carplay_connection_audio_select(unsigned id) { audio_selection = id; }
void carplay_connection_audio_snapshot(bool *open, bool *ready, bool *exiting,
    audio_test_config_t *config, int *result, audio_test_status_t *status)
{
    *open = audio_opened; *ready = true; *exiting = false;
    *config = (audio_test_config_t){AUDIO_TEST_TONE, 48000, 0}; *result = RTCP_OK;
    *status = (audio_test_status_t){.volume = 20};
}
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *data)
{ (void)area; (void)data; lv_display_flush_ready(d); }
static void screenshot(const char *directory, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    update_page(NULL); lv_refr_now(NULL);
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n1024 600\n255\n");
    for (unsigned i = 0; i < 1024 * 600; ++i) {
        uint8_t rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
        assert(fwrite(rgb, 1, sizeof(rgb), f) == sizeof(rgb));
    }
    assert(fclose(f) == 0);
}
static void inside(lv_obj_t *object)
{
    lv_area_t a; lv_obj_get_coords(object, &a);
    assert(a.x1 >= 0 && a.y1 >= 0 && a.x2 < 1024 && a.y2 < 600);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    lv_display_t *d = lv_display_create(1024, 600); assert(d);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);
    model.stage = DEMO_WAIT; model.automatic = model.wireless = true;
    model.usb_ready = model.wireless_enabled = model.bt_ready = true;
    snprintf(model.ssid, sizeof(model.ssid), "ArtInChip CarPlay");
    snprintf(model.pin, sizeof(model.pin), "3939");
    carplay_demo_launcher_init();
    assert(manager_calls == 1);
    update_page(NULL); assert(manager_calls == 2);
    preview_ms += 1000;
    update_page(NULL); assert(manager_calls == 3);
    update_page(NULL); assert(manager_calls == 3); /* One retry per second. */
    screenshot(argv[1], "home");
    inside(heading); inside(pin_label); inside(path_labels[1]); inside(retry_button);
    assert(!lv_obj_has_state(manage_button, LV_STATE_DISABLED));
    lv_obj_send_event(network_button, LV_EVENT_CLICKED, NULL);
    assert(!lv_obj_has_flag(network_panel, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "network-unavailable");assert(lv_obj_has_state(network_check, LV_STATE_DISABLED));
    network_model.available = network_model.device = true;
    screenshot(argv[1], "network-unplugged");assert(strstr(lv_label_get_text(network_summary), "Cable disconnected"));
    network_model.link = network_model.addressed = network_model.route_ready = true;
    strcpy(network_model.probe_url,"http://connectivitycheck.gstatic.com/generate_204");
    strcpy(network_model.address,"192.168.10.123");strcpy(network_model.mask,"255.255.255.0");
    strcpy(network_model.gateway,"192.168.10.1");strcpy(network_model.dns,"8.8.8.8");
    network_model.route_ready=false;
    update_page(NULL);assert(lv_obj_has_state(network_check,LV_STATE_DISABLED));
    assert(strstr(lv_label_get_text(network_summary),"No gateway"));
    network_model.route_ready=true;
    update_page(NULL);assert(!lv_obj_has_state(network_check, LV_STATE_DISABLED));
    lv_obj_send_event(network_check, LV_EVENT_CLICKED, NULL);
    screenshot(argv[1], "network-checking");assert(network_requests==1 && lv_obj_has_state(network_check, LV_STATE_DISABLED));
    network_model.checking=false;network_model.checked=network_model.reachable=true;
    screenshot(argv[1], "network-online");inside(network_detail);inside(network_check);
    assert(strstr(lv_label_get_text(network_detail),"sharing: Inactive"));
    assert(strstr(lv_label_get_text(network_summary),"Internet verified"));
    network_model.reachable=false;
    screenshot(argv[1], "network-failed");assert(strstr(lv_label_get_text(network_summary),"Check failed"));
    network_model.age_ms=46000;
    update_page(NULL);assert(strstr(lv_label_get_text(network_summary),"Check expired"));
    lv_obj_send_event(lv_obj_get_child(network_panel,4),LV_EVENT_CLICKED,NULL);
    assert(lv_obj_has_flag(network_panel,LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(settings_button, LV_EVENT_CLICKED, NULL);
    assert(!lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(wireless_setting, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(microphone_setting, LV_EVENT_CLICKED, NULL);
    assert(!model.wireless_enabled && microphone_muted);
    screenshot(argv[1], "settings");
    inside(wireless_setting); inside(microphone_setting);
    inside(ota_check_button); inside(ota_install_button); inside(ota_state_label);
    snprintf(ota_model.current, sizeof(ota_model.current), "1.0.0");
    snprintf(ota_model.current_commit, sizeof(ota_model.current_commit), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    snprintf(ota_model.slot, sizeof(ota_model.slot), "A");
    lv_obj_send_event(ota_check_button, LV_EVENT_CLICKED, NULL); update_page(NULL);
    assert(ota_checks == 1 && lv_obj_has_state(ota_check_button, LV_STATE_DISABLED));
    ota_model.state = CARPLAY_OTA_AVAILABLE; ota_model.busy = false;
    strcpy(ota_model.remote, "1.0.0");
    strcpy(ota_model.remote_commit, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    strcpy(ota_model.message, "Preview build bbbbbbbb is available. Disconnect CarPlay before installing.");
    screenshot(argv[1], "ota-available"); inside(ota_version_label);
    lv_obj_send_event(ota_install_button, LV_EVENT_CLICKED, NULL);
    assert(!ota_installs && !lv_obj_has_flag(ota_confirm_panel, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "ota-confirm"); inside(ota_confirm_text);
    inside(lv_obj_get_child(ota_confirm_panel, 1)); inside(lv_obj_get_child(ota_confirm_panel, 2));
    ota_confirm_cancel(NULL); assert(!ota_installs);
    ota_install_now(NULL); strcpy(ota_model.remote_commit, "cccccccccccccccccccccccccccccccccccccccc");
    ota_confirm_install(NULL); assert(!ota_installs && ota_notice);
    ota_install_now(NULL); ota_confirm_install(NULL); assert(ota_installs == 1);
    ota_model.state = CARPLAY_OTA_APPLYING; ota_model.progress = 60;
    strcpy(ota_model.message, "Package verified. Writing inactive slot; keep power connected.");
    screenshot(argv[1], "ota-applying");
    assert(lv_obj_has_state(ota_check_button, LV_STATE_DISABLED) && lv_obj_has_state(ota_install_button, LV_STATE_DISABLED));
    ota_model.state = CARPLAY_OTA_FAILED; ota_model.busy = false;
    strcpy(ota_model.message, "Download, storage or SHA256 check failed. Check for updates again.");
    screenshot(argv[1], "ota-failed"); inside(ota_state_label);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(diagnostics_panel, LV_OBJ_FLAG_HIDDEN);
    screenshot(argv[1], "diagnostics"); inside(debug_label);
    lv_obj_add_flag(diagnostics_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_send_event(manage_button, LV_EVENT_CLICKED, NULL);
    screenshot(argv[1], "devices");
    lv_obj_send_event(lv_obj_get_child(file_list, 0), LV_EVENT_CLICKED, NULL);
    assert(!lv_obj_has_flag(confirm_panel, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[1], "forget"); inside(confirm_text);
    lv_obj_add_flag(file_panel, LV_OBJ_FLAG_HIDDEN); files_opened = false;
    lv_obj_send_event(audio_button, LV_EVENT_CLICKED, NULL);
    assert(audio_opened);
    screenshot(argv[1], "audio"); inside(audio_note); inside(audio_play);
    lv_obj_send_event(audio_play, LV_EVENT_CLICKED, NULL);
    assert(audio_selection == 40);
    lv_obj_add_flag(audio_panel, LV_OBJ_FLAG_HIDDEN); audio_opened = false;
    model.stage = DEMO_PAIR; model.wireless_enabled = true;
    model.pairing.kind = RTCP_BT_PAIR_COMPARE; model.pairing.request = 42;
    model.pairing.passkey = 121314;
    screenshot(argv[1], "pairing");
    assert(!lv_obj_has_flag(bt_panel, LV_OBJ_FLAG_HIDDEN)); inside(bt_text);
    lv_obj_send_event(bt_accept, LV_EVENT_CLICKED, NULL); assert(replies == 1);
    assert(lv_obj_has_flag(bt_panel, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_state(manage_button, LV_STATE_DISABLED));
    show_modal(network_panel);model.stage=DEMO_LIVE;preview_video_active=true;
    update_page(NULL);assert(lv_obj_has_flag(network_panel,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(modal_scrim,LV_OBJ_FLAG_HIDDEN));
    puts("Actual LVGL 9: 1024x600 previews including OTA consent and failure states, visible bounds and network/settings/device/audio/pairing request routing passed; modeled connection values only");
    return 0;
}

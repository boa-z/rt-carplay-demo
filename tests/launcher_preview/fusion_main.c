/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual LVGL software rendering of the V4 fusion strip. A modeled video latch
 * is layout input, never an assertion about DE, iPhone negotiation or Ultra. */
#include <assert.h>
#include <stdlib.h>
#include "../../ui/fusion_dashboard.c"

static uint8_t pixels[1024 * 600 * 4];
/* t = (now / 1000) % 80 = 13 -> the simulated triangle reports 26 km/h. */
static uint32_t now = 13000;
static carplay_display_mode_t selected = CARPLAY_FUSION;
static unsigned irq_depth;

rt_base_t rt_hw_interrupt_disable(void)
{
    return irq_depth++;
}
void rt_hw_interrupt_enable(rt_base_t level)
{
    assert(level + 1 == irq_depth);
    --irq_depth;
}
uint32_t rt_tick_get_millisecond(void)
{
    return now;
}
int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}
int carplay_launcher_fusion_apply(const carplay_fusion_snapshot_t *s)
{
    assert(s->initialized && !irq_depth);
    return RTCP_OK;
}
/* The strip's Dashboard control asks the launcher page owner to select its
 * dashboard before the panel is handed back. */
static bool launcher_selected;
static bool projection_paused;
int carplay_app_set_projection_enabled(bool enabled)
{
    projection_paused = !enabled;
    return RTCP_OK;
}

void carplay_launcher_show_dashboard(void)
{
    assert(projection_paused);
    launcher_selected = true;
}
bool carplay_connection_set_display_mode(carplay_display_mode_t mode)
{
    selected = mode;
    return carplay_fusion_set_mode(mode) == RTCP_OK;
} /* Modeled owner request. */
bool carplay_connection_status(demo_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->display_requested = selected;
    return true;
}
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *data)
{
    (void)area;
    (void)data;
    lv_display_flush_ready(d);
}

static void inside(lv_obj_t *object)
{
    lv_area_t a;
    lv_obj_get_coords(object, &a);
    assert(a.x1 >= 768 && a.y1 >= 0 && a.x2 < 1024 && a.y2 < 600);
}

/* Every pixel left of x = 768 must still hold the video sink's frame: LVGL is
 * never allowed to paint there while the strip owns presentation. */
static uint8_t video_side[1024 * 600 * 4];
static bool projection_armed;

static void arm_projection(void)
{
    for (unsigned i = 0; i < 1024 * 600; ++i)
    {
        pixels[i * 4 + 0] = 0x1A;
        pixels[i * 4 + 1] = 0x16;
        pixels[i * 4 + 2] = 0x12;
        pixels[i * 4 + 3] = 0;
    }
    memcpy(video_side, pixels, sizeof(video_side));
    projection_armed = true;
}

static void projection_side_clean(void)
{
    assert(projection_armed);
    for (unsigned y = 0; y < 600; ++y)
        for (unsigned x = 0; x < 768; ++x)
        {
            unsigned i = (y * 1024 + x) * 4;
            if (pixels[i] != video_side[i] || pixels[i + 1] != video_side[i + 1] ||
                pixels[i + 2] != video_side[i + 2])
            {
                fprintf(stderr, "LVGL painted the projection at (%u,%u): %02x%02x%02x\n",
                        x, y, pixels[i], pixels[i + 1], pixels[i + 2]);
                assert(!"LVGL painted the projection side");
            }
        }
}

/* No LVGL object may be laid out on the projection side. The device keeps the
 * compositor crop in video_layer_policy; this proves the widget tree itself
 * never places anything there. */
static void only_strip_widgets(lv_obj_t *parent)
{
    lv_area_t a;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
    {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i);
        lv_obj_get_coords(child, &a);
        assert(a.x1 >= 768 && a.x2 < 1024);
        only_strip_widgets(child);
    }
}

static void screenshot(const char *directory, const char *name, demo_status_t *status,
                       bool expect_visible)
{
    bool owns = carplay_fusion_ui_update(status);
    assert(owns == expect_visible);
    lv_refr_now(NULL);
    if (expect_visible)
    {
        projection_side_clean();
        only_strip_widgets(lv_screen_active());
    }
    else
        projection_armed = false;
    if (expect_visible && !lv_obj_has_flag(strip, LV_OBJ_FLAG_HIDDEN))
    {
        inside(speed_caption);
        inside(speed_label);
        inside(unit_label);
        inside(soc_caption);
        inside(soc_label);
        inside(soc_bar);
        inside(home_button);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n1024 600\n255\n");
    for (unsigned i = 0; i < 1024 * 600; ++i)
    {
        uint8_t rgb[] = { pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4] };
        assert(fwrite(rgb, 1, 3, f) == 3);
    }
    assert(fclose(f) == 0);
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

    lv_obj_t *launcher = lv_obj_create(NULL);
    demo_status_t status = { .stage = DEMO_WAIT };
    carplay_fusion_ui_init(launcher, 1024, 600);
    carplay_fusion_ui_resume();

    /* LOCAL keeps the launcher pages, so the strip stays hidden. */
    screenshot(argv[1], "05-fusion-local", &status, false);

    /* From here the video sink owns x = 0..767; seed those pixels and require
     * LVGL to leave every one of them untouched. */
    arm_projection();

    /* FUSION only after a real video latch. */
    rtcp_display_rect_t rect = { .x = 0, .y = 0, .width = 768, .height = 600 };
    carplay_fusion_snapshot_t s;
    carplay_fusion_snapshot(now, &s);
    carplay_fusion_video_latched(&rect, 768, 600, s.generation, now, true);
    status.stage = DEMO_LIVE;
    screenshot(argv[1], "05-fusion_dark_en", &status, true);
    assert(!strcmp(lv_label_get_text(speed_label), "26"));
    assert(!strcmp(lv_label_get_text(soc_label), "82%"));

    carplay_vehicle_sample_t provider = { .available = true, .speed_kph = 42, .battery_percent = 77, .gear = "R" };
    assert(carplay_fusion_set_vehicle(&provider) == RTCP_OK);
    screenshot(argv[1], "05-fusion-provider", &status, true);
    assert(!strcmp(lv_label_get_text(speed_label), "42"));
    assert(!strcmp(lv_label_get_text(soc_label), "77%"));

    provider.available = false;
    assert(carplay_fusion_set_vehicle(&provider) == RTCP_OK);
    screenshot(argv[1], "05-fusion-unavailable", &status, true);
    assert(!strcmp(lv_label_get_text(speed_label), "--"));

    carplay_ui_set_dark(false);
    screenshot(argv[1], "05-fusion_light_en", &status, true);
    carplay_ui_set_chinese(true);
    screenshot(argv[1], "05-fusion_light_zh", &status, true);
    carplay_ui_set_dark(true);
    carplay_ui_set_chinese(false);
    carplay_fusion_use_simulated();

    /* "Dashboard" selects the dashboard page and hands the panel to it; the
     * projection request itself is unchanged, so Resume CarPlay can restore it. */
    lv_obj_send_event(home_button, LV_EVENT_CLICKED, NULL);
    assert(launcher_selected);
    assert(selected == CARPLAY_FUSION);
    assert(carplay_fusion_ui_update(&status) == false); /* launcher owns it now */

    carplay_fusion_ui_resume();
    carplay_fusion_resume_projection_on_frame();
    assert(carplay_fusion_ui_update(&status) == false);
    carplay_fusion_snapshot(now, &s);
    carplay_fusion_video_latched(&rect, 768, 600, s.generation, now, true);
    assert(carplay_fusion_ui_update(&status) == true);

    /* FULLSCREEN hides the strip and reports ownership to the launcher. */
    assert(carplay_connection_set_display_mode(CARPLAY_FULLSCREEN));
    screenshot(argv[1], "06-fullscreen-dark", &status, true);
    assert(lv_obj_has_flag(strip, LV_OBJ_FLAG_HIDDEN));

    carplay_fusion_video_stopped();
    status.stage = DEMO_FAILED;
    screenshot(argv[1], "05-fusion-disconnected", &status, false);

    puts("V4 fusion strip: FUSION, provider, unavailable, light/zh, FULLSCREEN "
         "hidden and disconnect rendered; x<768 verified untouched (Host only)");
    return 0;
}

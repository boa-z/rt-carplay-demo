/* SPDX-License-Identifier: GPL-3.0-or-later */
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
/* V4 FUSION instrument strip: the only LVGL pixels in x = 768..1023.
 * Everything here is positioned inside that strip; nothing is drawn on the
 * projection side, and the launcher's own pages are never shown while the
 * strip is active. */
#include "fusion_dashboard.h"
#include "fusion_display.h"
#include "video_layer_policy.h"
#include "video_sink_lubanlite.h"
#include "rtcp/rtcp.h"
#include <rtthread.h>
#include <stdio.h>
#include <string.h>
#include "ui_language.h"
#include "carplay_ui_theme.h"
#include "icons/carplay_icons.h"
#define lv_label_set_text carplay_ui_set_text
#define TR                carplay_ui_translate

LV_FONT_DECLARE(carplay_display_115);
LV_FONT_DECLARE(carplay_display_58);
LV_FONT_DECLARE(carplay_ui_18);
LV_FONT_DECLARE(carplay_ui_17);
LV_FONT_DECLARE(carplay_ui_15);

/* Reference geometry, svg/05-fusion_*.svg, in panel coordinates. */
#define STRIP_X 768
#define STRIP_Y 0
#define STRIP_W 256
#define STRIP_H 600
#define PAD     26 /* 794 - 768 */

static lv_obj_t *strip, *launcher_screen;
static lv_obj_t *speed_caption, *speed_label, *unit_label;
static lv_obj_t *soc_caption, *soc_label, *soc_bar;
static lv_obj_t *home_button;
static bool showing_launcher;
static unsigned shown_speed;
static unsigned shown_soc;

typedef enum
{
    STRIP_HIDDEN,
    STRIP_MODE_FUSION,
    STRIP_MODE_FULLSCREEN
} strip_mode_t;
static strip_mode_t strip_mode = STRIP_HIDDEN;

static lv_obj_t *line(lv_obj_t *parent, int x, int y, int w, int h, carplay_style_role_t role)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    carplay_ui_style(obj, role);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int top, int w, const lv_font_t *font,
                       carplay_style_role_t role)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, top);
    lv_obj_set_width(obj, w);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_line_space(obj, 0, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    carplay_ui_style(obj, role);
    return obj;
}

static void show_dashboard(lv_event_t *event)
{
    (void)event;
    carplay_fusion_ui_resume();
}

void carplay_fusion_ui_show_launcher(void)
{
    showing_launcher = true;
    (void)carplay_fusion_set_mode(CARPLAY_LOCAL);
    if (launcher_screen && lv_screen_active() != launcher_screen)
        lv_screen_load(launcher_screen);
}

void carplay_fusion_ui_resume(void)
{
    demo_status_t status = { 0 };
    showing_launcher = false;
    if (carplay_connection_status(&status))
        (void)carplay_fusion_set_mode(status.display_reconfiguring ? CARPLAY_LOCAL
                                                                   : status.display_requested);
}

static void build_strip(lv_obj_t *parent)
{
    strip = lv_obj_create(parent);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(strip, STRIP_X, STRIP_Y);
    lv_obj_set_size(strip, STRIP_W, STRIP_H);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_shadow_width(strip, 0, 0);
    lv_obj_set_style_outline_width(strip, 0, 0);
    carplay_ui_style(strip, CARPLAY_STYLE_STRIP);

    /* Left hairline: the only column at x = 768. */
    line(strip, 0, 0, 1, STRIP_H, CARPLAY_STYLE_LINE_BORDER);

    speed_caption = label(strip, PAD, 72, 200, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    lv_label_set_text(speed_caption, "Speed");

    speed_label = label(strip, 0, 150, STRIP_W, &carplay_display_115, CARPLAY_STYLE_TEXT);
    lv_obj_set_style_text_align(speed_label, LV_TEXT_ALIGN_CENTER, 0);

    unit_label = label(strip, 0, 268, STRIP_W, &carplay_ui_18, CARPLAY_STYLE_MUTED);
    lv_label_set_text(unit_label, "km/h");
    lv_obj_set_style_text_align(unit_label, LV_TEXT_ALIGN_CENTER, 0);

    line(strip, PAD - 1, 322, 202, 1, CARPLAY_STYLE_LINE_BORDER);

    soc_caption = label(strip, PAD, 346, 200, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    lv_label_set_text(soc_caption, "Battery");

    soc_label = label(strip, PAD, 378, 202, &carplay_display_58, CARPLAY_STYLE_TEXT);

    soc_bar = lv_bar_create(strip);
    lv_obj_remove_flag(soc_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(soc_bar, PAD, 469);
    lv_obj_set_size(soc_bar, 202, 9);
    lv_bar_set_range(soc_bar, 0, 100);
    lv_obj_set_style_radius(soc_bar, 4, 0);
    lv_obj_set_style_radius(soc_bar, 4, LV_PART_INDICATOR);
    carplay_ui_style_part(soc_bar, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(soc_bar, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);

    home_button = lv_obj_create(strip);
    lv_obj_remove_flag(home_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(home_button, PAD - 10, 526);
    lv_obj_set_size(home_button, 224, 55);
    lv_obj_set_style_radius(home_button, 13, 0);
    lv_obj_set_style_pad_all(home_button, 0, 0);
    lv_obj_set_style_shadow_width(home_button, 0, 0);
    lv_obj_set_style_outline_width(home_button, 0, 0);
    carplay_ui_style(home_button, CARPLAY_STYLE_STRIP_BUTTON);
    lv_obj_add_flag(home_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_button, show_dashboard, LV_EVENT_CLICKED, NULL);

    /* Official Lucide arrow-left mask; the local pages use the same asset. */
    lv_obj_t *arrow = lv_image_create(home_button);
    lv_obj_remove_flag(arrow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(arrow, &carplay_icon_arrow_left_20);
    lv_obj_set_pos(arrow, 22, 17);
    carplay_ui_style(arrow, CARPLAY_STYLE_ICON_ACCENT);

    lv_obj_t *caption = label(home_button, 44, 16, 170, &carplay_ui_15, CARPLAY_STYLE_TEXT);
    lv_label_set_text(caption, "Dashboard");
}

void carplay_fusion_ui_init(lv_obj_t *launcher, uint16_t width, uint16_t height)
{
    if (strip || carplay_fusion_init(width, height))
        return;
    launcher_screen = launcher;
    lv_obj_t *screen = lv_obj_create(NULL);
    /* The strip screen must stay transparent: the left 768 px belong to the
     * video sink, and a themed screen background would repaint them on every
     * theme change. It is deliberately not registered with carplay_ui_style. */
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_outline_width(screen, 0, 0);
    lv_obj_set_style_shadow_width(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    build_strip(screen);
    showing_launcher = true;
}

/* Returns true while the strip owns presentation (FUSION or FULLSCREEN), so
 * the launcher pages stop invalidating themselves behind the projection. */
bool carplay_fusion_ui_update(const demo_status_t *status)
{
    (void)status;
    if (!strip)
        return false;
    uint32_t now = rt_tick_get_millisecond();
    carplay_fusion_snapshot_t s;
    carplay_fusion_snapshot(now, &s);
    int result = carplay_launcher_fusion_apply(&s);
    if (result)
    {
        carplay_fusion_display_error(result);
        (void)carplay_fusion_set_mode(CARPLAY_LOCAL);
        carplay_fusion_snapshot(now, &s);
        (void)carplay_launcher_fusion_apply(&s);
    }
    else
        carplay_fusion_display_error(0);

    if (s.effective != CARPLAY_LOCAL)
        showing_launcher = false;

    if (showing_launcher || s.effective == CARPLAY_LOCAL)
    {
        strip_mode = STRIP_HIDDEN;
        if (launcher_screen && lv_screen_active() != launcher_screen)
            lv_screen_load(launcher_screen);
        return false;
    }

    lv_obj_t *strip_screen = lv_obj_get_parent(strip);
    if (lv_screen_active() != strip_screen)
        lv_screen_load(strip_screen);

    if (s.effective == CARPLAY_FULLSCREEN)
    {
        /* The DE makes the whole UI layer transparent; draw nothing visible
         * and hide the strip so a stale frame can never be composited. */
        if (strip_mode != STRIP_MODE_FULLSCREEN)
        {
            lv_obj_add_flag(strip, LV_OBJ_FLAG_HIDDEN);
            strip_mode = STRIP_MODE_FULLSCREEN;
        }
        return true;
    }

    if (strip_mode != STRIP_MODE_FUSION)
    {
        lv_obj_remove_flag(strip, LV_OBJ_FLAG_HIDDEN);
        strip_mode = STRIP_MODE_FUSION;
    }

    /* Only speed and SOC; no gear, RPM, range or diagnostic text. */
    const carplay_vehicle_sample_t *v = &s.vehicle;
    unsigned speed = v->available ? v->speed_kph : 0;
    unsigned soc = v->available ? v->battery_percent : 0;
    if (!v->available)
    {
        if (shown_speed != UINT16_MAX)
        {
            lv_label_set_text(speed_label, "--");
            lv_label_set_text(soc_label, "--%");
            lv_bar_set_value(soc_bar, 0, LV_ANIM_OFF);
            shown_speed = UINT16_MAX;
        }
    }
    else if (speed != shown_speed || soc != shown_soc)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%u", speed);
        lv_label_set_text(speed_label, buffer);
        snprintf(buffer, sizeof(buffer), "%u%%", soc);
        lv_label_set_text(soc_label, buffer);
        lv_bar_set_value(soc_bar, (int32_t)soc, LV_ANIM_OFF);
        shown_speed = speed;
        shown_soc = soc;
    }
    return true;
}
#endif

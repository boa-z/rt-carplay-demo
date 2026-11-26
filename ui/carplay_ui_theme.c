/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "carplay_ui_theme.h"

static const uint32_t palette[2][CARPLAY_UI_ROLE_COUNT] = {
    { 0x111214, 0x1B1C1F, 0x25272B, 0x2D2F34, 0x3C3F45, 0xF2F3F5, 0xC5C8CF,
      0xA0A5AF, 0xA8C7FF, 0x233550, 0xF6BC65, 0xF99797, 0x17181B, 0x4B505A },
    { 0xF2F3F5, 0xFFFFFF, 0xEDEEF1, 0xE2E5EB, 0xD0D3DA, 0x202227, 0x484E59,
      0x555D6A, 0x204F9F, 0xDDE8FC, 0x804800, 0xA3263B, 0xFFFFFF, 0xB7BFCD },
};

static bool dark = true;
static bool initialized;
static lv_style_t styles[CARPLAY_STYLE_COUNT];
static void (*theme_hook)(void);

bool carplay_ui_dark(void)
{
    return dark;
}

uint32_t carplay_ui_color(carplay_ui_role_t role)
{
    return palette[dark ? 0 : 1][(unsigned)role < CARPLAY_UI_ROLE_COUNT ? (unsigned)role : 0];
}

static lv_color_t color(carplay_ui_role_t role)
{
    return lv_color_hex(carplay_ui_color(role));
}

static void surface(carplay_style_role_t role, carplay_ui_role_t fill, unsigned border,
                    carplay_ui_role_t edge)
{
    lv_style_t *style = &styles[role];
    lv_style_set_bg_color(style, color(fill));
    lv_style_set_bg_opa(style, LV_OPA_COVER);
    lv_style_set_border_width(style, border);
    if (border)
        lv_style_set_border_color(style, color(edge));
}

static void ink(carplay_style_role_t role, carplay_ui_role_t foreground)
{
    lv_style_set_text_color(&styles[role], color(foreground));
}

static void line_color(carplay_style_role_t role, carplay_ui_role_t foreground)
{
    lv_style_set_line_color(&styles[role], color(foreground));
    lv_style_set_bg_opa(&styles[role], LV_OPA_TRANSP);
    lv_style_set_border_width(&styles[role], 0);
}

static void icon_color(carplay_style_role_t role, carplay_ui_role_t foreground)
{
    lv_style_set_image_recolor(&styles[role], color(foreground));
    lv_style_set_image_recolor_opa(&styles[role], LV_OPA_COVER);
    lv_style_set_bg_opa(&styles[role], LV_OPA_TRANSP);
    lv_style_set_border_width(&styles[role], 0);
}

static void update_styles(void)
{
    surface(CARPLAY_STYLE_SCREEN, CARPLAY_UI_BG, 0, CARPLAY_UI_BORDER);
    surface(CARPLAY_STYLE_CARD, CARPLAY_UI_SURFACE, 1, CARPLAY_UI_BORDER);
    surface(CARPLAY_STYLE_CARD_ALT, CARPLAY_UI_SURFACE2, 0, CARPLAY_UI_BORDER);
    surface(CARPLAY_STYLE_ACCENT_CARD, CARPLAY_UI_SOFT, 1, CARPLAY_UI_ACCENT);
    surface(CARPLAY_STYLE_TILE, CARPLAY_UI_SOFT, 0, CARPLAY_UI_BORDER);
    ink(CARPLAY_STYLE_TEXT, CARPLAY_UI_TEXT);
    ink(CARPLAY_STYLE_MUTED, CARPLAY_UI_MUTED);
    ink(CARPLAY_STYLE_FAINT, CARPLAY_UI_FAINT);
    ink(CARPLAY_STYLE_ACCENT_TEXT, CARPLAY_UI_ACCENT);
    ink(CARPLAY_STYLE_WARN_TEXT, CARPLAY_UI_RED);
    surface(CARPLAY_STYLE_INPUT, CARPLAY_UI_SURFACE2, 1, CARPLAY_UI_BORDER);
    ink(CARPLAY_STYLE_INPUT, CARPLAY_UI_TEXT);
    lv_style_set_border_color(&styles[CARPLAY_STYLE_INPUT_CURSOR], color(CARPLAY_UI_ACCENT));
    lv_style_set_bg_color(&styles[CARPLAY_STYLE_BAR_BG], color(CARPLAY_UI_BAR));
    lv_style_set_bg_opa(&styles[CARPLAY_STYLE_BAR_BG], LV_OPA_COVER);
    lv_style_set_bg_color(&styles[CARPLAY_STYLE_BAR_FILL], color(CARPLAY_UI_ACCENT));
    lv_style_set_bg_opa(&styles[CARPLAY_STYLE_BAR_FILL], LV_OPA_COVER);
    surface(CARPLAY_STYLE_SLIDER_KNOB, CARPLAY_UI_SURFACE, 3, CARPLAY_UI_ACCENT);
    surface(CARPLAY_STYLE_SEGMENT, CARPLAY_UI_SURFACE2, 0, CARPLAY_UI_BORDER);
    surface(CARPLAY_STYLE_SEGMENT_ON, CARPLAY_UI_SOFT, 1, CARPLAY_UI_ACCENT);
    ink(CARPLAY_STYLE_SEGMENT_TEXT, CARPLAY_UI_MUTED);
    ink(CARPLAY_STYLE_SEGMENT_TEXT_ON, CARPLAY_UI_ACCENT);
    line_color(CARPLAY_STYLE_LINE_TEXT, CARPLAY_UI_TEXT);
    line_color(CARPLAY_STYLE_LINE_ACCENT, CARPLAY_UI_ACCENT);
    surface(CARPLAY_STYLE_LINE_BORDER, CARPLAY_UI_BORDER, 0, CARPLAY_UI_BORDER);
    lv_style_set_bg_color(&styles[CARPLAY_STYLE_DOT_OFF], color(CARPLAY_UI_FAINT));
    lv_style_set_bg_opa(&styles[CARPLAY_STYLE_DOT_OFF], LV_OPA_COVER);
    lv_style_set_bg_color(&styles[CARPLAY_STYLE_DOT_ON], color(CARPLAY_UI_ACCENT));
    lv_style_set_bg_opa(&styles[CARPLAY_STYLE_DOT_ON], LV_OPA_COVER);
    icon_color(CARPLAY_STYLE_ICON_ACCENT, CARPLAY_UI_ACCENT);
    icon_color(CARPLAY_STYLE_ICON_TEXT, CARPLAY_UI_TEXT);
    icon_color(CARPLAY_STYLE_ICON_MUTED, CARPLAY_UI_MUTED);
    icon_color(CARPLAY_STYLE_ICON_FAINT, CARPLAY_UI_FAINT);
    surface(CARPLAY_STYLE_STRIP, CARPLAY_UI_PANEL, 0, CARPLAY_UI_BORDER);
    surface(CARPLAY_STYLE_STRIP_BUTTON, CARPLAY_UI_SURFACE2, 0, CARPLAY_UI_BORDER);
}

void carplay_ui_style_part(lv_obj_t *obj, carplay_style_role_t role,
                           lv_style_selector_t selector)
{
    if (!obj || (unsigned)role >= CARPLAY_STYLE_COUNT)
        return;
    if (!initialized)
    {
        for (unsigned index = 0; index < CARPLAY_STYLE_COUNT; ++index)
            lv_style_init(&styles[index]);
        update_styles();
        initialized = true;
    }
    for (unsigned index = 0; index < CARPLAY_STYLE_COUNT; ++index)
    {
        if (index != (unsigned)role &&
            lv_obj_replace_style(obj, &styles[index], &styles[role], selector))
            return;
    }
    lv_obj_add_style(obj, &styles[role], selector);
}

void carplay_ui_style(lv_obj_t *obj, carplay_style_role_t role)
{
    carplay_ui_style_part(obj, role, LV_PART_MAIN);
}

void carplay_ui_on_theme_change(void (*hook)(void))
{
    theme_hook = hook;
}

void carplay_ui_set_dark(bool value)
{
    if (dark == value)
        return;
    dark = value;
    if (initialized)
    {
        update_styles();
        for (unsigned index = 0; index < CARPLAY_STYLE_COUNT; ++index)
            lv_obj_report_style_change(&styles[index]);
    }
    if (theme_hook)
        theme_hook();
}

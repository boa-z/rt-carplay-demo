/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "carplay_ui_theme.h"
#include <string.h>

/* V4.0 tokens; keep assets/palette.json and this table in sync. */
static const uint32_t palette[2][CARPLAY_UI_ROLE_COUNT] = {
    { /* dark */
      0x0A1119, 0x131E28, 0x1C2B36, 0x223440, 0x30414D, 0xF2F7F9, 0xA6B8C1,
      0x738A97, 0x49D8BE, 0x123B3A, 0xF6BC65, 0xF07878, 0x0D1721, 0x364650 },
    { /* light */
      0xF2F5F6, 0xFFFFFF, 0xE9EFF1, 0xDEE8EC, 0xD2DDE1, 0x14242C, 0x4F6671,
      0x68808C, 0x007C70, 0xDEF1ED, 0xA35B00, 0xB93445, 0xFFFFFF, 0xCAD8DD },
};

static bool dark = true;
static void (*theme_hook)(void);

typedef struct
{
    lv_obj_t *obj;
    carplay_style_role_t role;
    lv_style_selector_t selector;
} styled_t;
static styled_t styled[384];

bool carplay_ui_dark(void)
{
    return dark;
}

uint32_t carplay_ui_color(carplay_ui_role_t role)
{
    return palette[dark ? 0 : 1][(unsigned)role < CARPLAY_UI_ROLE_COUNT ? (unsigned)role : 0];
}

static void retracted(lv_event_t *event)
{
    memset(lv_event_get_user_data(event), 0, sizeof(styled_t));
}

static void apply_style(const styled_t *s)
{
    lv_obj_t *obj = s->obj;
    lv_style_selector_t sel = s->selector;
    switch (s->role)
    {
    case CARPLAY_STYLE_SCREEN:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_BG)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    case CARPLAY_STYLE_CARD:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 1, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_BORDER)), sel);
        break;
    case CARPLAY_STYLE_CARD_ALT:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE2)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    case CARPLAY_STYLE_ACCENT_CARD:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_SOFT)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 1, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        break;
    case CARPLAY_STYLE_TILE:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_SOFT)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    case CARPLAY_STYLE_TEXT:
        lv_obj_set_style_text_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_TEXT)), sel);
        break;
    case CARPLAY_STYLE_MUTED:
        lv_obj_set_style_text_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_MUTED)), sel);
        break;
    case CARPLAY_STYLE_FAINT:
        lv_obj_set_style_text_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_FAINT)), sel);
        break;
    case CARPLAY_STYLE_ACCENT_TEXT:
        lv_obj_set_style_text_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        break;
    case CARPLAY_STYLE_WARN_TEXT:
        lv_obj_set_style_text_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_RED)), sel);
        break;
    case CARPLAY_STYLE_BAR_BG:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_BAR)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        break;
    case CARPLAY_STYLE_BAR_FILL:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        break;
    case CARPLAY_STYLE_SLIDER_KNOB:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 3, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        break;
    case CARPLAY_STYLE_SEGMENT:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE2)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    case CARPLAY_STYLE_SEGMENT_ON:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_SOFT)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 1, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        break;
    case CARPLAY_STYLE_SEGMENT_TEXT:
        lv_obj_set_style_text_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_MUTED)), sel);
        break;
    case CARPLAY_STYLE_SEGMENT_TEXT_ON:
        lv_obj_set_style_text_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        break;
    case CARPLAY_STYLE_LINE_TEXT:
        lv_obj_set_style_line_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_TEXT)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    case CARPLAY_STYLE_LINE_ACCENT:
        lv_obj_set_style_line_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    case CARPLAY_STYLE_LINE_BORDER:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_BORDER)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    case CARPLAY_STYLE_DOT_OFF:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_FAINT)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        break;
    case CARPLAY_STYLE_DOT_ON:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        break;
    case CARPLAY_STYLE_OUTLINE_ACCENT:
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, sel);
        lv_obj_set_style_border_width(obj, 2, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_ACCENT)), sel);
        break;
    case CARPLAY_STYLE_OUTLINE_FAINT:
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, sel);
        lv_obj_set_style_border_width(obj, 1, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_FAINT)), sel);
        break;
    case CARPLAY_STYLE_GRAPHIC:
    case CARPLAY_STYLE_GRAPHIC_TEXT:
    {
        /* Icons are drawn with line, arc and small rect primitives, so a
         * single role must cover every paint property they use. */
        uint32_t color = carplay_ui_color(s->role == CARPLAY_STYLE_GRAPHIC
                                              ? CARPLAY_UI_ACCENT
                                              : CARPLAY_UI_TEXT);
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(color), sel);
        lv_obj_set_style_line_color(obj, lv_color_hex(color), sel);
        lv_obj_set_style_arc_color(obj, lv_color_hex(color), sel);
        lv_obj_set_style_arc_opa(obj, LV_OPA_COVER, sel);
        break;
    }
    case CARPLAY_STYLE_STRIP:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_PANEL)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    case CARPLAY_STYLE_STRIP_BUTTON:
        lv_obj_set_style_bg_color(obj, lv_color_hex(carplay_ui_color(CARPLAY_UI_SURFACE2)), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_width(obj, 0, sel);
        break;
    default:
        break;
    }
}

void carplay_ui_style_part(lv_obj_t *obj, carplay_style_role_t role,
                           lv_style_selector_t selector)
{
    if (!obj)
        return;
    styled_t *slot = NULL;
    for (unsigned i = 0; i < sizeof(styled) / sizeof(styled[0]); ++i)
    {
        if (styled[i].obj == obj && styled[i].selector == selector)
        {
            slot = &styled[i];
            break;
        }
        if (!styled[i].obj && !slot)
            slot = &styled[i];
    }
    if (!slot)
        return;
    if (!slot->obj)
        lv_obj_add_event_cb(obj, retracted, LV_EVENT_DELETE, slot);
    slot->obj = obj;
    slot->role = role;
    slot->selector = selector;
    apply_style(slot);
}

void carplay_ui_style(lv_obj_t *obj, carplay_style_role_t role)
{
    carplay_ui_style_part(obj, role, 0);
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
    for (unsigned i = 0; i < sizeof(styled) / sizeof(styled[0]); ++i)
        if (styled[i].obj)
            apply_style(&styled[i]);
    if (theme_hook)
        theme_hook();
}

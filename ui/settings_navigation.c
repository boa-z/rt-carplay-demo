/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "settings_navigation.h"
#include "carplay_ui_theme.h"
#include "icons/carplay_icons.h"
#include "ui_language.h"

LV_FONT_DECLARE(carplay_ui_18);

lv_obj_t *carplay_settings_list(lv_obj_t *parent, int width, int height)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, width, height);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list, CARPLAY_SETTING_PADDING, 0);
    lv_obj_set_style_pad_row(list, CARPLAY_SETTING_GAP, 0);
    lv_obj_set_style_radius(list, CARPLAY_SETTING_RADIUS, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    carplay_ui_style(list, CARPLAY_STYLE_CARD);
    return list;
}

static void row_icon(lv_obj_t *row, const lv_image_dsc_t *asset, carplay_style_role_t role)
{
    lv_obj_t *image = lv_image_create(row);
    lv_image_set_src(image, asset);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    carplay_ui_style(image, role);
}

lv_obj_t *carplay_settings_row(lv_obj_t *parent, const char *caption,
                               const lv_image_dsc_t *icon, bool opens_page,
                               lv_event_cb_t select, void *route)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), CARPLAY_SETTING_ROW_HEIGHT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, CARPLAY_SETTING_PADDING, 0);
    lv_obj_set_style_pad_column(row, CARPLAY_SETTING_PADDING, 0);
    lv_obj_set_style_radius(row, CARPLAY_SETTING_RADIUS, 0);
    carplay_ui_style(row, CARPLAY_STYLE_CARD_ALT);
    carplay_ui_style_part(row, CARPLAY_STYLE_ACCENT_CARD, LV_STATE_CHECKED);
    carplay_ui_style_part(row, CARPLAY_STYLE_ACCENT_CARD, LV_STATE_PRESSED);
    carplay_ui_style_part(row, CARPLAY_STYLE_ACCENT_CARD, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(row, select, LV_EVENT_CLICKED, route);

    if (icon)
        row_icon(row, icon, CARPLAY_STYLE_ICON_ACCENT);
    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_style_text_font(label, &carplay_ui_18, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    carplay_ui_style(label, CARPLAY_STYLE_TEXT);
    carplay_ui_set_text(label, caption);
    if (opens_page)
        row_icon(row, &carplay_icon_chevron_right_23, CARPLAY_STYLE_ICON_MUTED);
    return row;
}

lv_obj_t *carplay_settings_tab(lv_obj_t *parent, const char *caption,
                               const lv_image_dsc_t *icon, lv_event_cb_t select,
                               void *route)
{
    lv_obj_t *tab = carplay_settings_row(parent, caption, icon, false, select, route);
    lv_obj_set_height(tab, 64);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(tab, 8, 0);
    lv_obj_set_style_border_side(tab, LV_BORDER_SIDE_LEFT, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab, 3, LV_STATE_CHECKED);
    carplay_settings_tab_select(tab, false);
    return tab;
}

void carplay_settings_tab_select(lv_obj_t *tab, bool selected)
{
    if (selected)
        lv_obj_add_state(tab, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(tab, LV_STATE_CHECKED);
    carplay_ui_style(lv_obj_get_child(tab, 0),
                     selected ? CARPLAY_STYLE_ICON_ACCENT : CARPLAY_STYLE_ICON_MUTED);
    carplay_ui_style(lv_obj_get_child(tab, 1),
                     selected ? CARPLAY_STYLE_ACCENT_TEXT : CARPLAY_STYLE_MUTED);
}

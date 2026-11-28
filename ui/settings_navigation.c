/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "settings_navigation.h"
#include "carplay_ui_theme.h"
#include "icons/carplay_icons.h"
#include "launcher_layout.h"
#include "ui_language.h"

LV_FONT_DECLARE(carplay_ui_18);
LV_FONT_DECLARE(carplay_ui_14);
LV_FONT_DECLARE(carplay_ui_15);
LV_FONT_DECLARE(carplay_ui_17);
LV_FONT_DECLARE(carplay_ui_21);

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

const char * const settings_titles[SETTINGS_PAGE_COUNT] = {
    [SETTINGS_DISPLAY] = "Display",
    [SETTINGS_SOUND] = "Sound",
    [SETTINGS_CONNECTIONS] = "Connections",
    [SETTINGS_GENERAL] = "General",
    [SETTINGS_SYSTEM] = "System",
    [SETTINGS_DEVELOPER] = "Developer options",
    [SETTINGS_LANGUAGE] = "Language",
    [SETTINGS_ABOUT] = "About",
    [SETTINGS_CLOCK] = "Date & time",
    [SETTINGS_BLUETOOTH] = "Bluetooth",
    [SETTINGS_WIFI] = "Wi-Fi",
    [SETTINGS_ETHERNET] = "Ethernet",
    [SETTINGS_GNSS] = "GNSS simulation",
};

settings_page_t carplay_settings_section(settings_page_t route)
{
    switch (route)
    {
    case SETTINGS_WIFI:
    case SETTINGS_ETHERNET:
    case SETTINGS_BLUETOOTH:
        return SETTINGS_CONNECTIONS;
    case SETTINGS_LANGUAGE:
    case SETTINGS_CLOCK:
        return SETTINGS_GENERAL;
    case SETTINGS_ABOUT:
        return SETTINGS_SYSTEM;
    case SETTINGS_GNSS:
        return SETTINGS_DEVELOPER;
    default:
        return route;
    }
}

void carplay_settings_build(lv_obj_t *parent, lv_obj_t **heading, lv_obj_t **pages, lv_obj_t **tabs,
                            unsigned routes, lv_event_cb_t select, lv_event_cb_t back, lv_event_cb_t update)
{
    static const struct
    {
        settings_page_t route;
        const lv_image_dsc_t *icon;
    } items[] = {
        { SETTINGS_CONNECTIONS, &carplay_icon_link_24 },
        { SETTINGS_DISPLAY, &carplay_icon_sun_29 },
        { SETTINGS_SOUND, &carplay_icon_volume_29 },
        { SETTINGS_GENERAL, &carplay_icon_languages_24 },
        { SETTINGS_SYSTEM, &carplay_icon_info_26 },
        { SETTINGS_DEVELOPER, &carplay_icon_settings_24 },
    };
    lv_obj_t *sidebar = carplay_settings_frame_build(parent, heading, back);
    for (unsigned index = 0; index < sizeof(items) / sizeof(items[0]); ++index)
    {
        settings_page_t route = items[index].route;
        if (routes & CARPLAY_SETTINGS_ROUTE(route))
            tabs[route] = carplay_settings_tab(sidebar, settings_titles[route], items[index].icon,
                                               select, (void *)(uintptr_t)route);
    }
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
        pages[index] = carplay_settings_detail_create(parent);

    lv_obj_t *sections[SETTINGS_PAGE_COUNT] = { 0 };
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
    {
        settings_page_t section = carplay_settings_section(index);
        if (section == (settings_page_t)index || !(routes & CARPLAY_SETTINGS_ROUTE(index)) || !tabs[section])
            continue;
        if (!sections[section])
        {
            sections[section] = carplay_settings_list(pages[section], CARPLAY_SETTINGS_DETAIL_WIDTH - 4, LV_SIZE_CONTENT);
            if (section == SETTINGS_DEVELOPER)
                lv_obj_set_y(sections[section], 102);
        }
        carplay_settings_row(sections[section], settings_titles[index], NULL, true, select, (void *)(uintptr_t)index);
    }
    if (update && sections[SETTINGS_SYSTEM])
        carplay_settings_row(sections[SETTINGS_SYSTEM], "System update", &carplay_icon_download_26, true, update, NULL);
}

void carplay_settings_show(lv_obj_t *heading, lv_obj_t **pages, lv_obj_t **tabs, settings_page_t route)
{
    carplay_ui_set_text(heading, settings_titles[route]);
    settings_page_t section = carplay_settings_section(route);
    for (unsigned index = 0; index < SETTINGS_PAGE_COUNT; ++index)
    {
        if (index == (unsigned)route)
            lv_obj_remove_flag(pages[index], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(pages[index], LV_OBJ_FLAG_HIDDEN);
        if (tabs[index])
            carplay_settings_tab_select(tabs[index], index == (unsigned)section);
    }
}

static lv_obj_t *settings_segment(lv_obj_t *parent, const char *label, int x, int y, int width,
                                  int height, lv_event_cb_t select, unsigned choice)
{
    lv_obj_t *button = carplay_launcher_panel(parent, x, y, width, height, 9, CARPLAY_STYLE_SEGMENT);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, select, LV_EVENT_CLICKED, (void *)(uintptr_t)choice);
    lv_obj_center(carplay_launcher_label(button, label, 0, 0, 0, &carplay_ui_15, CARPLAY_STYLE_SEGMENT_TEXT));
    return button;
}

void carplay_settings_language_build(lv_obj_t *parent, lv_obj_t *buttons[2], lv_event_cb_t select)
{
    lv_obj_t *card = carplay_launcher_panel(parent, 0, 0, CARPLAY_SETTINGS_DETAIL_WIDTH - 4, 90, 16, CARPLAY_STYLE_CARD);
    carplay_launcher_label(card, "Language", 24, 34, 230, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    carplay_launcher_panel(card, 301, 17, 304, 56, 11, CARPLAY_STYLE_SEGMENT);
    buttons[0] = settings_segment(card, "简体中文", 303, 20, 150, 50, select, 1);
    buttons[1] = settings_segment(card, "English", 453, 20, 150, 50, select, 0);
}

void carplay_settings_phone_appearance_build(lv_obj_t *parent, lv_obj_t *buttons[3], lv_event_cb_t select)
{
    carplay_launcher_panel(parent, 22, 59, CARPLAY_SETTINGS_DETAIL_WIDTH - 48, 1, 0, CARPLAY_STYLE_LINE_BORDER);
    carplay_launcher_label(parent, "Phone appearance", 22, 78, 310, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    carplay_launcher_label(parent, "Auto uses daylight on this board", 22, 100, 310, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    const char *choices[] = { "Auto", "Light", "Dark" };
    for (unsigned index = 0; index < 3; ++index)
        buttons[index] = settings_segment(parent, choices[index], CARPLAY_SETTINGS_DETAIL_WIDTH - 332 + 102 * index,
                                          66, 100, 48, select, index);
}

void carplay_settings_fps_build(lv_obj_t *parent, lv_obj_t *buttons[2], lv_event_cb_t select)
{
    lv_obj_t *card = carplay_launcher_panel(parent, 0, 0, CARPLAY_SETTINGS_DETAIL_WIDTH - 4, 90, 12, CARPLAY_STYLE_CARD);
    carplay_launcher_label(card, "CarPlay frame rate", 24, 19, 265, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    carplay_launcher_label(card, "Requested maximum", 24, 48, 265, &carplay_ui_14, CARPLAY_STYLE_MUTED);
    buttons[0] = settings_segment(card, "30 fps", 303, 20, 150, 50, select, 0);
    buttons[1] = settings_segment(card, "60 fps", 453, 20, 150, 50, select, 1);
}

void carplay_settings_readout_build(lv_obj_t *parent, const char * const *captions,
                                    const char * const *values, lv_obj_t **labels, unsigned count)
{
    lv_obj_t *card = carplay_launcher_panel(parent, 0, 0, CARPLAY_SETTINGS_DETAIL_WIDTH - 4, 12 + 84 * count, 16,
                                            CARPLAY_STYLE_CARD);
    for (unsigned index = 0; index < count; ++index)
    {
        int y = 12 + 84 * (int)index;
        if (index)
            carplay_launcher_panel(card, 24, y - 3, 580, 1, 0, CARPLAY_STYLE_LINE_BORDER);
        carplay_launcher_label(card, captions[index], 24, y + 9, 580, &carplay_ui_18, CARPLAY_STYLE_MUTED);
        lv_obj_t *value = carplay_launcher_label(card, values[index], 24, y + 37, 580, &carplay_ui_17, CARPLAY_STYLE_TEXT);
        lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
        if (labels)
            labels[index] = value;
    }
}

void carplay_settings_slider_build(lv_obj_t *parent, int y, const char *title, const lv_image_dsc_t *icon,
                                   lv_obj_t **value, lv_obj_t **output, unsigned min, unsigned max,
                                   lv_event_cb_t change, const char *hint)
{
    lv_obj_t *card = carplay_launcher_panel(parent, 0, y, CARPLAY_SETTINGS_DETAIL_WIDTH - 4, hint ? 128 : 86, 12,
                                            CARPLAY_STYLE_CARD);
    if (icon)
    {
        row_icon(card, icon, CARPLAY_STYLE_ICON_ACCENT);
        lv_obj_set_pos(lv_obj_get_child(card, 0), 23, 16);
    }
    carplay_launcher_label(card, title, icon ? 68 : 22, 16, 380, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    *value = carplay_launcher_label(card, "", CARPLAY_SETTINGS_DETAIL_WIDTH - 170, 17, 142,
                                    &carplay_ui_18, CARPLAY_STYLE_TEXT);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_pos(slider, 24, hint ? 67 : 54);
    lv_obj_set_size(slider, CARPLAY_SETTINGS_DETAIL_WIDTH - 52, 8);
    lv_obj_set_ext_click_area(slider, 18);
    lv_slider_set_range(slider, min, max);
    lv_obj_set_style_radius(slider, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 4, LV_PART_INDICATOR);
    carplay_ui_style_part(slider, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(slider, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider, 7, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    carplay_ui_style_part(slider, CARPLAY_STYLE_SLIDER_KNOB, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, change, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, change, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(slider, change, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, change, LV_EVENT_PRESS_LOST, NULL);
    *output = slider;
    if (hint)
        carplay_launcher_label(card, hint, 24, 107, 580, &carplay_ui_14, CARPLAY_STYLE_MUTED);
}

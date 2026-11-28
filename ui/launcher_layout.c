/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "launcher_layout.h"
#include "icons/carplay_icons.h"
#include "settings_navigation.h"
#include "system_shell.h"
#include "ui_language.h"

LV_FONT_DECLARE(carplay_ui_17);
LV_FONT_DECLARE(carplay_ui_15);
LV_FONT_DECLARE(carplay_ui_18);
LV_FONT_DECLARE(carplay_ui_21);
LV_FONT_DECLARE(carplay_ui_34);
LV_FONT_DECLARE(carplay_cjk_full_18);
LV_FONT_DECLARE(carplay_display_58);
LV_FONT_DECLARE(carplay_display_220);

lv_obj_t *carplay_launcher_surface(lv_obj_t *screen, int width, int height)
{
    if (width == CARPLAY_LOCAL_WIDTH && height == CARPLAY_LOCAL_HEIGHT)
        return screen;

    int scale = LV_MIN(width * LV_SCALE_NONE / CARPLAY_LOCAL_WIDTH,
                       height * LV_SCALE_NONE / CARPLAY_LOCAL_HEIGHT);
    lv_obj_t *surface = lv_obj_create(screen);
    lv_obj_remove_style_all(surface);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(surface, CARPLAY_LOCAL_WIDTH, CARPLAY_LOCAL_HEIGHT);
    lv_obj_set_pos(surface, (width - CARPLAY_LOCAL_WIDTH * scale / LV_SCALE_NONE) / 2,
                   (height - CARPLAY_LOCAL_HEIGHT * scale / LV_SCALE_NONE) / 2);
    lv_obj_set_style_transform_pivot_x(surface, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(surface, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_scale(surface, scale, LV_PART_MAIN);
    return surface;
}

lv_obj_t *carplay_launcher_panel(lv_obj_t *parent, int x, int y, int width, int height,
                                 int radius, carplay_style_role_t role)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    carplay_ui_style(obj, role);
    return obj;
}

lv_obj_t *carplay_launcher_label(lv_obj_t *parent, const char *value, int x, int y, int width,
                                 const lv_font_t *font, carplay_style_role_t role)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_line_space(label, 0, 0);
    carplay_ui_style(label, role);
    lv_obj_set_pos(label, x, y);
    if (width > 0)
    {
        lv_obj_set_width(label, width);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    }
    carplay_ui_set_text(label, value);
    return label;
}

void carplay_dashboard_build(carplay_dashboard_t *dashboard, lv_obj_t *parent,
                             lv_event_cb_t connectivity)
{
    lv_obj_t *vehicle = carplay_launcher_panel(parent, 24, 16, 510, 444, 12, CARPLAY_STYLE_CARD);
    carplay_launcher_label(vehicle, "Dashboard", 24, 18, 310, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    dashboard->note = carplay_launcher_label(vehicle, "Demo values", 327, 30, 160,
                                              &carplay_ui_17, CARPLAY_STYLE_MUTED);
    carplay_launcher_label(vehicle, "Speed", 24, 77, 300, &carplay_ui_21, CARPLAY_STYLE_MUTED);
    dashboard->speed = carplay_launcher_label(vehicle, "--", 24, 110, 345,
                                             &carplay_display_220, CARPLAY_STYLE_TEXT);
    carplay_launcher_label(vehicle, "km/h", 388, 221, 96, &carplay_ui_34, CARPLAY_STYLE_MUTED);
    lv_obj_t *divider = carplay_launcher_panel(vehicle, 24, 342, 462, 1, 0, CARPLAY_STYLE_LINE_BORDER);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    carplay_launcher_label(vehicle, "Battery", 24, 358, 242, &carplay_ui_21, CARPLAY_STYLE_MUTED);
    dashboard->charge = carplay_launcher_label(vehicle, "--", 360, 352, 88,
                                              &carplay_display_58, CARPLAY_STYLE_TEXT);
    carplay_launcher_label(vehicle, "%", 450, 369, 35, &carplay_ui_21, CARPLAY_STYLE_MUTED);
    lv_obj_t *bar = lv_bar_create(vehicle);
    dashboard->charge_bar = bar;
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(bar, 24, 402);
    lv_obj_set_size(bar, 462, 8);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    carplay_ui_style_part(bar, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(bar, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);
    carplay_instruments_bind(&dashboard->instruments, dashboard->speed, dashboard->charge, bar, false);

    lv_obj_t *connection = carplay_launcher_panel(parent, 554, 16, 446, 140, 12, CARPLAY_STYLE_CARD);
    carplay_launcher_label(connection, "Apple CarPlay", 20, 16, 380, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    dashboard->connection = carplay_launcher_label(connection, "", 20, 49, 402,
                                                    &carplay_ui_18, CARPLAY_STYLE_MUTED);
    lv_obj_t *button = carplay_launcher_panel(connection, 242, 83, 184, 44, 8, CARPLAY_STYLE_TILE);
    lv_obj_center(carplay_launcher_label(button, "Phone Hub", 0, 0, 0,
                                         &carplay_ui_18, CARPLAY_STYLE_ACCENT_TEXT));
    lv_obj_add_event_cb(button, connectivity, LV_EVENT_CLICKED, NULL);

    lv_obj_t *media = carplay_launcher_panel(parent, 554, 172, 446, 136, 12, CARPLAY_STYLE_CARD);
    carplay_launcher_label(media, "Now Playing", 20, 16, 400, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    lv_obj_t *cover = carplay_launcher_panel(media, 20, 54, 64, 64, 8, CARPLAY_STYLE_SEGMENT);
    dashboard->cover_placeholder = lv_label_create(cover);
    lv_label_set_text(dashboard->cover_placeholder, LV_SYMBOL_AUDIO);
    lv_obj_center(dashboard->cover_placeholder);
    dashboard->cover = lv_image_create(cover);
    lv_obj_center(dashboard->cover);
    lv_obj_add_flag(dashboard->cover, LV_OBJ_FLAG_HIDDEN);
    dashboard->media = carplay_launcher_label(media, "", 96, 54, 330, &carplay_ui_18, CARPLAY_STYLE_MUTED);
    dashboard->artist = carplay_launcher_label(media, "", 96, 76, 330, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    dashboard->playback = carplay_launcher_label(media, "", 96, 98, 330, &carplay_ui_17, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(dashboard->media, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(dashboard->artist, LV_LABEL_LONG_DOT);
    lv_obj_t *route = carplay_launcher_panel(parent, 554, 324, 446, 136, 12, CARPLAY_STYLE_CARD);
    carplay_launcher_label(route, "Route guidance", 20, 16, 400, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    dashboard->route = carplay_launcher_label(route, "", 20, 53, 404,
                                               &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(dashboard->route, LV_LABEL_LONG_WRAP);
}

void carplay_phone_hub_build(carplay_phone_hub_t *hub, lv_obj_t *parent, lv_event_cb_t open_details)
{
    carplay_launcher_label(parent, "Phone Hub", 24, 12, 600, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    lv_obj_t *media = carplay_launcher_panel(parent, 24, 174, 480, 226, 12, CARPLAY_STYLE_CARD);
    lv_obj_t *cover = carplay_launcher_panel(media, 20, 63, 96, 96, 10, CARPLAY_STYLE_SEGMENT);
    lv_obj_set_style_clip_corner(cover, true, 0);
    hub->cover_placeholder = lv_label_create(cover);
    lv_label_set_text(hub->cover_placeholder, LV_SYMBOL_AUDIO);
    lv_obj_center(hub->cover_placeholder);
    hub->cover = lv_image_create(cover);
    lv_obj_center(hub->cover);
    lv_obj_add_flag(hub->cover, LV_OBJ_FLAG_HIDDEN);
    hub->media_caption = carplay_launcher_label(media, "Now Playing", 20, 17, 220, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    hub->playback = carplay_launcher_label(media, "", 248, 17, 210, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    lv_obj_set_style_text_align(hub->playback, LV_TEXT_ALIGN_RIGHT, 0);
    hub->title = carplay_launcher_label(media, "-", 134, 73, 324, &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
    hub->artist = carplay_launcher_label(media, "", 134, 109, 324, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    lv_label_set_long_mode(hub->title, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(hub->artist, LV_LABEL_LONG_DOT);
    hub->progress = lv_bar_create(media);
    lv_obj_set_pos(hub->progress, 20, 177);
    lv_obj_set_size(hub->progress, 438, 5);
    lv_obj_remove_flag(hub->progress, LV_OBJ_FLAG_CLICKABLE);
    carplay_ui_style_part(hub->progress, CARPLAY_STYLE_BAR_BG, LV_PART_MAIN);
    carplay_ui_style_part(hub->progress, CARPLAY_STYLE_BAR_FILL, LV_PART_INDICATOR);
    lv_bar_set_range(hub->progress, 0, 1000);
    hub->position = carplay_launcher_label(media, "", 20, 193, 438, &carplay_ui_15, CARPLAY_STYLE_MUTED);

    lv_obj_t *route = carplay_launcher_panel(parent, 520, 174, 480, 226, 12, CARPLAY_STYLE_CARD);
    hub->route_caption = carplay_launcher_label(route, "Route guidance", 20, 17, 230, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    hub->remaining = carplay_launcher_label(route, "", 270, 17, 188, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    lv_obj_set_style_text_align(hub->remaining, LV_TEXT_ALIGN_RIGHT, 0);
    hub->destination = carplay_launcher_label(route, "-", 20, 48, 438, &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
    hub->road = carplay_launcher_label(route, "", 20, 74, 300, &carplay_cjk_full_18, CARPLAY_STYLE_MUTED);
    hub->turn_distance = carplay_launcher_label(route, "", 324, 74, 134, &carplay_cjk_full_18, CARPLAY_STYLE_ACCENT_TEXT);
    lv_obj_set_style_text_align(hub->turn_distance, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(hub->remaining, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(hub->destination, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(hub->road, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(hub->turn_distance, LV_LABEL_LONG_DOT);
    lv_obj_t *divider = carplay_launcher_panel(route, 20, 101, 438, 1, 0, CARPLAY_STYLE_LINE_BORDER);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    for (unsigned row = 0; row < CARPLAY_HUB_MANEUVER_ROWS; ++row)
    {
        hub->maneuvers[row] = carplay_launcher_label(route, "", 20, 106 + 23 * (int)row, 438,
                                                    &carplay_cjk_full_18, CARPLAY_STYLE_TEXT);
        lv_label_set_long_mode(hub->maneuvers[row], LV_LABEL_LONG_DOT);
    }
    hub->lane_strip = carplay_launcher_panel(route, 20, 182, 438, 32, 8, CARPLAY_STYLE_SEGMENT);
    lv_obj_add_flag(hub->lane_strip, LV_OBJ_FLAG_HIDDEN);
    for (unsigned lane = 0; lane < CARPLAY_HUB_LANES; ++lane)
        hub->lanes[lane] = carplay_launcher_label(hub->lane_strip, "", 18 + 53 * (int)lane, 6, 35,
                                                  LV_FONT_DEFAULT, CARPLAY_STYLE_MUTED);
    lv_obj_t *details = carplay_settings_list(parent, 976, LV_SIZE_CONTENT);
    lv_obj_set_pos(details, 24, 412);
    lv_obj_set_style_pad_all(details, 0, 0);
    carplay_settings_row(details, "Phone details", &carplay_icon_info_26, true, open_details, NULL);
}

lv_obj_t *carplay_connection_card_build(lv_obj_t *parent, lv_obj_t **state, lv_obj_t **transport)
{
    lv_obj_t *card = carplay_launcher_panel(parent, 24, 63, 976, 99, 12, CARPLAY_STYLE_CARD);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *phone = lv_image_create(card);
    lv_image_set_src(phone, &carplay_icon_smartphone_42);
    lv_obj_set_pos(phone, 18, 16);
    carplay_ui_style(phone, CARPLAY_STYLE_ICON_ACCENT);
    carplay_launcher_label(card, "Apple CarPlay", 76, 12, 360, &carplay_ui_21, CARPLAY_STYLE_TEXT);
    *state = carplay_launcher_label(card, "Waiting for phone", 76, 40, 350, &carplay_ui_18, CARPLAY_STYLE_TEXT);
    *transport = carplay_launcher_label(card, "", 76, 66, 360, &carplay_ui_18, CARPLAY_STYLE_MUTED);
    return card;
}

lv_obj_t *carplay_settings_frame_build(lv_obj_t *parent, lv_obj_t **heading, lv_event_cb_t go_back)
{
    enum { SIDEBAR_WIDTH = 228 };
    int height = CARPLAY_LOCAL_HEIGHT - CARPLAY_SHELL_STATUS_HEIGHT - CARPLAY_SHELL_NAV_HEIGHT;
    lv_obj_t *rail = carplay_launcher_panel(parent, 0, 0, SIDEBAR_WIDTH, height, 0, CARPLAY_STYLE_CARD);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *divider = carplay_launcher_panel(parent, SIDEBAR_WIDTH - 1, 0, 1, height, 0, CARPLAY_STYLE_LINE_BORDER);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *back = carplay_launcher_panel(parent, 4, 9, 56, 64, 8, CARPLAY_STYLE_CARD_ALT);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    carplay_ui_style_part(back, CARPLAY_STYLE_CARD_ALT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, go_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = lv_image_create(back);
    lv_image_set_src(arrow, &carplay_icon_arrow_left_22);
    lv_obj_remove_flag(arrow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    carplay_ui_style(arrow, CARPLAY_STYLE_ICON_TEXT);
    lv_obj_center(arrow);
    carplay_launcher_label(parent, "Settings", 64, 23, SIDEBAR_WIDTH - 74, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    *heading = carplay_launcher_label(parent, "", CARPLAY_SETTINGS_DETAIL_X, 23,
                                      CARPLAY_SETTINGS_DETAIL_WIDTH - 114, &carplay_ui_34, CARPLAY_STYLE_TEXT);
    lv_obj_t *sidebar = carplay_settings_list(parent, SIDEBAR_WIDTH - 20, CARPLAY_SETTINGS_DETAIL_HEIGHT + 16);
    lv_obj_set_pos(sidebar, 10, CARPLAY_SETTINGS_CONTENT_TOP);
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    return sidebar;
}

lv_obj_t *carplay_settings_detail_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, CARPLAY_SETTINGS_DETAIL_X, CARPLAY_SETTINGS_CONTENT_TOP);
    lv_obj_set_size(page, CARPLAY_SETTINGS_DETAIL_WIDTH, CARPLAY_SETTINGS_DETAIL_HEIGHT);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    return page;
}

void carplay_appearance_build(lv_obj_t *parent, int top, lv_obj_t *buttons[2], lv_event_cb_t select)
{
    const int width = CARPLAY_SETTINGS_DETAIL_WIDTH - 4;
    const int tile_width = (width - 42) / 2;
    const char *themes[] = { "Light", "Dark" };
    carplay_launcher_label(parent, "Appearance", 6, top, 260, &carplay_ui_15, CARPLAY_STYLE_MUTED);
    lv_obj_t *card = carplay_launcher_panel(parent, 0, top + 22, width, 104, 12, CARPLAY_STYLE_CARD);
    for (unsigned index = 0; index < 2; ++index)
    {
        lv_obj_t *tile = carplay_launcher_panel(card, 14 + (tile_width + 14) * (int)index, 8,
                                                tile_width, 88, 9, CARPLAY_STYLE_SEGMENT);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, select, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        lv_obj_t *caption = carplay_launcher_label(tile, themes[index], 0, 0, 0,
                                                   &carplay_ui_18, CARPLAY_STYLE_SEGMENT_TEXT);
        lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -12);
        lv_obj_t *swatch = carplay_launcher_panel(tile, (tile_width - 68) / 2, 12, 68, 42, 5, CARPLAY_STYLE_CARD);
        lv_obj_remove_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(swatch, 0, 0);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(index ? 0x17181B : 0xFFFFFF), 0);
        for (int line = 0; line < 3; ++line)
        {
            lv_obj_t *mark = carplay_launcher_panel(swatch, 9, 10 + line * 9, line == 1 ? 28 : 48,
                                                    3, 0, CARPLAY_STYLE_LINE_BORDER);
            lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(mark, lv_color_hex(0x898F9B), 0);
        }
        buttons[index] = tile;
    }
    carplay_segments_select(buttons, 2, carplay_ui_dark() ? 1U : 0U);
}

void carplay_segments_select(lv_obj_t * const *buttons, unsigned count, unsigned selected)
{
    for (unsigned index = 0; index < count; ++index)
    {
        if (!buttons[index])
            continue;
        carplay_ui_style(buttons[index], index == selected ? CARPLAY_STYLE_SEGMENT_ON : CARPLAY_STYLE_SEGMENT);
        lv_obj_t *caption = lv_obj_get_child(buttons[index], 0);
        if (caption)
            carplay_ui_style(caption, index == selected ? CARPLAY_STYLE_SEGMENT_TEXT_ON : CARPLAY_STYLE_SEGMENT_TEXT);
    }
}

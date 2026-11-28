/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "lvgl.h"
#include "carplay_ui_theme.h"
#include "vehicle_instruments.h"

enum
{
    CARPLAY_LOCAL_WIDTH = 1024,
    CARPLAY_LOCAL_HEIGHT = 600
};

lv_obj_t *carplay_launcher_surface(lv_obj_t *screen, int width, int height);

typedef struct
{
    carplay_instruments_t instruments;
    lv_obj_t *speed, *charge, *charge_bar;
    lv_obj_t *note, *connection, *media, *artist, *playback, *route;
    lv_obj_t *cover, *cover_placeholder;
} carplay_dashboard_t;

lv_obj_t *carplay_launcher_panel(lv_obj_t *parent, int x, int y, int width, int height,
                                 int radius, carplay_style_role_t role);
lv_obj_t *carplay_launcher_label(lv_obj_t *parent, const char *value, int x, int y, int width,
                                 const lv_font_t *font, carplay_style_role_t role);
void carplay_dashboard_build(carplay_dashboard_t *dashboard, lv_obj_t *parent,
                             lv_event_cb_t connectivity);

enum
{
    CARPLAY_HUB_MANEUVER_ROWS = 3,
    CARPLAY_HUB_LANES = 8
};

typedef struct
{
    lv_obj_t *title, *artist, *playback, *progress, *position;
    lv_obj_t *cover, *cover_placeholder, *media_caption;
    lv_obj_t *road, *destination, *remaining, *route_caption, *turn_distance;
    lv_obj_t *maneuvers[CARPLAY_HUB_MANEUVER_ROWS], *lane_strip;
    lv_obj_t *lanes[CARPLAY_HUB_LANES];
} carplay_phone_hub_t;

void carplay_phone_hub_build(carplay_phone_hub_t *hub, lv_obj_t *parent, lv_event_cb_t open_details);

lv_obj_t *carplay_connection_card_build(lv_obj_t *parent, lv_obj_t **state, lv_obj_t **transport);
lv_obj_t *carplay_connection_action(lv_obj_t *card, lv_event_cb_t activate, lv_obj_t **label);

enum
{
    CARPLAY_SETTINGS_DETAIL_X = 252,
    CARPLAY_SETTINGS_DETAIL_WIDTH = 748,
    CARPLAY_SETTINGS_CONTENT_TOP = 68,
    CARPLAY_SETTINGS_DETAIL_HEIGHT = 387
};

lv_obj_t *carplay_settings_frame_build(lv_obj_t *parent, lv_obj_t **heading, lv_event_cb_t go_back);
lv_obj_t *carplay_settings_detail_create(lv_obj_t *parent);
void carplay_appearance_build(lv_obj_t *parent, int top, lv_obj_t *buttons[2], lv_event_cb_t select);
lv_obj_t *carplay_projection_settings_build(lv_obj_t *parent, lv_obj_t *buttons[2], lv_event_cb_t select);
void carplay_segments_select(lv_obj_t * const *buttons, unsigned count, unsigned selected);

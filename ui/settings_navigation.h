/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "lvgl.h"

enum
{
    CARPLAY_SETTING_GAP = 8,
    CARPLAY_SETTING_PADDING = 12,
    CARPLAY_SETTING_RADIUS = 12,
    CARPLAY_SETTING_ROW_HEIGHT = 56,
};

lv_obj_t *carplay_settings_list(lv_obj_t *parent, int width, int height);
lv_obj_t *carplay_settings_row(lv_obj_t *parent, const char *caption,
                               const lv_image_dsc_t *icon, bool opens_page,
                               lv_event_cb_t select, void *route);
lv_obj_t *carplay_settings_tab(lv_obj_t *parent, const char *caption,
                               const lv_image_dsc_t *icon, lv_event_cb_t select,
                               void *route);
void carplay_settings_tab_select(lv_obj_t *tab, bool selected);

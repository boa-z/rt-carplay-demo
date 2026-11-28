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

typedef enum
{
    SETTINGS_DISPLAY,
    SETTINGS_SOUND,
    SETTINGS_CONNECTIONS,
    SETTINGS_GENERAL,
    SETTINGS_SYSTEM,
    SETTINGS_DEVELOPER,
    SETTINGS_LANGUAGE,
    SETTINGS_WIFI,
    SETTINGS_ETHERNET,
    SETTINGS_CLOCK,
    SETTINGS_ABOUT,
    SETTINGS_BLUETOOTH,
    SETTINGS_GNSS,
    SETTINGS_PAGE_COUNT
} settings_page_t;

#define CARPLAY_SETTINGS_ROUTE(route) (1U << (route))

extern const char * const settings_titles[SETTINGS_PAGE_COUNT];
settings_page_t carplay_settings_section(settings_page_t route);
void carplay_settings_build(lv_obj_t *parent, lv_obj_t **heading, lv_obj_t **pages, lv_obj_t **tabs,
                            unsigned routes, lv_event_cb_t select, lv_event_cb_t back, lv_event_cb_t update);
void carplay_settings_show(lv_obj_t *heading, lv_obj_t **pages, lv_obj_t **tabs, settings_page_t route);
void carplay_settings_language_build(lv_obj_t *parent, lv_obj_t *buttons[2], lv_event_cb_t select);
void carplay_settings_phone_appearance_build(lv_obj_t *parent, lv_obj_t *buttons[3], lv_event_cb_t select);
void carplay_settings_fps_build(lv_obj_t *parent, lv_obj_t *buttons[2], lv_event_cb_t select);
void carplay_settings_readout_build(lv_obj_t *parent, const char * const *captions,
                                    const char * const *values, lv_obj_t **labels, unsigned count);
void carplay_settings_slider_build(lv_obj_t *parent, int y, const char *title, const lv_image_dsc_t *icon,
                                   lv_obj_t **value, lv_obj_t **slider, unsigned min, unsigned max,
                                   lv_event_cb_t change, const char *hint);

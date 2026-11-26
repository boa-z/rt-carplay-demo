/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "lvgl.h"

enum
{
    CARPLAY_SHELL_STATUS_HEIGHT = 40,
    CARPLAY_SHELL_NAV_HEIGHT = 76,
    CARPLAY_SHELL_DESTINATIONS = 3
};

typedef struct
{
    lv_obj_t *status;
    lv_obj_t *content;
    lv_obj_t *navigation;
    lv_obj_t *tabs[CARPLAY_SHELL_DESTINATIONS];
} carplay_system_shell_t;

void carplay_system_shell_build(carplay_system_shell_t *shell, lv_obj_t *parent,
                                lv_event_cb_t navigate);
void carplay_system_shell_select(carplay_system_shell_t *shell, unsigned destination);

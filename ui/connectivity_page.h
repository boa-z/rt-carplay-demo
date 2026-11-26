/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "connection_manager.h"
#include "lvgl.h"

typedef struct
{
    lv_obj_t *transport;
    lv_obj_t *state;
    lv_obj_t *action;
    lv_obj_t *action_label;
    lv_obj_t *capabilities;
} carplay_connectivity_view_t;

void carplay_connectivity_build(carplay_connectivity_view_t *view, lv_obj_t *page);
void carplay_connectivity_update(carplay_connectivity_view_t *view,
                                 const demo_status_t *status);
const char *carplay_connectivity_state(const demo_status_t *status);

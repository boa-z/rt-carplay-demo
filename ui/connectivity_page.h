/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "lvgl.h"

typedef struct
{
    lv_obj_t *transport;
    lv_obj_t *state;
    lv_obj_t *action;
    lv_obj_t *action_label;
    lv_obj_t *capabilities;
} carplay_connectivity_view_t;

typedef struct
{
    const char *state;
    const char *transport;
    const char *availability;
    bool resume, retry, actionable;
} carplay_connectivity_snapshot_t;

void carplay_connectivity_build(carplay_connectivity_view_t *view, lv_obj_t *page,
                                lv_event_cb_t activate);
void carplay_connectivity_update(const carplay_connectivity_view_t *view,
                                 const carplay_connectivity_snapshot_t *snapshot);

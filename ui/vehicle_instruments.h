/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "lvgl.h"

typedef struct
{
#if LV_USE_OBSERVER
    lv_subject_t speed;
    lv_subject_t charge;
#else
    lv_obj_t *speed_label, *charge_label, *charge_bar;
    int speed, charge;
    bool percent_suffix;
#endif
} carplay_instruments_t;

void carplay_instruments_bind(carplay_instruments_t *view, lv_obj_t *speed,
                              lv_obj_t *charge, lv_obj_t *bar, bool percent_suffix);
void carplay_instruments_update(carplay_instruments_t *view, bool valid,
                                unsigned speed, unsigned charge);

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "vehicle_instruments.h"

static void render_value(lv_obj_t *label, int value, bool percent_suffix)
{
    if (value < 0)
        lv_label_set_text_static(label, percent_suffix ? "--%" : "--");
    else
        lv_label_set_text_fmt(label, percent_suffix ? "%d%%" : "%d", value);
}

#if LV_USE_OBSERVER
static void number_changed(lv_observer_t *observer, lv_subject_t *subject)
{
    render_value(lv_observer_get_target_obj(observer), lv_subject_get_int(subject),
                 observer->user_data != NULL);
}

static void bar_changed(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_bar_set_value(lv_observer_get_target_obj(observer),
                     LV_MAX(0, lv_subject_get_int(subject)), LV_ANIM_OFF);
}
#endif

void carplay_instruments_bind(carplay_instruments_t *view, lv_obj_t *speed,
                              lv_obj_t *charge, lv_obj_t *bar, bool percent_suffix)
{
#if LV_USE_OBSERVER
    lv_subject_init_int(&view->speed, -1);
    lv_subject_init_int(&view->charge, -1);
    lv_subject_add_observer_obj(&view->speed, number_changed, speed, NULL);
    lv_subject_add_observer_obj(&view->charge, number_changed, charge, percent_suffix ? view : NULL);
    lv_subject_add_observer_obj(&view->charge, bar_changed, bar, NULL);
#else
    *view = (carplay_instruments_t){ speed, charge, bar, -1, -1, percent_suffix };
    render_value(speed, -1, false);
    render_value(charge, -1, percent_suffix);
#endif
}

void carplay_instruments_update(carplay_instruments_t *view, bool valid,
                                unsigned speed, unsigned charge)
{
    int next_speed = valid ? (int)speed : -1;
    int next_charge = valid ? (int)LV_MIN(charge, 100U) : -1;
#if LV_USE_OBSERVER
    if (lv_subject_get_int(&view->speed) != next_speed)
        lv_subject_set_int(&view->speed, next_speed);
    if (lv_subject_get_int(&view->charge) != next_charge)
        lv_subject_set_int(&view->charge, next_charge);
#else
    if (view->speed != next_speed)
    {
        view->speed = next_speed;
        render_value(view->speed_label, next_speed, false);
    }
    if (view->charge != next_charge)
    {
        view->charge = next_charge;
        render_value(view->charge_label, next_charge, view->percent_suffix);
        lv_bar_set_value(view->charge_bar, LV_MAX(0, next_charge), LV_ANIM_OFF);
    }
#endif
}

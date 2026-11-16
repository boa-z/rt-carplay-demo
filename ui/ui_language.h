/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "lvgl.h"
#include <stdbool.h>

/* GUI owner only. English is the boot default; selection is kept in RAM. */
bool carplay_ui_is_chinese(void);
void carplay_ui_set_chinese(bool chinese);
const char *carplay_ui_translate(const char *english);
void carplay_ui_set_text(lv_obj_t *label, const char *english);
void carplay_ui_set_text_fmt(lv_obj_t *label, const char *format, ...)
    LV_FORMAT_ATTRIBUTE(2, 3);

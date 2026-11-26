/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "lvgl.h"

enum
{
    CARPLAY_LOCAL_WIDTH = 1024,
    CARPLAY_LOCAL_HEIGHT = 600
};

lv_obj_t *carplay_launcher_surface(lv_obj_t *screen, int width, int height);

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "launcher_layout.h"

lv_obj_t *carplay_launcher_surface(lv_obj_t *screen, int width, int height)
{
    if (width == CARPLAY_LOCAL_WIDTH && height == CARPLAY_LOCAL_HEIGHT)
        return screen;

    int scale = LV_MIN(width * LV_SCALE_NONE / CARPLAY_LOCAL_WIDTH,
                       height * LV_SCALE_NONE / CARPLAY_LOCAL_HEIGHT);
    lv_obj_t *surface = lv_obj_create(screen);
    lv_obj_remove_style_all(surface);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(surface, CARPLAY_LOCAL_WIDTH, CARPLAY_LOCAL_HEIGHT);
    lv_obj_set_pos(surface, (width - CARPLAY_LOCAL_WIDTH * scale / LV_SCALE_NONE) / 2,
                   (height - CARPLAY_LOCAL_HEIGHT * scale / LV_SCALE_NONE) / 2);
    lv_obj_set_style_transform_pivot_x(surface, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(surface, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_scale(surface, scale, LV_PART_MAIN);
    return surface;
}

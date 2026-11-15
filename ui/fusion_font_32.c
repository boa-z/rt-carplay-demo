/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Reuse the SDK's existing LVGL font source; its upstream license applies.
 * A separate translation unit avoids duplicating generated static symbols. */
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "lvgl.h"
#if !LV_FONT_MONTSERRAT_32
#undef LV_FONT_MONTSERRAT_32
#define LV_FONT_MONTSERRAT_32 1
#include "../../../../packages/artinchip/lvgl-ui/lvgl_v9/lvgl/src/font/lv_font_montserrat_32.c"
#endif
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH         32
#define CACHE_LINE_SIZE        64
#define LV_USE_OS              LV_OS_NONE
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_CLIB
#define LV_USE_LOG             0
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_16
#define LV_FONT_FMT_TXT_LARGE  1
#define LV_USE_FONT_COMPRESSED 1
#define LV_THEME_DEFAULT_DARK  1
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_LODEPNG         1
#define LV_USE_TJPGD           1
#define LV_USE_FS_MEMFS        1
#define LV_FS_MEMFS_LETTER     'M'
#endif

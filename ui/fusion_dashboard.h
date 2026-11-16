/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "lvgl.h"
#include "fusion_display.h"
#include "connection_manager.h"
void carplay_fusion_ui_init(lv_obj_t *launcher, uint16_t width, uint16_t height);
/* GUI owner; true when the dashboard owns presentation (including fullscreen). */
bool carplay_fusion_ui_update(const demo_status_t *);
void carplay_fusion_ui_show_launcher(void);
void carplay_fusion_ui_resume(void);
/* One shared menu for dashboard, launcher settings and full-screen overlay. */
void carplay_launcher_open_display_settings(void);
#endif

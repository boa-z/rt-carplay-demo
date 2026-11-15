/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>

/* Opt-in policy for this application's full-screen launcher. Other hosts
 * retain their own HMI policy; the video backend never selects it for them.
 * Called by the sink worker on first presentation and by the session owner
 * after video shutdown, so it must not call LVGL or reenter the lifecycle. */
void carplay_launcher_video_event(bool active, void *context);
bool carplay_launcher_video_is_active(void);

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "fusion_display.h"
/* GUI-owner only. Hardware UI crop + alpha; never copies a video image. */
int carplay_launcher_fusion_apply(const carplay_fusion_snapshot_t *);
#endif

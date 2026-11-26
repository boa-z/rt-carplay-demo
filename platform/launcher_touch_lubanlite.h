/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool carplay_launcher_touch_init(void);
bool carplay_launcher_touch_bind(void);
int carplay_launcher_touch_start(uint16_t width, uint16_t height);
bool carplay_demo_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y);

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "fusion_display.h"
void carplay_launcher_touch_dispatch(carplay_fusion_touch_t action);
#endif

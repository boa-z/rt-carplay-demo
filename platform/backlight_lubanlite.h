/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Panel backlight for the D213 demo board. The schematic routes PC.7 as
 * LCD_PWM (PWM channel 3, output B) into the LED boost driver, so brightness
 * is the real PWM duty rather than a software tint. When the image is built
 * with a GPIO-only backlight the control stays disabled instead of faking it. */
#ifndef CARPLAY_BACKLIGHT_H
#define CARPLAY_BACKLIGHT_H

#include <stdbool.h>

/* Safe minimum: 5% keeps the panel readable and avoids an accidental blackout. */
#define CARPLAY_BRIGHTNESS_MIN 5U
#define CARPLAY_BRIGHTNESS_MAX 100U

bool carplay_backlight_available(void);

/* Current confirmed duty as a percentage. */
bool carplay_backlight_get(unsigned *percent);

/* Applies a percentage to the PWM channel. Values below the safe minimum are
 * clamped rather than refused. Returns false when no PWM backend exists. */
bool carplay_backlight_set(unsigned percent);

/* Re-applies a persisted value once the panel is already lit at boot. */
void carplay_backlight_restore(unsigned percent);

#endif

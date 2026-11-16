/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Local media volume for the D213 speaker path. This is the board codec's
 * mixer gain, not a phone Control Center volume. The AirPlay SET_PARAMETER
 * volume is a separate remote control and is not overwritten here. */
#ifndef CARPLAY_AUDIO_VOLUME_H
#define CARPLAY_AUDIO_VOLUME_H

#include <stdbool.h>

/* True when the local audio device exposes a mixer volume. Callers must
 * disable the slider when this returns false instead of simulating success. */
bool carplay_volume_available(void);

/* Current confirmed output value, 0..100. Returns false when unavailable. */
bool carplay_volume_get(unsigned *percent);

/* Applies the value to the real codec and reads it back. Returns false when the
 * backend is missing or the read-back does not match the request. */
bool carplay_volume_set(unsigned percent);

#endif

/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "airplay_audio_sender.h"
bool carplay_audio_input_available(void);
bool carplay_audio_input_start(const airplay_audio_input_config_t *);
/* Start/stop serialize with a nonblocking gate (start returns false on
 * contention). Stop returns RTCP_ESTATE for contention/self-join and waits
 * up to five seconds otherwise. RTCP_ETIMEDOUT retains worker ownership:
 * preserve the borrowed net table/context until a later stop succeeds.
 * Do not call lifecycle operations from device/network callbacks. */
int carplay_audio_input_stop(void);
/* Nonblocking request; the owner finishes an in-progress capture/packet step.
 * A generation also preserves requests arriving during startup validation. */
void carplay_audio_input_request_stop(void);
void carplay_audio_input_mute(bool);
bool carplay_audio_input_is_muted(void);
void carplay_audio_input_reference(const int16_t *stereo, size_t frames);
void carplay_audio_input_status(void);

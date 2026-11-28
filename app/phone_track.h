/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "rtcp/receiver.h"

typedef struct
{
    char title[129], artist[129], album[129];
    bool has_playing, playing;
    bool has_duration, has_elapsed;
    uint32_t duration_ms, elapsed_ms;
    uint32_t artwork_revision;
} carplay_phone_track_t;

void carplay_phone_track_update(carplay_phone_track_t *track,
                                const rtcp_now_playing_details_t *update,
                                bool artwork_has_transfer_lifecycle);

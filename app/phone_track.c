/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "phone_track.h"

#include <stdio.h>
#include <string.h>

void carplay_phone_track_update(carplay_phone_track_t *track,
                                const rtcp_now_playing_details_t *update,
                                bool artwork_has_transfer_lifecycle)
{
    if (update->invalidated)
    {
        *track = (carplay_phone_track_t){ 0 };
        return;
    }
    if (update->item.title)
    {
        if (strcmp(track->title, update->item.title))
        {
            track->has_duration = false;
            track->has_elapsed = false;
            if (!artwork_has_transfer_lifecycle || !*update->item.title)
                track->artwork_revision = 0;
        }
        snprintf(track->title, sizeof(track->title), "%s", update->item.title);
    }
    if (update->item.artist)
        snprintf(track->artist, sizeof(track->artist), "%s", update->item.artist);
    if (update->item.album)
        snprintf(track->album, sizeof(track->album), "%s", update->item.album);
    if (update->item.has_playing)
    {
        track->has_playing = true;
        track->playing = update->item.playing;
    }
    if (update->has_duration)
    {
        track->has_duration = true;
        track->duration_ms = update->duration_ms;
    }
    if (update->has_elapsed)
    {
        track->has_elapsed = true;
        track->elapsed_ms = update->elapsed_ms;
    }
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* SDK-specific exclusive dmic0 capture. Fixed 48 kHz stereo protects the
 * shared playback/capture sampling clock. All APIs have one worker owner. */
typedef struct rtcp_audio_capture rtcp_audio_capture_t;
int rtcp_audio_capture_open(rtcp_audio_capture_t **);
/* Nonblocking read of host-endian stereo frames. Returns frames or <0. */
int rtcp_audio_capture_read(rtcp_audio_capture_t *, int16_t *pcm, size_t max_frames);
/* Monotonic timestamp of the last read's final sample, based on the most
 * recent DMA arrival and remaining capture backlog. Zero means unavailable. */
uint64_t rtcp_audio_capture_end_us(rtcp_audio_capture_t *);
void rtcp_audio_capture_close(rtcp_audio_capture_t *);

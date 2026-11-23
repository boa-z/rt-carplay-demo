/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Luban-Lite video sink: MPP H.264 decoder -> DE video layer.
 *
 * The AirPlay Screen stream delivers Annex-B access units.  This sink splits
 * them into NAL units, decodes them on its own thread and presents the
 * resulting NV12 frames through the "aicfb" device.  It is the on-board
 * counterpart of the Linux simulator's H.264 dump and requires the SDK's
 * VE driver (CONFIG_AIC_MPP_H264_DEC_ENABLE).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Starts the decoder and takes over the video layer.  Returns 0 on success,
 * a negative RTCP error otherwise.  Safe to call again after stop(). */
int carplay_video_sink_start(uint16_t display_width, uint16_t display_height);

/* Optional host display policy. Register/unregister only while stopped, with
 * serialized start/stop ownership. true is delivered once per start, by the
 * decoder worker after the first successfully latched video frame. false is
 * delivered by stop after the worker is quiescent and video disabled, even if
 * no frame was displayed. No LVGL calls or lifecycle reentry from callbacks.
 * The context is borrowed until unregister returns; registration survives
 * restarts. With no observer, this sink never changes the host UI layer. */
typedef void (*carplay_video_display_cb_t)(bool active, void *context);
int carplay_video_sink_set_display_observer(carplay_video_display_cb_t, void *);

/* Ordinary-thread lifecycle calls serialize with a nonblocking gate. Contention
 * or worker self-join returns RTCP_ESTATE. Stop retires accepted queue/query
 * calls, then waits up to three seconds for the decoder. RTCP_ETIMEDOUT retains
 * all decoder/frame/display resources. RTCP_EIO retains those resources when
 * layer disable/vsync fails; retry stop before restart or releasing
 * the observer context. Success disables video and notifies host policy.
 * Lifecycle calls must not run from device/display callbacks. */
int carplay_video_sink_stop(void);

bool carplay_video_sink_is_running(void);
/* True only after a successful layer update and vsync in the current run.
 * Decoder frame statistics include failed presentations; they are not ready.
 * False while stopped/retiring. Host UI handoff is a separate policy result. */
bool carplay_video_sink_has_presented(void);

/* True after lost input or decoder failure until the stream recovers. */
bool carplay_video_sink_needs_keyframe(void);

/* NAL units waiting for the decoder.  Useful to pace a file feeder. */
size_t carplay_video_sink_pending(void);

/* Decoder counters since start; any output pointer may be NULL. Public queries
 * return false/zero while lifecycle retirement rejects new readers; frame
 * callbacks are ignored then. Counters are observational across fields. */
void carplay_video_sink_stats(unsigned *nals, unsigned *frames,
                              unsigned *dropped, unsigned *errors);

/* Drop-in carplay_app_screen_cb_t: queues the Annex-B data and returns. */
void carplay_video_sink_frame(bool configuration, const uint8_t *annex_b,
                              size_t annex_b_len, uint64_t sender_time_ns,
                              void *context);

/* Same queue admission, with the complete-message arrival time from the
 * RT-Thread receiver's monotonic clock. */
void carplay_video_sink_frame_at(bool configuration, const uint8_t *annex_b,
                                 size_t annex_b_len, uint64_t sender_time_ns,
                                 uint64_t arrival_time_us);

/* Coherent diagnostic counters copied with interrupts briefly masked on this
 * single-core target. These are elapsed wall times, not isolated CPU costs. */
typedef struct
{
    unsigned peak, queue_p95_ms, queue_max_ms, decode_p95_us, decode_max_us;
    unsigned packet_waits, packet_wait_max_us, frame_starves;
    unsigned submit_max_us, fence_max_us, fence_fallbacks;
    unsigned alloc_max_us, copy_max_us, lock_wait_max_us, lock_hold_max_us;
    unsigned overflows, overflow_depth, overflow_oldest_ms;
    unsigned overflow_packet_waits, overflow_frame_starves;
    unsigned pressure_peak, deferred_outputs, discarded_outputs, recovery_filtered;
    unsigned rx_latch_samples, rx_latch_p95_ms, rx_latch_max_ms;
} carplay_video_window_t;

typedef struct
{
    unsigned frames, latched, packets, drops, errors, pending, queue_peak;
    /* latched counts new decoded frames with a successful DE update/vsync,
     * including retained-frame retries, excluding static layout reapplication. */
    unsigned dequeued, decode_calls, present_calls;
    uint64_t queue_ms, decode_ms, present_ms;
    carplay_video_window_t window;
} carplay_video_perf_t;

/* Reads lifetime totals and closes the diagnostic window for its sole caller.
 * P95 is a histogram estimate. Maxima and samples are scoped to that window. */
void carplay_video_sink_perf(carplay_video_perf_t *stats);

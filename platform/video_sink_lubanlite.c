/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Luban-Lite video sink: MPP H.264 decoder -> DE video layer.
 *
 * The AirPlay poll thread calls carplay_video_sink_frame() with Annex-B
 * access units. The FIFO holds complete access units. A dedicated thread
 * feeds one picture NAL per decoder packet, pumps the
 * decoder and presents the resulting NV12 frames through the "aicfb" video
 * layer.  Only the sink thread touches the decoder and the display.
 *
 * Configuration messages may repeat within a live reference chain. Identical
 * SPS/PPS are ignored; changed configuration and decode errors require an IDR
 * before decoding resumes. Cached configuration is replayed on recovery.
 */
#include "video_sink_lubanlite.h"
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "fusion_display.h"
#endif

#include <rtthread.h>
#include <rtdevice.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <limits.h>

#include "artinchip_fb.h"
#include "mpp_decoder.h"
#include "rtcp/rtcp.h"
#include "rtcp_rtthread.h"

#define TAG "carplay-vid"

#define SINK_QUEUE_LENGTH       16U
#define SINK_MAX_UNIT_BYTES     (512U * 1024U)
#define SINK_THREAD_STACK_BYTES (16U * 1024U)
/* Consume queued video before the priority-15 network producer can fill the
 * FIFO. VE completion and vsync block this thread, allowing reception to run.
 * TCP/IP and USB workers retain their higher system priorities. */
#define SINK_THREAD_PRIORITY    14U
#define SINK_THREAD_TICK        20U
#define SINK_BITSTREAM_BYTES    (1024U * 1024U)
#define SINK_PACKET_COUNT       2
#define SINK_EXTRA_FRAMES       1
#define SINK_FENCE_TIMEOUT_MS   100U
#define SINK_MAX_PACKET_RETRIES 2000U

static const unsigned perf_bins_us[] = {
    1000,
    4000,
    8000,
    12000,
    16000,
    20000,
    25000,
    33000,
    50000,
    75000,
    100000,
    150000,
    250000,
    500000,
    UINT_MAX,
};
#define PERF_BIN_COUNT (sizeof(perf_bins_us) / sizeof(perf_bins_us[0]))

extern unsigned long long aic_get_time_us(void);

typedef struct
{
    uint8_t *data;
    size_t len;
    uint32_t pts_us;
    uint32_t arrival_ms;
    uint32_t queued_ms;
    uint32_t overflow_ms;
    bool configure;
    bool overflow_idr;
    unsigned loss_generation;
} nal_entry_t;

typedef struct
{
    uint32_t start_ms;
    uint32_t fence_start_us;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    rtcp_display_rect_t destination;
    uint32_t generation;
#endif
} scanout_timing_t;

typedef struct
{
    struct mpp_decoder *decoder;
    rt_device_t fb;
    rt_thread_t thread;
    rt_sem_t wake;
    rt_mutex_t lock;

    uint16_t display_width;
    uint16_t display_height;
    /* Panel geometry from aicfb; the DE layer works in these coordinates. */
    uint16_t panel_width;
    uint16_t panel_height;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    uint32_t fusion_generation;
#endif

    nal_entry_t queue[SINK_QUEUE_LENGTH];
    atomic_uint head;
    atomic_uint tail;
    atomic_bool queue_wait_idr;
    uint32_t queue_overflow_ms;
    atomic_uint queue_overflows;
    atomic_uint queue_discarded;
    atomic_uint queue_idr_samples;
    atomic_uint queue_idr_last_ms;
    atomic_uint queue_idr_max_ms;
    atomic_uint queue_latch_samples;
    atomic_uint queue_latch_last_ms;
    atomic_uint queue_latch_max_ms;
    bool queue_latch_pending;
    uint32_t queue_idr_accepted_ms;
    uint32_t queue_idr_pts_us;
    unsigned queue_idr_generation;

    struct mpp_frame held;
    bool have_frame;
    struct mpp_frame pending_scanout;
    bool have_pending_scanout; /* An attempted update has no confirmed latch. */
    struct mpp_frame ready_scanout;
    bool have_ready_scanout;
    bool ready_deferred;
    bool async_scanout;
    bool fence_armed;
    unsigned fence_sequence;
    scanout_timing_t fence_timing;
    atomic_bool display_reported;

    atomic_bool stop;
    atomic_bool running;
    atomic_bool thread_running;

    atomic_uint nal_count;
    atomic_uint frame_count;
    atomic_uint latched_frames;
    atomic_uint skipped_frames;
    atomic_uint dropped_nals;
    atomic_uint loss_generation;
    atomic_uint consumed_generation;
    atomic_uint decode_errors;
    atomic_uint decoder_resets;
    uint8_t *configuration;
    size_t configuration_len;
    atomic_uint repeated_configs;
    atomic_uint waiting_nals;
    atomic_bool wait_idr;
    atomic_bool recovery_pending;
    bool reset_pending;
    bool defer_present;
    unsigned display_pressure;
    uint32_t last_submit_ms;
    carplay_video_window_t window;
    unsigned queue_hist[PERF_BIN_COUNT];
    unsigned decode_hist[PERF_BIN_COUNT];
    unsigned rx_latch_hist[PERF_BIN_COUNT];
    atomic_uint queue_peak;
    atomic_uint dequeued;
    atomic_uint_fast64_t queue_wait_ms;
    atomic_uint queue_wait_max_ms;
    atomic_uint decode_calls;
    atomic_uint_fast64_t decode_ms;
    atomic_uint decode_max_ms;
    atomic_uint present_calls;
    atomic_uint_fast64_t present_ms;
    atomic_uint present_max_ms;
    uint32_t report_ms;
    atomic_uint report_frames;
    atomic_uint report_nals;
} video_sink_t;

static uint32_t sink_now_ms(void)
{
    return (uint32_t)rt_tick_get_millisecond();
}

static uint32_t sink_now_us(void)
{
    return (uint32_t)aic_get_time_us();
}

static void update_max(unsigned *maximum, unsigned sample)
{
    if (sample > *maximum)
        *maximum = sample;
}

static void observe_latency(unsigned *hist, unsigned sample)
{
    unsigned bin = 0;
    while (sample > perf_bins_us[bin])
        ++bin;
    ++hist[bin];
}

static unsigned ms_to_us(unsigned ms)
{
    return ms > UINT_MAX / 1000U ? UINT_MAX : ms * 1000U;
}

static unsigned latency_p95(const unsigned *hist, unsigned exact_max)
{
    unsigned total = 0;
    for (unsigned bin = 0; bin < PERF_BIN_COUNT; ++bin)
        total += hist[bin];
    unsigned needed = total - total / 20U;
    for (unsigned bin = 0; bin < PERF_BIN_COUNT; ++bin)
    {
        if (hist[bin] >= needed)
            return perf_bins_us[bin] < exact_max ? perf_bins_us[bin] : exact_max;
        needed -= hist[bin];
    }
    return exact_max;
}

static video_sink_t s_sink;
static carplay_video_display_cb_t s_display_observer;
static void *s_display_context;

#define VIDEO_RETIRING (UINT_MAX ^ (UINT_MAX >> 1))
static atomic_uint s_video_access;
static atomic_flag s_video_lifecycle = ATOMIC_FLAG_INIT;
static atomic_flag s_video_shell = ATOMIC_FLAG_INIT;

static bool video_read_enter(void)
{
    unsigned access = atomic_load(&s_video_access);
    do
    {
        if ((access & VIDEO_RETIRING) || access == VIDEO_RETIRING - 1U)
            return false;
    } while (!atomic_compare_exchange_weak(&s_video_access, &access, access + 1U));
    return true;
}

static void video_read_leave(void)
{
    atomic_fetch_sub(&s_video_access, 1U);
}

static void video_retire_readers(void)
{
    atomic_fetch_or(&s_video_access, VIDEO_RETIRING);
    while (atomic_load(&s_video_access) != VIDEO_RETIRING) rt_thread_mdelay(1);
}

static void video_lifecycle_leave(void)
{
    atomic_store(&s_video_access, 0U);
    atomic_flag_clear(&s_video_lifecycle);
}

static int video_set_display_observer_owned(carplay_video_display_cb_t callback,
                                            void *context)
{
    if (s_sink.running || s_sink.thread_running || s_sink.lock != NULL)
        return RTCP_ESTATE;
    s_display_observer = callback;
    s_display_context = context;
    return RTCP_OK;
}

/* ------------------------------------------------------------------ */
/* NAL queue                                                           */
/* ------------------------------------------------------------------ */

static const uint8_t *find_start_code(const uint8_t *cursor, const uint8_t *end)
{
    while ((size_t)(end - cursor) >= 3U)
    {
        if (cursor[0] == 0x00 && cursor[1] == 0x00 && cursor[2] == 0x01)
        {
            return cursor;
        }
        ++cursor;
    }
    return NULL;
}

static bool is_idr_start(const uint8_t *header, size_t available)
{
    /* first_mb_in_slice == 0 has a leading one in its Exp-Golomb code. */
    return available >= 2U && (header[0] & 31U) == 5U && (header[1] & 0x80U);
}

static bool unit_starts_idr(const uint8_t *data, size_t len)
{
    const uint8_t *end = data + len;
    const uint8_t *start = find_start_code(data, end);
    while (start != NULL && (size_t)(end - start) > 3U)
    {
        const uint8_t *header = start + 3U;
        unsigned type = header[0] & 31U;
        if (type >= 1U && type <= 5U)
            return is_idr_start(header, (size_t)(end - header));
        start = find_start_code(header, end);
    }
    return false;
}

/* The producer owns queue admission under the queue mutex. Keep only the
 * newest queued SPS/PPS when abandoning stale pictures at an overrun. */
static unsigned start_queue_recovery(video_sink_t *sink, bool incoming_config, uint8_t **reclaim)
{
    nal_entry_t config = { 0 };
    unsigned discarded = 0;
    uint32_t now = sink_now_ms();
    rt_base_t level = rt_hw_interrupt_disable();
    ++sink->window.overflows;
    sink->window.overflow_depth = (sink->head + SINK_QUEUE_LENGTH - sink->tail) % SINK_QUEUE_LENGTH;
    update_max(&sink->window.peak, sink->window.overflow_depth);
    sink->window.overflow_oldest_ms = now - sink->queue[sink->tail].queued_ms;
    sink->window.overflow_packet_waits = sink->window.packet_waits;
    sink->window.overflow_frame_starves = sink->window.frame_starves;
    rt_hw_interrupt_enable(level);
    while (sink->tail != sink->head)
    {
        nal_entry_t *entry = &sink->queue[sink->tail];
        if (entry->configure && !incoming_config)
        {
            if (config.data != NULL)
            {
                reclaim[discarded++] = config.data;
            }
            config = *entry;
        }
        else
        {
            reclaim[discarded++] = entry->data;
        }
        sink->tail = (sink->tail + 1U) % SINK_QUEUE_LENGTH;
    }
    ++sink->loss_generation;
    ++sink->queue_overflows;
    sink->queue_overflow_ms = now;
    sink->queue_wait_idr = true;
    sink->queue_discarded += discarded;
    sink->dropped_nals += discarded;
    if (config.data != NULL)
    {
        config.loss_generation = sink->loss_generation;
        sink->queue[sink->head] = config;
        sink->head = (sink->head + 1U) % SINK_QUEUE_LENGTH;
    }
    return discarded;
}

static void drop_nal(video_sink_t *sink, const char *reason)
{
    (void)rt_mutex_take(sink->lock, RT_WAITING_FOREVER);
    ++sink->loss_generation;
    ++sink->dropped_nals;
    (void)rt_mutex_release(sink->lock);
    if (sink->dropped_nals == 1U || sink->dropped_nals % 60U == 0U)
    {
        rt_kprintf("[" TAG "] input dropped reason=%s count=%u\n", reason, sink->dropped_nals);
    }
}

static bool enqueue_nal(video_sink_t *sink, const uint8_t *data, size_t len,
                        uint32_t pts_us, uint32_t arrival_ms, bool configure)
{
    if (len == 0U || len > SINK_MAX_UNIT_BYTES)
    {
        drop_nal(sink, "invalid_size");
        return false;
    }
    bool checked_idr = !configure && sink->queue_wait_idr;
    bool idr = checked_idr && unit_starts_idr(data, len);
    if (checked_idr && !idr)
    {
        uint32_t lock_start = sink_now_us();
        if (rt_mutex_take(sink->lock, RT_WAITING_FOREVER) != RT_EOK)
            return false;
        uint32_t locked = sink_now_us();
        bool filtered = sink->queue_wait_idr;
        if (filtered)
        {
            ++sink->queue_discarded;
            ++sink->dropped_nals;
        }
        rt_base_t level = rt_hw_interrupt_disable();
        sink->window.recovery_filtered += filtered;
        update_max(&sink->window.lock_wait_max_us, locked - lock_start);
        update_max(&sink->window.lock_hold_max_us, sink_now_us() - locked);
        rt_hw_interrupt_enable(level);
        (void)rt_mutex_release(sink->lock);
        if (filtered)
            return false;
    }
    uint32_t alloc_start = sink_now_us();
    uint8_t *copy = malloc(len);
    if (copy == NULL)
    {
        drop_nal(sink, "allocation");
        return false;
    }
    uint32_t copy_start = sink_now_us();
    memcpy(copy, data, len);
    uint32_t lock_start = sink_now_us();

    if (rt_mutex_take(sink->lock, RT_WAITING_FOREVER) != RT_EOK)
    {
        free(copy);
        return false;
    }
    uint32_t locked = sink_now_us();
    uint8_t *reclaim[SINK_QUEUE_LENGTH - 1U];
    bool was_empty = sink->head == sink->tail;
    bool overflow = (sink->head + 1U) % SINK_QUEUE_LENGTH == sink->tail;
    unsigned reclaim_count = overflow ? start_queue_recovery(sink, configure, reclaim) : 0;
    bool waiting = sink->queue_wait_idr && !configure;
    if (waiting && !checked_idr)
        idr = unit_starts_idr(data, len);
    bool accepted = !waiting || idr;
    if (!accepted)
    {
        ++sink->queue_discarded;
        ++sink->dropped_nals;
    }
    else
    {
        if (waiting)
            sink->queue_wait_idr = false;
        sink->queue[sink->head] = (nal_entry_t){
            .data = copy,
            .len = len,
            .pts_us = pts_us,
            .arrival_ms = arrival_ms,
            .configure = configure,
            .overflow_idr = waiting,
            .overflow_ms = sink->queue_overflow_ms,
            .loss_generation = sink->loss_generation,
            .queued_ms = sink_now_ms(),
        };
        sink->head = (sink->head + 1U) % SINK_QUEUE_LENGTH;
    }
    unsigned pending = (sink->head + SINK_QUEUE_LENGTH - sink->tail) % SINK_QUEUE_LENGTH;
    if (pending > sink->queue_peak)
        sink->queue_peak = pending;
    rt_base_t level = rt_hw_interrupt_disable();
    update_max(&sink->window.peak, pending);
    update_max(&sink->window.alloc_max_us, copy_start - alloc_start);
    update_max(&sink->window.copy_max_us, lock_start - copy_start);
    update_max(&sink->window.lock_wait_max_us, locked - lock_start);
    update_max(&sink->window.lock_hold_max_us, sink_now_us() - locked);
    rt_hw_interrupt_enable(level);
    (void)rt_mutex_release(sink->lock);
    for (unsigned index = 0; index < reclaim_count; ++index)
        free(reclaim[index]);
    if (!accepted)
        free(copy);
    if (overflow)
        rt_kprintf("[" TAG "] input dropped reason=queue_full; prioritizing IDR\n");
    return accepted && was_empty;
}

static bool dequeue_nal(video_sink_t *sink, nal_entry_t *out)
{
    if (rt_mutex_take(sink->lock, RT_WAITING_FOREVER) != RT_EOK)
        return false;
    bool available = sink->tail != sink->head;
    if (available)
    {
        *out = sink->queue[sink->tail];
        sink->tail = (sink->tail + 1U) % SINK_QUEUE_LENGTH;
        uint32_t wait = sink_now_ms() - out->queued_ms;
        ++sink->dequeued;
        sink->queue_wait_ms += wait;
        if (wait > sink->queue_wait_max_ms)
            sink->queue_wait_max_ms = wait;
        rt_base_t level = rt_hw_interrupt_disable();
        update_max(&sink->window.queue_max_ms, wait);
        observe_latency(sink->queue_hist, ms_to_us(wait));
        rt_hw_interrupt_enable(level);
    }
    (void)rt_mutex_release(sink->lock);
    return available;
}

static void video_frame_owned(bool configuration, const uint8_t *annex_b,
                              size_t annex_b_len, uint64_t sender_time_ns,
                              uint64_t arrival_time_us)
{
    video_sink_t *sink = &s_sink;
    if (!sink->running || annex_b == NULL || annex_b_len == 0U)
        return;

    uint32_t pts_us = (uint32_t)(sender_time_ns / 1000U);
    uint32_t arrival_ms = (uint32_t)(arrival_time_us / 1000U);
    /* Keep SPS/PPS atomic: resetting for each NAL destroys the SPS before
     * decoding its PPS. MPP walks Annex-B start codes inside this packet. */
    if (configuration)
    {
        if (enqueue_nal(sink, annex_b, annex_b_len, pts_us, arrival_ms, true))
            (void)rt_sem_release(sink->wake);
        return;
    }
    const uint8_t *end = annex_b + annex_b_len;
    const uint8_t *cursor = find_start_code(annex_b, end);
    if (cursor == NULL)
        return; /* not Annex-B; nothing to feed the decoder */

    if (enqueue_nal(sink, cursor, (size_t)(end - cursor), pts_us, arrival_ms, false))
        (void)rt_sem_release(sink->wake);
}

/* ------------------------------------------------------------------ */
/* Decode and display                                                  */
/* ------------------------------------------------------------------ */

static bool disable_video_layer(video_sink_t *sink)
{
    struct aicfb_layer_data layer;
    memset(&layer, 0, sizeof(layer));
    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
    layer.enable = 0;
    if (rt_device_control(sink->fb, AICFB_UPDATE_LAYER_CONFIG, &layer) < 0)
        return false;
    return rt_device_control(sink->fb, AICFB_WAIT_FOR_VSYNC, NULL) >= 0;
}

static bool submit_scanout(video_sink_t *sink, const struct mpp_frame *frame,
                           scanout_timing_t *timing)
{
#if !defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    (void)timing;
#endif
    struct aicfb_layer_data layer;
    memset(&layer, 0, sizeof(layer));
    layer.layer_id = AICFB_LAYER_TYPE_VIDEO;
    layer.enable = 1;

    /* The decoder asks the DE to crop H.264 macroblock padding away; the
     * scaler sees the cropped rectangle as its input. */
    int src_w = frame->buf.size.width;
    int src_h = frame->buf.size.height;
    if (frame->buf.crop_en && frame->buf.crop.width > 0 &&
        frame->buf.crop.height > 0)
    {
        src_w = frame->buf.crop.width;
        src_h = frame->buf.crop.height;
    }

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    rtcp_display_rect_t viewport = { .width = sink->panel_width, .height = sink->panel_height }, destination;
    uint32_t generation = 0;
    (void)carplay_fusion_video_target(sink->panel_width, sink->panel_height, &viewport, &generation);
    if (src_w <= 0 || src_h <= 0 || src_w > UINT16_MAX || src_h > UINT16_MAX ||
        !rtcp_display_fit(&viewport, (uint16_t)src_w, (uint16_t)src_h, &destination))
    {
        ++sink->decode_errors;
        return false;
    }
    layer.scale_size.width = destination.width;
    layer.scale_size.height = destination.height;
    layer.pos.x = destination.x;
    layer.pos.y = destination.y;
    timing->destination = destination;
    timing->generation = generation;
#else
    /* Letterbox: keep the source aspect ratio inside the panel.  The DE layer
     * position/scale is in panel pixels, not in the AirPlay display mode, so
     * use the aicfb geometry (identical when the mode matches the panel). */
    int scale_w = sink->panel_width;
    int scale_h = sink->panel_height;
    if (src_w > 0 && src_h > 0)
    {
        int by_width = (int)sink->panel_width * src_h / src_w;
        if (by_width <= (int)sink->panel_height)
        {
            scale_h = by_width;
        }
        else
        {
            scale_w = (int)sink->panel_height * src_w / src_h;
        }
    }
    layer.scale_size.width = scale_w;
    layer.scale_size.height = scale_h;
    layer.pos.x = ((int)sink->panel_width - scale_w) / 2;
    layer.pos.y = ((int)sink->panel_height - scale_h) / 2;
#endif
    memcpy(&layer.buf, &frame->buf, sizeof(layer.buf));

    uint32_t start = sink_now_us();
    int result = rt_device_control(sink->fb, AICFB_UPDATE_LAYER_CONFIG, &layer);
    unsigned elapsed = sink_now_us() - start;
    rt_base_t level = rt_hw_interrupt_disable();
    update_max(&sink->window.submit_max_us, elapsed);
    rt_hw_interrupt_enable(level);
    if (result < 0)
    {
        ++sink->decode_errors;
        return false;
    }
    return true;
}

static void finish_scanout(video_sink_t *sink, const scanout_timing_t *timing,
                           bool new_frame, bool latched)
{
    uint32_t elapsed = sink_now_ms() - timing->start_ms;
    ++sink->present_calls;
    sink->present_ms += elapsed;
    if (elapsed > sink->present_max_ms)
        sink->present_max_ms = elapsed;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (latched)
    {
        sink->fusion_generation = timing->generation;
        carplay_fusion_video_latched(&timing->destination, sink->display_width, sink->display_height,
                                     timing->generation, sink_now_ms(), new_frame);
    }
#else
    (void)new_frame;
    (void)latched;
#endif
}

static bool wait_scanout(video_sink_t *sink)
{
    if (rt_device_control(sink->fb, AICFB_WAIT_FOR_VSYNC, NULL) >= 0)
        return true;
    ++sink->decode_errors;
    return false;
}

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static bool present_timed(video_sink_t *sink, const struct mpp_frame *frame, bool new_frame)
{
    scanout_timing_t timing = { .start_ms = sink_now_ms() };
    bool presented = submit_scanout(sink, frame, &timing) && wait_scanout(sink);
    finish_scanout(sink, &timing, new_frame, presented);
    return presented;
}
#endif

static void commit_scanout(video_sink_t *sink, const struct mpp_frame *frame)
{
    uint32_t arrival_ms = (uint32_t)((uint64_t)frame->pts >> 32U);
    unsigned elapsed_ms = ((uint32_t)rtcp_os_rtthread.now_ms() - arrival_ms) & INT32_MAX;
    if (sink->have_frame)
        (void)mpp_decoder_put_frame(sink->decoder, &sink->held);
    sink->held = *frame;
    sink->have_frame = true;
    rt_base_t level = rt_hw_interrupt_disable();
    ++sink->latched_frames;
    ++sink->window.rx_latch_samples;
    update_max(&sink->window.rx_latch_max_ms, elapsed_ms);
    observe_latency(sink->rx_latch_hist, ms_to_us(elapsed_ms));
    rt_hw_interrupt_enable(level);
    if (sink->queue_latch_pending && !sink->recovery_pending &&
        sink->queue_idr_generation == sink->loss_generation &&
        (uint32_t)((uint32_t)frame->pts - sink->queue_idr_pts_us) < (UINT32_MAX / 2U + 1U))
    {
        uint32_t elapsed = sink_now_ms() - sink->queue_idr_accepted_ms;
        sink->queue_latch_pending = false;
        sink->queue_latch_last_ms = elapsed;
        if (elapsed > sink->queue_latch_max_ms)
            sink->queue_latch_max_ms = elapsed;
        ++sink->queue_latch_samples;
    }
    if (!sink->display_reported)
    {
        sink->display_reported = true;
        if (s_display_observer)
            s_display_observer(true, s_display_context);
    }
}

/* Release only after a confirmed layer-disable fence, including reset/stop. */
static bool retire_scanout(video_sink_t *sink)
{
    if (sink->fb && !disable_video_layer(sink))
    {
        ++sink->decode_errors;
        return false;
    }
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_fusion_video_stopped();
#endif
    if (sink->have_pending_scanout)
    {
        (void)mpp_decoder_put_frame(sink->decoder, &sink->pending_scanout);
        sink->have_pending_scanout = false;
        sink->fence_armed = false;
    }
    if (sink->have_ready_scanout)
    {
        (void)mpp_decoder_put_frame(sink->decoder, &sink->ready_scanout);
        sink->have_ready_scanout = false;
    }
    if (sink->have_frame)
    {
        (void)mpp_decoder_put_frame(sink->decoder, &sink->held);
        sink->have_frame = false;
    }
    return true;
}

static void progress_scanout(video_sink_t *sink)
{
    if (!sink->have_pending_scanout || sink->stop)
        return;
    bool latched;
    if (sink->fence_armed)
    {
        unsigned sequence;
        if (rt_device_control(sink->fb, AICFB_GET_VSYNC_SEQUENCE, &sequence) >= 0)
        {
            latched = sequence != sink->fence_sequence;
            if (!latched && sink_now_ms() - sink->fence_timing.start_ms < SINK_FENCE_TIMEOUT_MS)
                return;
        }
        else
            latched = false;
        sink->fence_armed = false;
        unsigned fence_age = sink_now_us() - sink->fence_timing.fence_start_us;
        rt_base_t level = rt_hw_interrupt_disable();
        update_max(&sink->window.fence_max_us, fence_age);
        sink->window.fence_fallbacks += !latched;
        rt_hw_interrupt_enable(level);
        if (!latched)
            latched = wait_scanout(sink);
    }
    else
    {
        sink->fence_timing.start_ms = sink_now_ms();
        bool submitted = submit_scanout(sink, &sink->pending_scanout, &sink->fence_timing);
        if (submitted)
            sink->last_submit_ms = sink_now_ms();
        /* Sample after submitting. A missed interrupt costs one refresh but
         * only a subsequent interrupt can release the previous scanout. */
        if (submitted && sink->async_scanout &&
            rt_device_control(sink->fb, AICFB_GET_VSYNC_SEQUENCE, &sink->fence_sequence) >= 0)
        {
            sink->fence_timing.fence_start_us = sink_now_us();
            sink->fence_armed = true;
            return;
        }
        if (submitted && sink->async_scanout)
        {
            rt_base_t level = rt_hw_interrupt_disable();
            ++sink->window.fence_fallbacks;
            rt_hw_interrupt_enable(level);
        }
        latched = submitted && wait_scanout(sink);
    }
    finish_scanout(sink, &sink->fence_timing, true, latched);
    if (latched)
    {
        commit_scanout(sink, &sink->pending_scanout);
        sink->have_pending_scanout = false;
    }
}

static void discard_ready_scanout(video_sink_t *sink)
{
    if (sink->have_ready_scanout)
    {
        (void)mpp_decoder_put_frame(sink->decoder, &sink->ready_scanout);
        sink->have_ready_scanout = false;
        sink->ready_deferred = false;
        ++sink->skipped_frames;
        rt_base_t level = rt_hw_interrupt_disable();
        ++sink->window.discarded_outputs;
        rt_hw_interrupt_enable(level);
    }
}

static void update_display_pressure(video_sink_t *sink, unsigned depth, unsigned oldest_ms)
{
    /* Age alone cannot justify holding decoded output when there is room
     * in the queue. Leave pacing as soon as there is headroom again. */
    if (depth >= 12U || (depth >= 10U && oldest_ms >= 120U) ||
        (sink->display_pressure == 2U && depth >= 10U))
        sink->display_pressure = 2;
    else if (depth >= 6U || (sink->display_pressure && depth > 4U))
        sink->display_pressure = 1;
    else
        sink->display_pressure = 0;
    rt_base_t level = rt_hw_interrupt_disable();
    update_max(&sink->window.pressure_peak, sink->display_pressure);
    rt_hw_interrupt_enable(level);
}

static unsigned drain_frames(video_sink_t *sink)
{
    progress_scanout(sink);
    if (sink->recovery_pending)
    {
        discard_ready_scanout(sink);
        return 0;
    }
    if (sink->stop || (sink->have_pending_scanout && !sink->fence_armed))
        return 0;

    unsigned drained = 0U;
    while (drained < 8U && !sink->stop && sink->decoder != NULL)
    {
        struct mpp_frame frame;
        memset(&frame, 0, sizeof(frame));
        int ret = mpp_decoder_get_frame(sink->decoder, &frame);
        if (ret != DEC_OK)
        {
            if (ret != DEC_NO_RENDER_FRAME && ret != DEC_NO_EMPTY_FRAME &&
                ret != DEC_ERR_FM_NOT_CREATE)
            {
                ++sink->decode_errors;
            }
            break;
        }
        /* Catch up using decoded output only: never discard compressed
         * reference NALs. MPP keeps separate decoder/display references.
         * The old scanout stays held until the new layer reaches vsync. */
        discard_ready_scanout(sink);
        sink->ready_scanout = frame;
        sink->have_ready_scanout = true;
        ++drained;
    }
    if (sink->have_ready_scanout && !sink->have_pending_scanout)
    {
        bool recovering = sink->queue_latch_pending && sink->queue_idr_generation == sink->loss_generation;
        if (sink->defer_present && !recovering)
        {
            discard_ready_scanout(sink);
            return drained;
        }
        if (sink->async_scanout && sink->display_pressure == 2U && sink->have_frame && !recovering &&
            sink_now_ms() - sink->last_submit_ms < 20U)
        {
            if (!sink->ready_deferred)
            {
                sink->ready_deferred = true;
                rt_base_t level = rt_hw_interrupt_disable();
                ++sink->window.deferred_outputs;
                rt_hw_interrupt_enable(level);
            }
            return drained;
        }
        sink->pending_scanout = sink->ready_scanout;
        sink->have_ready_scanout = false;
        sink->ready_deferred = false;
        sink->have_pending_scanout = true;
        ++sink->frame_count;
        progress_scanout(sink);
    }
    return drained;
}

static void pump_decoder(video_sink_t *sink);

static int submit_nal(video_sink_t *sink, const nal_entry_t *nal)
{
    if (sink->decoder == NULL)
        return -1;

    struct mpp_packet packet;
    memset(&packet, 0, sizeof(packet));
    int ret = -1;
    bool waited = false;
    uint32_t wait_start = 0;
    for (unsigned attempt = 0; attempt < SINK_MAX_PACKET_RETRIES; ++attempt)
    {
        ret = mpp_decoder_get_packet(sink->decoder, &packet, (int)nal->len);
        if (ret == DEC_OK)
            break;
        if (sink->stop)
            break;
        if (ret != DEC_NO_EMPTY_PACKET && ret != -1)
        {
            ++sink->decode_errors;
            break;
        }
        if (!waited)
        {
            waited = true;
            wait_start = sink_now_us();
            rt_base_t level = rt_hw_interrupt_disable();
            ++sink->window.packet_waits;
            rt_hw_interrupt_enable(level);
        }
        /* Only this thread decodes. Waiting without pumping cannot release
         * either of the two MPP packet slots. */
        pump_decoder(sink);
        if (sink->recovery_pending)
            break;
        rt_thread_mdelay(1);
    }
    if (waited)
    {
        unsigned elapsed = sink_now_us() - wait_start;
        rt_base_t level = rt_hw_interrupt_disable();
        update_max(&sink->window.packet_wait_max_us, elapsed);
        rt_hw_interrupt_enable(level);
    }
    if (ret != DEC_OK)
    {
        if (!sink->stop && !sink->recovery_pending && (ret == DEC_NO_EMPTY_PACKET || ret == -1))
            ++sink->decode_errors;
        return -1;
    }
    memcpy(packet.data, nal->data, nal->len);
    packet.size = (int)nal->len;
    /* MPP copies PTS to the matching frame, including reordered output.
     * Keep sender PTS in the low word for IDR recovery comparisons. */
    packet.pts = (long long)(((uint64_t)(nal->arrival_ms & INT32_MAX) << 32U) | nal->pts_us);
    ret = mpp_decoder_put_packet(sink->decoder, &packet);
    if (ret != 0)
    {
        ++sink->decode_errors;
        return -1;
    }
    ++sink->nal_count;
    return 0;
}

static void pump_decoder(video_sink_t *sink)
{
    if (sink->decoder == NULL)
        return;
    for (int i = 0; i < 32 && !sink->stop; ++i)
    {
        uint32_t decode_start = sink_now_us();
        int ret = mpp_decoder_decode(sink->decoder);
        uint32_t decode_us = sink_now_us() - decode_start;
        uint32_t decode_elapsed = (decode_us + 500U) / 1000U;
        /* Exclude empty probes from per-call averages. Values are elapsed
         * wall time including scheduler/VE waits, not isolated CPU time. */
        if (ret != DEC_NO_READY_PACKET)
        {
            ++sink->decode_calls;
            sink->decode_ms += decode_elapsed;
            if (decode_elapsed > sink->decode_max_ms)
                sink->decode_max_ms = decode_elapsed;
            rt_base_t level = rt_hw_interrupt_disable();
            update_max(&sink->window.decode_max_us, decode_us);
            observe_latency(sink->decode_hist, decode_us);
            sink->window.frame_starves += ret == DEC_NO_EMPTY_FRAME;
            rt_hw_interrupt_enable(level);
        }
        if (ret == DEC_NO_READY_PACKET)
            break;
        if (ret == DEC_OK)
            continue;
        if (ret == DEC_NO_EMPTY_FRAME)
        {
            if (drain_frames(sink) == 0U)
            {
                if (!sink->ready_deferred)
                    break;
                discard_ready_scanout(sink);
            }
            continue;
        }
        if (ret != 0)
        {
            if (ret != DEC_NO_RENDER_FRAME)
            {
                ++sink->decode_errors;
                if (sink->decode_errors <= 3U)
                {
                    rt_kprintf("[" TAG "] decode error %d; waiting for IDR\n", ret);
                }
                sink->wait_idr = true;
                sink->recovery_pending = true;
                sink->reset_pending = true;
            }
            break;
        }
    }
    (void)drain_frames(sink);
}

/* ------------------------------------------------------------------ */
/* Decoder lifetime                                                    */
/* ------------------------------------------------------------------ */

static int create_decoder(video_sink_t *sink)
{
    sink->decoder = mpp_decoder_create(MPP_CODEC_VIDEO_DECODER_H264);
    if (sink->decoder == NULL)
    {
        rt_kprintf("[" TAG "] cannot create the H.264 decoder\n");
        return RTCP_ENOMEM;
    }
    struct decode_config config;
    memset(&config, 0, sizeof(config));
    config.pix_fmt = MPP_FMT_NV12;
    config.bitstream_buffer_size = SINK_BITSTREAM_BYTES;
    config.packet_count = SINK_PACKET_COUNT;
    /* Reserve space for the submitted and newest unsent display buffers. */
    config.extra_frame_num = SINK_EXTRA_FRAMES + (sink->async_scanout ? 2 : 0);
    int ret = mpp_decoder_init(sink->decoder, &config);
    if (ret != 0)
    {
        rt_kprintf("[" TAG "] decoder init failed: %d\n", ret);
        mpp_decoder_destory(sink->decoder);
        sink->decoder = NULL;
        return RTCP_EIO;
    }
    return RTCP_OK;
}

/* Called from the sink thread only. */
static bool recreate_decoder(video_sink_t *sink)
{
    if (!retire_scanout(sink))
        return false;
    if (sink->decoder != NULL)
        mpp_decoder_destory(sink->decoder);
    sink->decoder = NULL;
    ++sink->decoder_resets;
    if (create_decoder(sink) != RTCP_OK)
    {
        ++sink->decode_errors;
        rt_kprintf("[" TAG "] decoder reset failed\n");
    }
    else
    {
        rt_kprintf("[" TAG "] decoder reset for a new stream\n");
    }
    return sink->decoder != NULL;
}

/* Worker-owned state: no producer may mutate decoder/configuration lifetime. */
static bool accept_configuration(video_sink_t *sink, const nal_entry_t *nal)
{
    if (sink->configuration_len == nal->len && sink->configuration != NULL &&
        memcmp(sink->configuration, nal->data, nal->len) == 0)
    {
        ++sink->repeated_configs;
        if (sink->repeated_configs == 1U || sink->repeated_configs % 60U == 0U)
            rt_kprintf("[" TAG "] repeated configuration ignored, bytes=%u count=%u\n",
                       (unsigned)nal->len, sink->repeated_configs);
        return false;
    }
    uint8_t *copy = malloc(nal->len);
    if (copy == NULL)
    {
        ++sink->decode_errors;
        sink->wait_idr = true;
        sink->recovery_pending = true;
        return false;
    }
    memcpy(copy, nal->data, nal->len);
    bool changed = sink->configuration != NULL || sink->frame_count != 0U;
    free(sink->configuration);
    sink->configuration = copy;
    sink->configuration_len = nal->len;
    sink->wait_idr = true;
    if (changed)
    {
        /* Delay recreation until IDR, discarding dependent pictures meanwhile. */
        sink->recovery_pending = true;
        sink->reset_pending = true;
        rt_kprintf("[" TAG "] configuration changed; waiting for IDR\n");
        return false;
    }
    return true;
}

static bool prepare_picture(video_sink_t *sink, const nal_entry_t *nal)
{
    if (!sink->wait_idr)
        return true;
    const uint8_t *start = find_start_code(nal->data, nal->data + nal->len);
    size_t available = start != NULL ? (size_t)(nal->data + nal->len - start) : 0U;
    if (available <= 3U || !is_idr_start(start + 3U, available - 3U))
    {
        ++sink->waiting_nals;
        return false;
    }
    if (sink->recovery_pending)
    {
        /* A newer queue gap already invalidated this recovery point. */
        if (nal->loss_generation != sink->loss_generation)
            return false;
        if (sink->configuration == NULL)
        {
            ++sink->waiting_nals;
            return false;
        }
        if (sink->reset_pending)
        {
            if (!recreate_decoder(sink))
                return false;
            nal_entry_t config = { .data = sink->configuration,
                                   .len = sink->configuration_len };
            /* Clear before pumping so any configuration failure re-arms recovery. */
            sink->recovery_pending = false;
            if (submit_nal(sink, &config) != 0)
            {
                sink->recovery_pending = true;
                return false;
            }
            pump_decoder(sink);
            if (sink->recovery_pending)
                return false;
            sink->reset_pending = false;
        }
        /* A queue gap drops whole access units. The intact IDR refreshes
         * MPP's reference pictures without retiring the current scanout. */
        sink->recovery_pending = false;
    }
    sink->wait_idr = false;
    rt_kprintf("[" TAG "] IDR accepted; skipped dependent NALs=%u\n",
               sink->waiting_nals);
    return true;
}

static void decode_unit(video_sink_t *sink, const nal_entry_t *unit)
{
    const uint8_t *cursor = unit->data;
    const uint8_t *end = cursor + unit->len;
    do
    {
        const uint8_t *next = NULL;
        if (!unit->configure && (size_t)(end - cursor) > 3U)
            next = find_start_code(cursor + 3U, end);
        nal_entry_t nal = *unit;
        nal.data = (uint8_t *)cursor;
        nal.len = (size_t)((next != NULL ? next : end) - cursor);
        bool waiting = sink->wait_idr;
        bool feed = nal.configure ? accept_configuration(sink, &nal)
                                  : prepare_picture(sink, &nal);
        if (feed)
        {
            if (submit_nal(sink, &nal) != 0)
            {
                sink->wait_idr = true;
                sink->recovery_pending = true;
                sink->reset_pending = true;
            }
            else if (unit->overflow_idr && waiting && !sink->wait_idr &&
                     unit->loss_generation == sink->loss_generation)
            {
                uint32_t now = sink_now_ms();
                uint32_t elapsed = now - unit->overflow_ms;
                sink->queue_idr_last_ms = elapsed;
                if (elapsed > sink->queue_idr_max_ms)
                    sink->queue_idr_max_ms = elapsed;
                ++sink->queue_idr_samples;
                sink->queue_idr_accepted_ms = now;
                sink->queue_idr_pts_us = unit->pts_us;
                sink->queue_idr_generation = unit->loss_generation;
                sink->queue_latch_pending = true;
            }
        }
        if (!sink->recovery_pending)
            pump_decoder(sink);
        cursor = next;
    } while (cursor != NULL && !sink->stop);
}

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static void apply_display_mode(video_sink_t *sink)
{
    /* A static phone image must also respond to mode changes. Re-presenting
     * the owned scanout does not count as a newly received video frame. */
    rtcp_display_rect_t viewport;
    uint32_t generation;
    if (!sink->stop && sink->have_frame && !sink->have_pending_scanout &&
        carplay_fusion_video_target(sink->panel_width, sink->panel_height, &viewport, &generation) &&
        generation != sink->fusion_generation)
        (void)present_timed(sink, &sink->held, false);
}
#endif

static void sink_thread(void *argument)
{
    video_sink_t *sink = argument;
    unsigned backlog_units = 0;
    sink->thread_running = true;
    rt_kprintf("[" TAG "] decoder running\n");

    while (!sink->stop)
    {
        if (rt_sem_take(sink->wake, sink->fence_armed ? 1 : 10) == RT_EOK)
        {
            nal_entry_t nal;
            while (dequeue_nal(sink, &nal))
            {
                if (sink->stop)
                {
                    free(nal.data);
                    break;
                }
                if (nal.loss_generation != sink->consumed_generation)
                {
                    sink->consumed_generation = nal.loss_generation;
                    if (!sink->recovery_pending)
                        rt_kprintf("[" TAG "] input loss; waiting for IDR\n");
                    sink->wait_idr = true;
                    sink->recovery_pending = true;
                    discard_ready_scanout(sink);
                }
                unsigned pending = (sink->head + SINK_QUEUE_LENGTH - sink->tail) % SINK_QUEUE_LENGTH;
                if (sink->async_scanout)
                    update_display_pressure(sink, pending + 1U, sink_now_ms() - nal.queued_ms);
                /* At panel rate, a vsync wait for every output can keep an
                 * existing backlog alive. Skip decoded outputs in bounded
                 * groups while continuing to feed every compressed picture. */
                if (!sink->async_scanout && sink->have_frame && pending >= 3U)
                    sink->defer_present = ++backlog_units % 4U != 0U;
                else
                {
                    backlog_units = 0;
                    sink->defer_present = false;
                }
                decode_unit(sink, &nal);
                free(nal.data);
            }
        }
        sink->defer_present = false;
        if (sink->async_scanout && sink->head == sink->tail)
            update_display_pressure(sink, 0, 0);
        if (!sink->recovery_pending)
            pump_decoder(sink);
        else
            progress_scanout(sink);
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
        apply_display_mode(sink);
#endif
    }

    free(sink->configuration);
    sink->configuration = NULL;
    sink->configuration_len = 0;
    /* The joined lifecycle owner fences the DE before returning scanouts. */
    rt_kprintf("[" TAG "] stopped: %u NALs, %u frames, %u dropped, %u errors, %u resets\n",
               sink->nal_count, sink->frame_count, sink->dropped_nals,
               sink->decode_errors, sink->decoder_resets);
    sink->thread_running = false;
}

/* ------------------------------------------------------------------ */
/* Start / stop                                                        */
/* ------------------------------------------------------------------ */

static int video_start_owned(uint16_t display_width, uint16_t display_height)
{
    video_sink_t *sink = &s_sink;
    if (sink->running || sink->thread_running || sink->lock != NULL)
        return RTCP_ESTATE;
    if (display_width == 0U || display_height == 0U)
        return RTCP_EINVAL;

    memset(sink, 0, sizeof(*sink));
    sink->display_width = display_width;
    sink->display_height = display_height;
    sink->report_ms = sink_now_ms();
    sink->panel_width = display_width;
    sink->panel_height = display_height;

    sink->fb = rt_device_find("aicfb");
    if (sink->fb == NULL)
    {
        rt_kprintf("[" TAG "] aicfb device not found\n");
        return RTCP_ENOENT;
    }

    struct aicfb_screeninfo screen;
    memset(&screen, 0, sizeof(screen));
    if (rt_device_control(sink->fb, AICFB_GET_SCREENINFO, &screen) >= 0 &&
        screen.width != 0U && screen.height != 0U)
    {
        sink->panel_width = (uint16_t)screen.width;
        sink->panel_height = (uint16_t)screen.height;
        rt_kprintf("[" TAG "] panel %ux%u, AirPlay display %ux%u\n",
                   (unsigned)sink->panel_width, (unsigned)sink->panel_height,
                   (unsigned)display_width, (unsigned)display_height);
    }
    else
    {
        rt_kprintf("[" TAG "] no panel info, assuming %ux%u\n",
                   (unsigned)display_width, (unsigned)display_height);
    }

    unsigned vsync_sequence;
    sink->async_scanout = rt_device_control(sink->fb, AICFB_GET_VSYNC_SEQUENCE, &vsync_sequence) >= 0;
    int ret = create_decoder(sink);
    if (ret != RTCP_OK)
        return ret;

    sink->wake = rt_sem_create("cvid", 0, RT_IPC_FLAG_FIFO);
    sink->lock = rt_mutex_create("cvidq", RT_IPC_FLAG_FIFO);
    if (sink->wake == NULL || sink->lock == NULL)
    {
        if (sink->wake != NULL)
            (void)rt_sem_delete(sink->wake);
        if (sink->lock != NULL)
            (void)rt_mutex_delete(sink->lock);
        sink->wake = NULL;
        sink->lock = NULL;
        mpp_decoder_destory(sink->decoder);
        sink->decoder = NULL;
        return RTCP_ENOMEM;
    }

    sink->stop = false;
    sink->running = true;
    sink->thread = rt_thread_create("carplay-vid", sink_thread, sink,
                                    SINK_THREAD_STACK_BYTES,
                                    SINK_THREAD_PRIORITY, SINK_THREAD_TICK);
    if (sink->thread == NULL)
    {
        sink->running = false;
        mpp_decoder_destory(sink->decoder);
        sink->decoder = NULL;
        (void)rt_sem_delete(sink->wake);
        (void)rt_mutex_delete(sink->lock);
        sink->wake = NULL;
        sink->lock = NULL;
        return RTCP_ENOMEM;
    }
    /* Publish worker ownership before startup: stop may run before the new
     * thread is first scheduled and must still wait for its final access. */
    sink->thread_running = true;
    if (rt_thread_startup(sink->thread) != RT_EOK)
    {
        (void)rt_thread_delete(sink->thread);
        sink->thread = NULL;
        sink->thread_running = false;
        sink->running = false;
        mpp_decoder_destory(sink->decoder);
        sink->decoder = NULL;
        (void)rt_sem_delete(sink->wake);
        (void)rt_mutex_delete(sink->lock);
        sink->wake = NULL;
        sink->lock = NULL;
        return RTCP_EIO;
    }
    return RTCP_OK;
}

static int video_stop_owned(void)
{
    video_sink_t *sink = &s_sink;
    if (!sink->running && sink->lock == NULL && !sink->thread_running)
        return RTCP_OK;
    sink->running = false; /* stop accepting new NALs */
    sink->stop = true;
    if (sink->wake != NULL)
        (void)rt_sem_release(sink->wake);
    for (unsigned waited = 0; sink->thread_running && waited < 3000U; waited += 10U)
        rt_thread_mdelay(10);
    if (sink->thread_running)
        return RTCP_ETIMEDOUT;
    if (!retire_scanout(sink))
        return RTCP_EIO;

    while (sink->tail != sink->head)
    {
        free(sink->queue[sink->tail].data);
        sink->tail = (sink->tail + 1U) % SINK_QUEUE_LENGTH;
    }

    if (sink->decoder != NULL)
    {
        mpp_decoder_destory(sink->decoder);
        sink->decoder = NULL;
    }

    if (sink->wake != NULL)
    {
        (void)rt_sem_delete(sink->wake);
        sink->wake = NULL;
    }
    if (sink->lock != NULL)
    {
        (void)rt_mutex_delete(sink->lock);
        sink->lock = NULL;
    }
    sink->thread = NULL;
    if (s_display_observer != NULL)
        s_display_observer(false, s_display_context);
    return RTCP_OK;
}

static bool video_is_running_owned(void)
{
    return s_sink.running;
}

static size_t video_pending_owned(void)
{
    video_sink_t *sink = &s_sink;
    if (!sink->running || sink->lock == NULL)
        return 0U;
    if (rt_mutex_take(sink->lock, RT_WAITING_FOREVER) != RT_EOK)
        return 0U;
    size_t pending =
        (sink->head + SINK_QUEUE_LENGTH - sink->tail) % SINK_QUEUE_LENGTH;
    (void)rt_mutex_release(sink->lock);
    return pending;
}

static void video_stats_owned(unsigned *nals, unsigned *frames,
                              unsigned *dropped, unsigned *errors)
{
    if (nals != NULL)
        *nals = s_sink.nal_count;
    if (frames != NULL)
        *frames = s_sink.frame_count;
    if (dropped != NULL)
        *dropped = s_sink.dropped_nals;
    if (errors != NULL)
        *errors = s_sink.decode_errors;
}

static void video_perf_owned(carplay_video_perf_t *out)
{
    if (!out)
        return;
    unsigned queue_hist[PERF_BIN_COUNT], decode_hist[PERF_BIN_COUNT], rx_latch_hist[PERF_BIN_COUNT];
    rt_base_t level = rt_hw_interrupt_disable();
    *out = (carplay_video_perf_t){
        .frames = s_sink.frame_count,
        .latched = s_sink.latched_frames,
        .packets = s_sink.nal_count,
        .drops = s_sink.dropped_nals,
        .errors = s_sink.decode_errors,
        .pending = (s_sink.head + SINK_QUEUE_LENGTH - s_sink.tail) % SINK_QUEUE_LENGTH,
        .queue_peak = s_sink.queue_peak,
        .dequeued = s_sink.dequeued,
        .decode_calls = s_sink.decode_calls,
        .present_calls = s_sink.present_calls,
        .queue_ms = s_sink.queue_wait_ms,
        .decode_ms = s_sink.decode_ms,
        .present_ms = s_sink.present_ms,
        .window = s_sink.window,
    };
    memcpy(queue_hist, s_sink.queue_hist, sizeof(queue_hist));
    memcpy(decode_hist, s_sink.decode_hist, sizeof(decode_hist));
    memcpy(rx_latch_hist, s_sink.rx_latch_hist, sizeof(rx_latch_hist));
    memset(&s_sink.window, 0, sizeof(s_sink.window));
    memset(s_sink.queue_hist, 0, sizeof(s_sink.queue_hist));
    memset(s_sink.decode_hist, 0, sizeof(s_sink.decode_hist));
    memset(s_sink.rx_latch_hist, 0, sizeof(s_sink.rx_latch_hist));
    s_sink.window.peak = out->pending;
    rt_hw_interrupt_enable(level);
    out->window.queue_p95_ms = latency_p95(queue_hist, ms_to_us(out->window.queue_max_ms)) / 1000U;
    out->window.decode_p95_us = latency_p95(decode_hist, out->window.decode_max_us);
    out->window.rx_latch_p95_ms = latency_p95(rx_latch_hist, ms_to_us(out->window.rx_latch_max_ms)) / 1000U;
}

/* Public calls retain resources through their final queue/wake access. */
int carplay_video_sink_set_display_observer(carplay_video_display_cb_t callback,
                                            void *context)
{
    if (atomic_flag_test_and_set(&s_video_lifecycle))
        return RTCP_ESTATE;
    video_retire_readers();
    int result = video_set_display_observer_owned(callback, context);
    video_lifecycle_leave();
    return result;
}

int carplay_video_sink_start(uint16_t width, uint16_t height)
{
    if (atomic_flag_test_and_set(&s_video_lifecycle))
        return RTCP_ESTATE;
    video_retire_readers();
    int result = video_start_owned(width, height);
    video_lifecycle_leave();
    return result;
}

int carplay_video_sink_stop(void)
{
    if (atomic_flag_test_and_set(&s_video_lifecycle))
        return RTCP_ESTATE;
    if (s_sink.thread_running && s_sink.thread == rt_thread_self())
    {
        atomic_flag_clear(&s_video_lifecycle);
        return RTCP_ESTATE;
    }
    video_retire_readers();
    int result = video_stop_owned();
    video_lifecycle_leave();
    return result;
}

void carplay_video_sink_frame(bool configuration, const uint8_t *annex_b,
                              size_t length, uint64_t timestamp, void *context)
{
    (void)context;
    carplay_video_sink_frame_at(configuration, annex_b, length, timestamp, rtcp_os_rtthread.now_us());
}

void carplay_video_sink_frame_at(bool configuration, const uint8_t *annex_b,
                                 size_t length, uint64_t timestamp, uint64_t arrival_time_us)
{
    if (!video_read_enter())
        return;
    video_frame_owned(configuration, annex_b, length, timestamp, arrival_time_us);
    video_read_leave();
}

bool carplay_video_sink_is_running(void)
{
    if (!video_read_enter())
        return false;
    bool result = video_is_running_owned();
    video_read_leave();
    return result;
}

bool carplay_video_sink_has_presented(void)
{
    if (!video_read_enter())
        return false;
    bool result = s_sink.running && s_sink.display_reported;
    video_read_leave();
    return result;
}

bool carplay_video_sink_needs_keyframe(void)
{
    if (!video_read_enter())
        return false;
    bool needed = s_sink.running && !s_sink.stop &&
                  (s_sink.recovery_pending || s_sink.loss_generation != s_sink.consumed_generation);
    video_read_leave();
    return needed;
}

size_t carplay_video_sink_pending(void)
{
    if (!video_read_enter())
        return 0U;
    size_t result = video_pending_owned();
    video_read_leave();
    return result;
}

void carplay_video_sink_stats(unsigned *nals, unsigned *frames,
                              unsigned *dropped, unsigned *errors)
{
    if (nals)
        *nals = 0U;
    if (frames)
        *frames = 0U;
    if (dropped)
        *dropped = 0U;
    if (errors)
        *errors = 0U;
    if (!video_read_enter())
        return;
    video_stats_owned(nals, frames, dropped, errors);
    video_read_leave();
}

void carplay_video_sink_perf(carplay_video_perf_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!video_read_enter())
        return;
    video_perf_owned(out);
    video_read_leave();
}

/* ------------------------------------------------------------------ */
/* Bring-up shell command: decode an Annex-B file without a controller */
/* ------------------------------------------------------------------ */

#define VIDEO_TEST_READ_BYTES  (64U * 1024U)
#define VIDEO_TEST_CARRY_BYTES (SINK_MAX_UNIT_BYTES + VIDEO_TEST_READ_BYTES)

static size_t find_last_start_code(const uint8_t *data, size_t len)
{
    if (len < 3U)
        return SIZE_MAX;
    size_t i = len - 3U;
    for (;;)
    {
        if (data[i] == 0x00 && data[i + 1U] == 0x00 && data[i + 2U] == 0x01)
        {
            return i;
        }
        if (i == 0U)
            return SIZE_MAX;
        --i;
    }
}

static void wait_for_decode_room(void)
{
    unsigned guard = 2000U;
    while (carplay_video_sink_is_running() &&
           carplay_video_sink_pending() >= 8U && guard-- != 0U)
    {
        rt_thread_mdelay(5);
    }
}

static int play_h264_file(const char *path)
{
    bool started_here = false;
    if (!carplay_video_sink_is_running())
    {
        int result = carplay_video_sink_start(1280U, 480U);
        if (result != RTCP_OK)
        {
            rt_kprintf("[" TAG "] cannot start the sink: %d\n", result);
            return result;
        }
        started_here = true;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        rt_kprintf("[" TAG "] cannot open %s\n", path);
        if (started_here)
            carplay_video_sink_stop();
        return RTCP_ENOENT;
    }
    uint8_t *carry = malloc(VIDEO_TEST_CARRY_BYTES);
    if (carry == NULL)
    {
        fclose(file);
        if (started_here)
            carplay_video_sink_stop();
        return RTCP_ENOMEM;
    }

    size_t carry_len = 0U;
    size_t fed_bytes = 0U;
    unsigned fed_batches = 0U;
    bool eof = false;
    while (carplay_video_sink_is_running())
    {
        if (!eof && carry_len < VIDEO_TEST_CARRY_BYTES)
        {
            size_t got = fread(carry + carry_len, 1U,
                               VIDEO_TEST_CARRY_BYTES - carry_len, file);
            if (got == 0U)
                eof = true;
            carry_len += got;
        }

        const uint8_t *start = find_start_code(carry, carry + carry_len);
        if (start == NULL)
        {
            if (eof)
                break;
            /* Keep a possible split start code, drop the rest. */
            size_t keep = carry_len > 2U ? 2U : carry_len;
            memmove(carry, carry + carry_len - keep, keep);
            carry_len = keep;
            continue;
        }
        if (start != carry)
        {
            size_t drop = (size_t)(start - carry);
            memmove(carry, start, carry_len - drop);
            carry_len -= drop;
        }

        size_t next = find_last_start_code(carry + 3U, carry_len - 3U);
        if (next == SIZE_MAX)
        {
            if (!eof && carry_len < VIDEO_TEST_CARRY_BYTES)
                continue;
            /* One NAL fills the buffer (or ends the file): submit it whole. */
            next = carry_len;
        }
        else
        {
            next += 3U;
        }
        if (next == 0U)
            continue;

        carplay_video_sink_frame(false, carry, next, 0U, NULL);
        fed_bytes += next;
        ++fed_batches;
        memmove(carry, carry + next, carry_len - next);
        carry_len -= next;
        if (eof && carry_len < 3U)
            break;
        wait_for_decode_room();
    }

    free(carry);
    fclose(file);
    unsigned nals = 0U, frames = 0U, dropped = 0U, errors = 0U;
    carplay_video_sink_stats(&nals, &frames, &dropped, &errors);
    rt_kprintf("[" TAG "] played %s: %u batches, %u bytes, decoder %u NALs/%u frames, dropped %u, errors %u\n",
               path, fed_batches, (unsigned)fed_bytes, nals, frames, dropped,
               errors);
    return RTCP_OK;
}

static int cmd_carplay_video_owned(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("usage: carplay_video start [width height] | stop | status | play <annexb-file>\n");
        return 0;
    }
#ifdef RTCARPLAY_DEMO_LAUNCHER
    if (strcmp(argv[1], "status") != 0)
    {
        rt_kprintf("[" TAG "] launcher owns video; manual mutation unavailable\n");
        return RTCP_ESTATE;
    }
#endif
    if (strcmp(argv[1], "start") == 0)
    {
        unsigned width = argc >= 4 ? (unsigned)atoi(argv[2]) : 1280U;
        unsigned height = argc >= 4 ? (unsigned)atoi(argv[3]) : 480U;
        int result = carplay_video_sink_start((uint16_t)width, (uint16_t)height);
        rt_kprintf("[" TAG "] start %ux%u: %d\n", width, height, result);
        return 0;
    }
    if (strcmp(argv[1], "stop") == 0)
    {
        int result = carplay_video_sink_stop();
        rt_kprintf("[" TAG "] stop result=%d\n", result);
        return result;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        if (!video_read_enter())
            return RTCP_ESTATE;
        unsigned nals = 0U, frames = 0U, dropped = 0U, errors = 0U;
        video_stats_owned(&nals, &frames, &dropped, &errors);
        rt_kprintf("[" TAG "] %s, pending %u, NALs %u, frames %u, dropped %u, errors %u\n",
                   video_is_running_owned() ? "running" : "stopped",
                   (unsigned)video_pending_owned(), nals, frames,
                   dropped, errors);
        video_sink_t *sink = &s_sink;
        unsigned latched = sink->latched_frames;
        uint32_t now = sink_now_ms();
        uint32_t elapsed = now - sink->report_ms;
        unsigned fps10 = elapsed != 0U ? (unsigned)((uint64_t)(latched - sink->report_frames) * 10000U / elapsed) : 0U;
        rt_kprintf("[" TAG "] interval=%ums presented_fps=%u.%u latched=%u\n",
                   (unsigned)elapsed, fps10 / 10U, fps10 % 10U, latched);
        rt_kprintf("[" TAG "] input NALs=%u queue_peak=%u resets=%u skipped=%u\n",
                   nals - sink->report_nals, sink->queue_peak, sink->decoder_resets,
                   sink->skipped_frames);
        rt_kprintf("[" TAG "] recovery: repeated_configs=%u waiting_idr=%u skipped_nals=%u\n",
                   sink->repeated_configs, sink->wait_idr ? 1U : 0U,
                   sink->waiting_nals);
        rt_kprintf("[" TAG "] overflows=%u discarded_units=%u async_vsync=%u\n",
                   sink->queue_overflows, sink->queue_discarded, sink->async_scanout ? 1U : 0U);
        rt_kprintf("[" TAG "] overflow_to_idr_ms last/max=%u/%u samples=%u\n",
                   sink->queue_idr_last_ms, sink->queue_idr_max_ms, sink->queue_idr_samples);
        rt_kprintf("[" TAG "] idr_to_latch_ms last/max=%u/%u samples=%u\n",
                   sink->queue_latch_last_ms, sink->queue_latch_max_ms, sink->queue_latch_samples);
        rt_kprintf("[" TAG "] elapsed_ms avg/max: queue=%u/%u decode=%u/%u present=%u/%u\n",
                   sink->dequeued ? (unsigned)(sink->queue_wait_ms / sink->dequeued) : 0U,
                   (unsigned)sink->queue_wait_max_ms,
                   sink->decode_calls ? (unsigned)(sink->decode_ms / sink->decode_calls) : 0U,
                   (unsigned)sink->decode_max_ms,
                   sink->present_calls ? (unsigned)(sink->present_ms / sink->present_calls) : 0U,
                   (unsigned)sink->present_max_ms);
        sink->report_ms = now;
        sink->report_frames = latched;
        sink->report_nals = nals;
        video_read_leave();
        return 0;
    }
    if (strcmp(argv[1], "play") == 0)
    {
        if (argc < 3)
        {
            rt_kprintf("usage: carplay_video play <annexb-file>\n");
            return 0;
        }
        (void)play_h264_file(argv[2]);
        return 0;
    }
    rt_kprintf("unknown carplay_video subcommand: %s\n", argv[1]);
    return 0;
}

static int cmd_carplay_video(int argc, char **argv)
{
    if (atomic_flag_test_and_set(&s_video_shell))
        return RTCP_ESTATE;
    int result = cmd_carplay_video_owned(argc, argv);
    atomic_flag_clear(&s_video_shell);
    return result;
}

MSH_CMD_EXPORT_ALIAS(cmd_carplay_video, carplay_video,
                     Test the MPP video path : start / stop / status / play file);

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "audio_input_lubanlite.h"
#include "rtcp_audio_capture_lubanlite.h"
#include "rtcp/rtcp.h"
#include "audio_voice.h"
#include "mbedtls/platform_util.h"
#include <rtthread.h>
#include "rtcp_rtthread.h"
#include <rtdevice.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef struct
{
    airplay_audio_input_config_t config;
    atomic_bool stop, alive, mute;
    atomic_uint packets, errors, samples;
    rt_thread_t thread;
} input_t;
static input_t input;
static atomic_flag input_lifecycle = ATOMIC_FLAG_INIT;
static atomic_uint input_stop_generation;
bool carplay_audio_input_available(void)
{
    rt_device_t d = rt_device_find("dmic0");
    return d && !d->ref_count && !atomic_load(&input.alive);
}
#if defined(RTCARPLAY_MIC_AEC)
#include "audio_echo_reference.h"
static carplay_echo_history_t references;
static rt_mutex_t reference_lock;
static atomic_bool aec_requested = true;
static atomic_uint aec_delay_ms = RTCARPLAY_MIC_AEC_DELAY_MS;
static atomic_bool aec_auto_delay = true;
static atomic_uint delay_updates;
static atomic_uint reference_hits, reference_misses, reference_drops, process_max_us;
static atomic_uint capture_gaps, mic_peak, mic_clipped;
static uint64_t reference_next;
static uint64_t input_now_us(void)
{
    return rtcp_os_rtthread.now_us();
}
void carplay_audio_input_reference_queued(const int16_t *stereo, size_t frames, uint32_t queued_bytes)
{
    if (!stereo || frames != 480 || !atomic_load(&input.alive) || !reference_lock)
        return;
    if (rt_mutex_take(reference_lock, 0) != RT_EOK)
    {
        ++reference_drops;
        return;
    }
    uint64_t now = input_now_us();
    uint64_t queued_us = (uint64_t)queued_bytes * 1000000 / (48000 * 4);
    uint64_t start = now + queued_us;
    start = start >= 10000 ? start - 10000 : 0;
    /* Queue snapshots and millisecond ticks jitter around the sample clock.
     * Preserve contiguous chunks unless the device suffered a real gap. */
    uint64_t difference = start > reference_next ? start - reference_next : reference_next - start;
    if (reference_next && difference < 20000)
        start = reference_next;
    reference_next = start + 10000;
    carplay_echo_push(&references, stereo, start);
    (void)rt_mutex_release(reference_lock);
}
static bool reference_frame(int16_t *out, uint64_t capture_end)
{
    uint64_t delay = 10000 + (uint64_t)atomic_load(&aec_delay_ms) * 1000;
    if (!capture_end || capture_end < delay ||
        rt_mutex_take(reference_lock, rt_tick_from_millisecond(2)) != RT_EOK)
        return false;
    bool found = carplay_echo_read(&references, capture_end - delay, out);
    (void)rt_mutex_release(reference_lock);
    return found;
}
#else
void carplay_audio_input_reference_queued(const int16_t *stereo, size_t frames, uint32_t bytes)
{
    (void)stereo;
    (void)frames;
    (void)bytes;
}
#endif
void carplay_audio_input_reference(const int16_t *stereo, size_t frames)
{
    carplay_audio_input_reference_queued(stereo, frames, 480 * 4);
}
static void input_thread(void *arg)
{
    input_t *in = arg;
    rtcp_audio_capture_t *capture = NULL;
    airplay_audio_sender_t *sender = NULL;
    rtcp_audio_voice_t *voice = NULL;
    int fd = -1;
    int rc = RTCP_OK;
    if (atomic_load(&in->stop))
        goto done;
    rc = rtcp_audio_capture_open(&capture);
    if (rc != RTCP_OK)
        goto done;
    if (atomic_load(&in->stop))
        goto done;
    rc = airplay_audio_sender_create(&in->config.sender, &sender);
    if (rc != RTCP_OK)
        goto done;
    if (atomic_load(&in->stop))
        goto done;
    fd = in->config.net->udp_open(in->config.peer.family, 0, false);
    if (fd < 0)
    {
        rc = RTCP_EIO;
        goto done;
    }
    if (atomic_load(&in->stop))
        goto done;
#if defined(RTCP_AUDIO_CODECS) || defined(LPKG_RT_CARPLAY_AUDIO_CODECS)
#if defined(RTCARPLAY_MIC_AEC)
    bool active_aec = atomic_load(&aec_requested);
    unsigned active_delay = atomic_load(&aec_delay_ms);
    /* Do not amplify uncancelled far-end residue with a second AGC. */
    voice = rtcp_audio_voice_create(true, !active_aec, active_aec);
#else
    voice = rtcp_audio_voice_create(true, true, false);
#endif
#else
    voice = rtcp_audio_voice_create(false, true, false);
#endif
    if (!voice)
    {
        rc = RTCP_ENOMEM;
        goto done;
    }
    unsigned target = in->config.sender.format.sample_rate;
    size_t packet_frames = airplay_audio_sender_frames(sender);
    size_t used = 0;
    int16_t frame[960];
    int16_t near_end[480], clean[480];
    size_t near_used = 0;
    int16_t previous = 0;
    uint32_t phase = 0;
#if defined(RTCARPLAY_MIC_AEC)
    uint64_t previous_end = 0;
    unsigned align_frames = 0, candidate_delay = 0, candidate_votes = 0;
#endif
    rt_kprintf("[carplay-mic] started dmic0=48000/stereo target=%u codec=%u packet=%u\n",
               target, (unsigned)in->config.sender.format.codec, (unsigned)packet_frames);
    while (!atomic_load(&in->stop))
    {
        int16_t pcm[480 * 2];
        int n = rtcp_audio_capture_read(capture, pcm, 480);
        if (n < 0)
        {
            rc = n;
            break;
        }
        if (!n)
        {
            rt_thread_mdelay(2);
            continue;
        }
#if defined(RTCARPLAY_MIC_AEC)
        uint64_t read_end = rtcp_audio_capture_end_us(capture);
        bool gap = previous_end && read_end > previous_end + (uint64_t)n * 1000000 / 48000 + 3000;
        if (gap)
        {
            ++capture_gaps;
            near_used = 0;
        }
        previous_end = read_end;
        bool wanted = atomic_load(&aec_requested);
        unsigned delay = atomic_load(&aec_delay_ms);
        if (gap || wanted != active_aec || delay != active_delay)
        {
            rtcp_audio_voice_t *replacement = rtcp_audio_voice_create(true, !wanted, wanted);
            if (!replacement)
            {
                rc = RTCP_ENOMEM;
                break;
            }
            rtcp_audio_voice_destroy(voice);
            voice = replacement;
            active_aec = wanted;
            active_delay = delay;
        }
#endif
        for (int j = 0; j < n; ++j)
        {
            near_end[near_used++] = (int16_t)(((int32_t)pcm[2 * j] + pcm[2 * j + 1]) / 2);
            if (near_used != 480)
                continue;
            near_used = 0;
            const int16_t *far = NULL;
#if defined(RTCARPLAY_MIC_AEC)
            int16_t reference[480];
            uint64_t began = input_now_us();
            uint64_t tail = (uint64_t)(n - j - 1) * 1000000 / 48000;
            uint64_t frame_end = read_end > tail ? read_end - tail : 0;
            if (active_aec)
            {
                if (atomic_load(&aec_auto_delay) && ++align_frames % 10 == 0 &&
                    rt_mutex_take(reference_lock, rt_tick_from_millisecond(2)) == RT_EOK)
                {
                    unsigned measured = 0;
                    bool aligned = carplay_echo_delay(&references, near_end, frame_end, &measured);
                    (void)rt_mutex_release(reference_lock);
                    if (aligned)
                    {
                        if (measured == candidate_delay)
                            ++candidate_votes;
                        else
                        {
                            candidate_delay = measured;
                            candidate_votes = 1;
                        }
                        if (candidate_votes >= 3 && measured != active_delay)
                        {
                            atomic_store(&aec_delay_ms, measured);
                            ++delay_updates;
                            candidate_votes = 0;
                        }
                    }
                    else
                        candidate_votes = 0;
                }
                if (reference_frame(reference, frame_end))
                {
                    far = reference;
                    ++reference_hits;
                }
                else
                {
                    ++reference_misses;
                    /* Preserve the echo filter timeline through a reference
                     * gap; never silently bypass cancellation for that frame. */
                    memset(reference, 0, sizeof(reference));
                    far = reference;
                }
            }
            unsigned peak = 0, clipped = 0;
            for (unsigned k = 0; k < 480; ++k)
            {
                unsigned v = near_end[k] < 0 ? (unsigned)-(int)near_end[k] : (unsigned)near_end[k];
                if (v > peak)
                    peak = v;
                if (v >= 32760)
                    ++clipped;
            }
            if (peak > atomic_load(&mic_peak))
                atomic_store(&mic_peak, peak);
            atomic_fetch_add(&mic_clipped, clipped);
#endif
            if (rtcp_audio_voice_process(voice, near_end, far, clean) != RTCP_OK)
            {
                rc = RTCP_EIO;
                break;
            }
#if defined(RTCARPLAY_MIC_AEC)
            unsigned elapsed = (unsigned)(input_now_us() - began);
            if (elapsed > atomic_load(&process_max_us))
                atomic_store(&process_max_us, elapsed);
#endif
            for (unsigned k = 0; k < 480; ++k)
            {
                int16_t mono = clean[k];
                phase += target;
                if (phase >= 48000)
                {
                    phase -= 48000;
                    frame[used++] = (int16_t)(previous + ((int64_t)mono - previous) * (target - phase) / target);
                    if (used == packet_frames)
                    {
                        uint8_t wire[1452];
                        int bytes = airplay_audio_sender_encode(sender, frame, used, atomic_load(&in->mute), wire, sizeof(wire));
                        if (bytes < 0 || in->config.net->udp_sendto(fd, &in->config.peer, wire, (size_t)bytes) != bytes)
                            atomic_fetch_add(&in->errors, 1);
                        else
                            atomic_fetch_add(&in->packets, 1);
                        atomic_fetch_add(&in->samples, (unsigned)used);
                        used = 0;
                    }
                }
                previous = mono;
            }
        }
        if (rc != RTCP_OK)
            break;
    }

done:
    if (rc != RTCP_OK)
    {
        atomic_fetch_add(&in->errors, 1);
        rt_kprintf("[carplay-mic] source/encoder failure rc=%d\n", rc);
    }
    if (fd >= 0)
        in->config.net->close(fd);
    airplay_audio_sender_destroy(sender);
    rtcp_audio_voice_destroy(voice);
    rtcp_audio_capture_close(capture);
    mbedtls_platform_zeroize(&in->config, sizeof(in->config));
    atomic_store(&in->alive, false);
}
static bool input_start_owned(const airplay_audio_input_config_t *config,
                              unsigned stop_generation)
{
    if (!config || !config->net || !config->net->udp_open || !config->net->udp_sendto || !config->net->close)
        return false;
    /* Busy replacement is rejected without interrupting an existing call. */
    if (atomic_load(&input.alive))
        return false;
    if (!carplay_audio_input_available())
        return false;
    /* Validate packet size/codec before accepting a SETUP; the worker owns its
     * own encoder and device after startup. */
    airplay_audio_sender_t *probe = NULL;
    if (airplay_audio_sender_create(&config->sender, &probe) != RTCP_OK)
        return false;
    airplay_audio_sender_destroy(probe);
#if defined(RTCARPLAY_MIC_AEC)
    if (!reference_lock)
        reference_lock = rt_mutex_create("cmicref", RT_IPC_FLAG_FIFO);
    if (!reference_lock)
        return false;
    (void)rt_mutex_take(reference_lock, RT_WAITING_FOREVER);
    memset(&references, 0, sizeof(references));
    reference_next = 0;
    reference_hits = reference_misses = reference_drops = process_max_us = 0;
    capture_gaps = mic_peak = mic_clipped = 0;
    (void)rt_mutex_release(reference_lock);
#endif
    input.config = *config;
    atomic_store(&input.stop, false);
    atomic_store(&input.alive, true);
    if (stop_generation != atomic_load(&input_stop_generation))
        atomic_store(&input.stop, true);
    atomic_store(&input.packets, 0);
    atomic_store(&input.errors, 0);
    atomic_store(&input.samples, 0);
    input.thread = rt_thread_create("carplay-mic", input_thread, &input, 64 * 1024, 13, 5);
    if (!input.thread || rt_thread_startup(input.thread) != RT_EOK)
    {
        if (input.thread)
            (void)rt_thread_delete(input.thread);
        mbedtls_platform_zeroize(&input.config, sizeof(input.config));
        atomic_store(&input.alive, false);
        return false;
    }
    return true;
}
bool carplay_audio_input_start(const airplay_audio_input_config_t *config)
{
    if (atomic_flag_test_and_set(&input_lifecycle))
        return false;
    unsigned generation = atomic_load(&input_stop_generation);
    bool result = input_start_owned(config, generation);
    atomic_flag_clear(&input_lifecycle);
    return result;
}
void carplay_audio_input_request_stop(void)
{
    atomic_fetch_add(&input_stop_generation, 1U);
    atomic_store(&input.stop, true);
}
int carplay_audio_input_stop(void)
{
    if (atomic_flag_test_and_set(&input_lifecycle))
        return RTCP_ESTATE;
    if (atomic_load(&input.alive) && input.thread == rt_thread_self())
    {
        atomic_flag_clear(&input_lifecycle);
        return RTCP_ESTATE;
    }
    carplay_audio_input_request_stop();
    for (unsigned wait = 0; atomic_load(&input.alive) && wait < 500; ++wait) rt_thread_mdelay(10);
    int result = atomic_load(&input.alive) ? RTCP_ETIMEDOUT : RTCP_OK;
    if (result != RTCP_OK)
        rt_kprintf("[carplay-mic] worker busy; ownership retained\n");
    atomic_flag_clear(&input_lifecycle);
    return result;
}
void carplay_audio_input_mute(bool mute)
{
    atomic_store(&input.mute, mute);
}
bool carplay_audio_input_is_muted(void)
{
    return atomic_load(&input.mute);
}
bool carplay_audio_input_is_running(void)
{
    return atomic_load(&input.alive);
}
void carplay_audio_input_status(void)
{
    rt_kprintf("[carplay-mic] running=%u mute=%u packets=%u samples=%u errors=%u\n",
               atomic_load(&input.alive), atomic_load(&input.mute), atomic_load(&input.packets), atomic_load(&input.samples), atomic_load(&input.errors));
#if defined(RTCARPLAY_MIC_AEC)
    rt_kprintf("[carplay-mic] aec=%u delay_ms=%u ref_ok=%u ref_miss=%u ref_drop=%u\n",
               atomic_load(&aec_requested), atomic_load(&aec_delay_ms), atomic_load(&reference_hits),
               atomic_load(&reference_misses), atomic_load(&reference_drops));
    rt_kprintf("[carplay-mic] auto_delay=%u delay_updates=%u\n",
               atomic_load(&aec_auto_delay), atomic_load(&delay_updates));
    rt_kprintf("[carplay-mic] process_max_us=%u capture_gaps=%u peak=%u clipped=%u\n",
               atomic_load(&process_max_us), atomic_load(&capture_gaps), atomic_load(&mic_peak), atomic_load(&mic_clipped));
#else
    rt_kprintf("[carplay-mic] AEC not compiled\n");
#endif
}
static void mic_command(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "mute"))
        carplay_audio_input_mute(true);
    else if (argc == 2 && !strcmp(argv[1], "unmute"))
        carplay_audio_input_mute(false);
#if defined(RTCARPLAY_MIC_AEC)
    else if (argc == 3 && !strcmp(argv[1], "aec") &&
             (!strcmp(argv[2], "on") || !strcmp(argv[2], "off")))
        atomic_store(&aec_requested, !strcmp(argv[2], "on"));
    else if (argc == 3 && !strcmp(argv[1], "delay"))
    {
        if (!strcmp(argv[2], "auto"))
        {
            atomic_store(&aec_auto_delay, true);
            carplay_audio_input_status();
            return;
        }
        char *end = NULL;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (!argv[2][0] || *end || value > 200)
        {
            rt_kprintf("Usage: carplay_mic delay <auto|0..200 ms>\n");
            return;
        }
        atomic_store(&aec_delay_ms, (unsigned)value);
        atomic_store(&aec_auto_delay, false);
    }
#endif
    carplay_audio_input_status();
}
MSH_CMD_EXPORT_ALIAS(mic_command, carplay_mic, CarPlay microphone status / mute / unmute);

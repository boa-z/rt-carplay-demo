/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtcp_linux.h"
#include "mbedtls/chachapoly.h"
#include "airplay_audio_sender.h"
static int test_sender_create(const airplay_audio_sender_config_t *, airplay_audio_sender_t **);
#define airplay_audio_sender_create test_sender_create
#include "../../platform/audio_input_lubanlite.c"
#undef airplay_audio_sender_create
extern int host_rtthread_fail_start;
extern atomic_bool host_rtthread_hold_entry;
void host_rtthread_join(void);
struct rtcp_audio_capture
{
    unsigned phase;
};
static struct host_audio_device mic_device;
static atomic_uint source_opens, source_closes;
static bool stop_during_probe;
static atomic_bool hold_capture, capture_entered, stop_on_open, self_stop;
static atomic_int self_result;
#ifdef RTCARPLAY_MIC_AEC
static int16_t echo_sample(unsigned n)
{
    uint32_t x = n * 747796405U + 2891336453U;
    x = ((x >> ((x >> 28) + 4)) ^ x) * 277803737U;
    return (int16_t)((int)((x >> 16) & 16383) - 8192);
}
static void test_timed_echo(void)
{
    carplay_echo_history_t *history = calloc(1, sizeof(*history));
    assert(history);
    rtcp_audio_voice_t *voice = rtcp_audio_voice_create(false, false, true);
    assert(voice);
    int16_t stereo[960], reference[480], near[480], clean[480];
    double before = 0, after = 0, dot = 0, local_energy = 0, output_energy = 0;
    for (unsigned block = 0; block < 481; ++block)
    {
        for (unsigned i = 0; i < 480; ++i) stereo[2 * i] = stereo[2 * i + 1] = echo_sample(block * 480 + i);
        carplay_echo_push(history, stereo, 100000 + (uint64_t)block * 10000);
        if (!block)
            continue;
        /* Independently generated echo spans two output chunks at a 5 ms offset. */
        assert(carplay_echo_read(history, 100000 + (uint64_t)(block - 1) * 10000 + 5000, reference));
        for (unsigned i = 0; i < 480; ++i)
        {
            unsigned index = (block - 1) * 480 + 240 + i;
            near[i] = echo_sample(index) / 2;
            if (block > 400)
                near[i] += (int16_t)(echo_sample(index + 1000000) / 2);
        }
        assert(rtcp_audio_voice_process(voice, near, reference, clean) == RTCP_OK);
        if (block > 350 && block <= 400)
            for (unsigned i = 0; i < 480; ++i)
            {
                before += (double)near[i] * near[i];
                after += (double)clean[i] * clean[i];
            }
        if (block > 400)
            for (unsigned i = 0; i < 480; ++i)
            {
                double local = echo_sample((block - 1) * 480 + 240 + i + 1000000) / 2;
                dot += clean[i] * local;
                local_energy += local * local;
                output_energy += (double)clean[i] * clean[i];
            }
    }
    assert(before > 0 && after < before / 4);
    assert(dot > 0 && dot * dot > 0.49 * local_energy * output_energy);
    /* Unknown 35 ms acoustic offset, then independent near-end speech/silence.
  * The estimator must align the former and refuse to learn the latter. */
    unsigned delay = 999;
    uint64_t end = 100000 + (uint64_t)480 * 10000 + 10000;
    for (unsigned i = 0; i < 480; ++i) near[i] = echo_sample(480 * 480 - 35 * 48 + i) / 2;
    assert(carplay_echo_delay(history, near, end, &delay) && delay == 35);
    for (unsigned i = 0; i < 480; ++i) near[i] = echo_sample(9000000 + i);
    assert(!carplay_echo_delay(history, near, end, &delay));
    memset(near, 0, sizeof(near));
    assert(!carplay_echo_delay(history, near, end, &delay));
    for (unsigned i = 0; i < 480; ++i) near[i] = 2000;
    assert(!carplay_echo_delay(history, near, end, &delay));
    printf("timestamped AEC: synthetic residual energy ratio %.6f (not a board acoustic result)\n", after / before);
    rtcp_audio_voice_destroy(voice);
    free(history);
}
#endif
static int test_sender_create(const airplay_audio_sender_config_t *config, airplay_audio_sender_t **out)
{
    int result = airplay_audio_sender_create(config, out);
    if (stop_during_probe)
    {
        stop_during_probe = false;
        carplay_audio_input_request_stop();
    }
    return result;
}
rt_device_t rt_device_find(const char *name)
{
    assert(!strcmp(name, "dmic0"));
    return &mic_device;
}
int rtcp_audio_capture_open(rtcp_audio_capture_t **out)
{
    *out = calloc(1, sizeof(**out));
    ++source_opens;
    if (atomic_exchange(&self_stop, false))
        self_result = carplay_audio_input_stop();
    if (atomic_exchange(&stop_on_open, false))
        carplay_audio_input_request_stop();
    return 0;
}
int rtcp_audio_capture_read(rtcp_audio_capture_t *c, int16_t *pcm, size_t frames)
{
    if (atomic_load(&hold_capture))
    {
        capture_entered = true;
        while (atomic_load(&hold_capture)) rt_thread_mdelay(1);
    }
    assert(frames == 480);
    for (unsigned i = 0; i < 480; ++i)
    {
        int16_t s = (int16_t)((c->phase++ % 48) * 100 - 2400);
        pcm[2 * i] = pcm[2 * i + 1] = s;
    }
    rt_thread_mdelay(10);
    return 480;
}
uint64_t rtcp_audio_capture_end_us(rtcp_audio_capture_t *c)
{
    (void)c;
    return rtcp_os_rtthread.now_us();
}
void rtcp_audio_capture_close(rtcp_audio_capture_t *c)
{
    if (c)
    {
        ++source_closes;
        free(c);
    }
}
static void run_input(uint64_t bits)
{
    int fd = rtcp_net_linux.udp_open(RTCP_AF_INET, 0, false);
    assert(fd >= 0);
    airplay_audio_input_config_t config = { .net = &rtcp_net_linux };
    config.peer = (rtcp_addr_t){ .family = RTCP_AF_INET, .addr = { 127, 0, 0, 1 }, .port = rtcp_net_linux.local_port(fd) };
    config.sender.payload_type = 100;
    memset(config.sender.input_key, 0x55, 32);
    assert(rtcp_audio_format_parse(bits, &config.sender.format));
    assert(carplay_audio_input_start(&config));
    uint8_t wire[1500], data[1500];
    rtcp_addr_t source;
    mbedtls_chachapoly_context cipher;
    mbedtls_chachapoly_init(&cipher);
    assert(mbedtls_chachapoly_setkey(&cipher, config.sender.input_key) == 0);
    for (unsigned i = 0; i < 4; ++i)
    {
        struct rtcp_pollfd p = { .fd = fd, .events = RTCP_POLLIN };
        assert(rtcp_net_linux.poll(&p, 1, 2000) > 0);
        int n = rtcp_net_linux.udp_recvfrom(fd, wire, sizeof(wire), &source);
        assert(n > 36 && n <= 1452);
        uint8_t nonce[12] = { 0 };
        memcpy(nonce + 4, wire + n - 8, 8);
        assert(nonce[4] == i);
        assert(wire[0] == 0x80 && wire[1] == 100 && wire[3] == i);
        assert(mbedtls_chachapoly_auth_decrypt(&cipher, (size_t)n - 36, nonce, wire + 4, 8, wire + n - 24, wire + 12, data) == 0);
        if (bits == 0x4000)
            assert(n == 996); /* 10 ms at 48k mono */
    }
    carplay_audio_input_mute(true);
    carplay_audio_input_request_stop();
    assert(carplay_audio_input_stop() == RTCP_OK);
    host_rtthread_join();
    assert(!atomic_load(&input.alive));
    mbedtls_chachapoly_free(&cipher);
    rtcp_net_linux.close(fd);
}
int main(void)
{
#ifdef RTCARPLAY_MIC_AEC
    test_timed_echo();
    /* Actual timestamp lookup stitches a 5 ms offset across DAC chunks; it must
  * reject missing history rather than reuse the nearest old 10 ms frame. */
    memset(&references, 0, sizeof(references));
    int16_t stereo[960], aligned[480];
    for (unsigned block = 0; block < 3; ++block)
    {
        for (unsigned i = 0; i < 480; ++i) stereo[2 * i] = stereo[2 * i + 1] = (int16_t)(block * 480 + i);
        carplay_echo_push(&references, stereo, 100000 + block * 10000);
    }
    assert(carplay_echo_read(&references, 105000, aligned));
    for (unsigned i = 0; i < 480; ++i) assert(aligned[i] == 240 + (int)i);
    assert(!carplay_echo_read(&references, 95000, aligned));
    assert(!carplay_echo_read(&references, 125000, aligned));
    char *off[] = { "carplay_mic", "aec", "off" };
    mic_command(3, off);
    assert(!aec_requested);
    char *on[] = { "carplay_mic", "aec", "on" };
    mic_command(3, on);
    assert(aec_requested);
    char *delay[] = { "carplay_mic", "delay", "25" };
    mic_command(3, delay);
    assert(aec_delay_ms == 25);
    char *bad[] = { "carplay_mic", "delay", "-1" };
    mic_command(3, bad);
    assert(aec_delay_ms == 25);
    char *zero[] = { "carplay_mic", "delay", "0" };
    mic_command(3, zero);
    assert(aec_delay_ms == 0);
#endif
    self_stop = true;
    self_result = RTCP_OK;
    run_input(0x4000);
    assert(self_result == RTCP_ESTATE);
#ifdef RTCP_AUDIO_CODECS
    run_input(0x20000000);
#endif
    airplay_audio_input_config_t config = { .net = &rtcp_net_linux };
    int fd = rtcp_net_linux.udp_open(RTCP_AF_INET, 0, false);
    assert(fd >= 0);
    config.peer = (rtcp_addr_t){ .family = RTCP_AF_INET, .addr = { 127, 0, 0, 1 }, .port = rtcp_net_linux.local_port(fd) };
    assert(rtcp_audio_format_parse(0x4000, &config.sender.format));
    memset(config.sender.input_key, 0x55, 32);
    host_rtthread_fail_start = 1;
    assert(!carplay_audio_input_start(&config));
    assert(!input.alive && !input.config.net);
    host_rtthread_fail_start = 0;
    assert(!atomic_flag_test_and_set(&input_lifecycle));
    assert(!carplay_audio_input_start(&config));
    assert(carplay_audio_input_stop() == RTCP_ESTATE);
    atomic_flag_clear(&input_lifecycle);
    unsigned before = source_opens;
    stop_during_probe = true;
    assert(carplay_audio_input_start(&config));
    assert(atomic_load(&input.stop));
    for (unsigned i = 0; i < 3000 && input.alive; ++i) rt_thread_mdelay(1);
    assert(!input.alive);
    assert(carplay_audio_input_stop() == RTCP_OK);
    host_rtthread_join();
    assert(source_opens == before); /* request during validation survives reset */
    stop_on_open = true;
    assert(carplay_audio_input_start(&config));
    for (unsigned i = 0; i < 3000 && input.alive; ++i) rt_thread_mdelay(1);
    assert(!input.alive);
    host_rtthread_join();
    assert(source_opens == before + 1 && source_opens == source_closes);
    assert(carplay_audio_input_stop() == RTCP_OK);

    hold_capture = true;
    capture_entered = false;
    assert(carplay_audio_input_start(&config));
    for (unsigned i = 0; i < 3000 && !capture_entered; ++i) rt_thread_mdelay(1);
    assert(capture_entered);
    assert(carplay_audio_input_stop() == RTCP_ETIMEDOUT);
    assert(input.alive && input.config.net == config.net && source_opens == source_closes + 1);
    assert(!carplay_audio_input_start(&config));
    hold_capture = false;
    host_rtthread_join();
    assert(carplay_audio_input_stop() == RTCP_OK && !input.config.net);
    for (unsigned cycle = 0; cycle < 32; ++cycle)
    {
        before = source_opens;
        host_rtthread_hold_entry = true;
        assert(carplay_audio_input_start(&config));
        carplay_audio_input_request_stop();
        host_rtthread_hold_entry = false;
        assert(carplay_audio_input_stop() == RTCP_OK);
        host_rtthread_join();
        assert(source_opens == before && !input.alive && !input.config.net);
    }
    rtcp_net_linux.close(fd);
    assert(source_opens == source_closes);
    puts("actual microphone worker: virtual capture -> voice DSP -> PCM/Opus -> authenticated UDP, stop/restart passed");
    puts("microphone lifecycle: rollback, contention, self-join, startup cancellation, timeout ownership and 32 pre-scheduling stops passed");
}

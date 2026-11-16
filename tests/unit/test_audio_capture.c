/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rtdevice.h>
#include "rtcp/os.h"
#define RT_DEVICE_OFLAG_RDONLY 1
#define AUDIO_TYPE_INPUT       10
#define RT_NULL                NULL
typedef int rt_base_t;
struct rt_ringbuffer
{
    uint8_t data[8192];
    size_t bytes;
};
struct rt_audio_record
{
    struct
    {
        struct rt_ringbuffer ringbuffer;
    } pipe;
};
struct rt_audio_device
{
    struct host_audio_device parent;
    struct rt_audio_record *record;
};
static struct rt_audio_record record;
static struct rt_audio_device device = { .record = &record };
static uint64_t now;
static int fail_config;
static uint64_t clock_now(void)
{
    return now;
}
const struct rtcp_os rtcp_os_rtthread = { .now_us = clock_now };
static rt_base_t rt_hw_interrupt_disable(void)
{
    return 0;
}
static void rt_hw_interrupt_enable(rt_base_t state)
{
    (void)state;
}
static size_t rt_ringbuffer_data_len(struct rt_ringbuffer *r)
{
    return r->bytes;
}
static int rt_device_init(rt_device_t d)
{
    (void)d;
    return 0;
}
static size_t rt_device_read(rt_device_t d, int pos, void *pcm, size_t bytes)
{
    (void)d;
    (void)pos;
    assert(bytes <= record.pipe.ringbuffer.bytes);
    memcpy(pcm, record.pipe.ringbuffer.data, bytes);
    record.pipe.ringbuffer.bytes -= bytes;
    memmove(record.pipe.ringbuffer.data, record.pipe.ringbuffer.data + bytes, record.pipe.ringbuffer.bytes);
    return bytes;
}
#include "../../platform/rtcp_audio_capture_lubanlite.c"
rt_device_t rt_device_find(const char *name)
{
    assert(!strcmp(name, "dmic0"));
    return &device.parent;
}
int rt_device_control(rt_device_t d, int cmd, void *arg)
{
    (void)d;
    assert(cmd == AUDIO_CTL_CONFIGURE);
    struct rt_audio_caps *caps = arg;
    assert(caps->udata.config.samplerate == 48000 && caps->udata.config.channels == 2);
    return fail_config ? -1 : 0;
}
int rt_device_open(rt_device_t d, int flag)
{
    assert(flag == RT_DEVICE_OFLAG_RDONLY);
    ++d->ref_count;
    return 0;
}
int rt_device_close(rt_device_t d)
{
    assert(d->ref_count);
    --d->ref_count;
    return 0;
}
int main(void)
{
    rtcp_audio_capture_t *capture = NULL;
    fail_config = 1;
    assert(rtcp_audio_capture_open(&capture) == RTCP_EIO);
    assert(!capture_owner && !device.parent.rx_indicate);
    fail_config = 0;
    assert(rtcp_audio_capture_open(&capture) == RTCP_OK);
    rtcp_audio_capture_t *other = NULL;
    assert(rtcp_audio_capture_open(&other) == RTCP_ESTATE);
    int16_t pcm[960];
    assert(rtcp_audio_capture_read(capture, pcm, 480) == 0);
    for (unsigned i = 0; i < 4096; ++i) record.pipe.ringbuffer.data[i] = (uint8_t)i;
    record.pipe.ringbuffer.bytes = 4096;
    now = 100000;
    assert(device.parent.rx_indicate(&device.parent, 4096) == RT_EOK);
    assert(rtcp_audio_capture_read(capture, pcm, 480) == 480);
    assert(rtcp_audio_capture_end_us(capture) == 88667);
    assert(rtcp_audio_capture_read(capture, pcm, 480) == 480);
    assert(rtcp_audio_capture_end_us(capture) == 98667);
    assert(rtcp_audio_capture_read(capture, pcm, 480) == 64);
    assert(rtcp_audio_capture_end_us(capture) == 100000);
    /* The next 1024-frame period is 21.333 ms, not the ISR tick's 21 ms. */
    record.pipe.ringbuffer.bytes = 4096;
    now = 121000;
    assert(device.parent.rx_indicate(&device.parent, 4096) == RT_EOK);
    assert(capture->dma_end_us == 121333);
    rtcp_audio_capture_close(capture);
    assert(!capture_owner && !device.parent.rx_indicate && !device.parent.ref_count);
    puts("actual dmic capture: DMA/backlog timestamps, partial reads, exclusive ownership and rollback passed");
}

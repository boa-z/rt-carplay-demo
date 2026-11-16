/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rtcp_audio_capture_lubanlite.h"
#include "rtcp/rtcp.h"
#include "rtcp_rtthread.h"
#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/audio.h>
#include <stdlib.h>
#include <string.h>
struct rtcp_audio_capture
{
    rt_device_t dev;
    uint64_t dma_end_us, read_end_us, dma_epoch_us, dma_frames;
};
static rtcp_audio_capture_t *capture_owner;
static rt_err_t capture_arrived(rt_device_t dev, rt_size_t bytes)
{
    if (capture_owner && capture_owner->dev == dev)
    {
        rtcp_audio_capture_t *c = capture_owner;
        uint64_t now = rtcp_os_rtthread.now_us();
        uint64_t frames = bytes / 4, duration = frames * 1000000 / 48000;
        if (!c->dma_frames)
            c->dma_epoch_us = now > duration ? now - duration : 0;
        c->dma_frames += frames;
        uint64_t end = c->dma_epoch_us + c->dma_frames * 1000000 / 48000;
        uint64_t difference = now > end ? now - end : end - now;
        /* Millisecond OS ticks must not inject a changing 48-sample offset
         * into each AEC frame. Follow the hardware's sample count, rebasing
         * only after a substantial discontinuity. */
        if (difference > 20000)
        {
            c->dma_epoch_us = now > duration ? now - duration : 0;
            c->dma_frames = frames;
            end = c->dma_epoch_us + duration;
        }
        c->dma_end_us = end;
    }
    return RT_EOK;
}
int rtcp_audio_capture_open(rtcp_audio_capture_t **out)
{
    if (!out)
        return RTCP_EINVAL;
    *out = NULL;
    rt_device_t dev = rt_device_find("dmic0");
    if (!dev)
        return RTCP_ENOENT;
    /* This port relies on the SDK audio pipe being single-reader. Do not
     * steal an application recorder's device or replace its callbacks. */
    if (dev->ref_count || dev->rx_indicate || capture_owner)
        return RTCP_ESTATE;
    rtcp_audio_capture_t *c = calloc(1, sizeof(*c));
    if (!c)
        return RTCP_ENOMEM;
    if (rt_device_init(dev) != RT_EOK)
    {
        free(c);
        return RTCP_EIO;
    }
    c->dev = dev;
    rt_base_t level = rt_hw_interrupt_disable();
    capture_owner = c;
    dev->rx_indicate = capture_arrived;
    rt_hw_interrupt_enable(level);
    struct rt_audio_caps caps = { 0 };
    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = 48000;
    caps.udata.config.channels = 2;
    caps.udata.config.samplebits = 16;
    if (rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK ||
        rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY) != RT_EOK)
    {
        level = rt_hw_interrupt_disable();
        dev->rx_indicate = RT_NULL;
        capture_owner = NULL;
        rt_hw_interrupt_enable(level);
        free(c);
        return RTCP_EIO;
    }
    struct rt_audio_device *audio = (struct rt_audio_device *)dev;
    if (!audio->record)
    {
        rtcp_audio_capture_close(c);
        return RTCP_EIO;
    }
    c->dev = dev;
    *out = c;
    return RTCP_OK;
}
int rtcp_audio_capture_read(rtcp_audio_capture_t *c, int16_t *pcm, size_t max_frames)
{
    if (!c || !pcm || !max_frames || max_frames > 480)
        return RTCP_EINVAL;
    struct rt_audio_device *audio = (struct rt_audio_device *)c->dev;
    /* Inspect the SDK pipe under its own IRQ exclusion. There is no other
     * reader. The ISR can add/discard oldest data but cannot empty the pipe,
     * so a nonzero snapshot makes the subsequent read nonblocking without
     * altering driver flags, cancelling reads or touching DMA storage. */
    rt_base_t level = rt_hw_interrupt_disable();
    size_t available = rt_ringbuffer_data_len(&audio->record->pipe.ringbuffer);
    uint64_t dma_end = c->dma_end_us;
    rt_hw_interrupt_enable(level);
    available -= available % 4;
    if (!available)
        return 0;
    size_t bytes = max_frames * 4;
    if (bytes > available)
        bytes = available;
    size_t got = rt_device_read(c->dev, 0, pcm, bytes);
    if (got > bytes || got % 4)
        return RTCP_EIO;
    uint64_t backlog = (uint64_t)((available - got) / 4) * 1000000 / 48000;
    c->read_end_us = dma_end > backlog ? dma_end - backlog : 0;
    return (int)(got / 4);
}
uint64_t rtcp_audio_capture_end_us(rtcp_audio_capture_t *c)
{
    return c ? c->read_end_us : 0;
}
void rtcp_audio_capture_close(rtcp_audio_capture_t *c)
{
    if (c)
    {
        rt_base_t level = rt_hw_interrupt_disable();
        if (capture_owner == c)
        {
            c->dev->rx_indicate = RT_NULL;
            capture_owner = NULL;
        }
        rt_hw_interrupt_enable(level);
        (void)rt_device_close(c->dev);
        free(c);
    }
}

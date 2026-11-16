/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Local media volume for the D213 speaker path: the board codec's mixer gain.
 * The AirPlay SET_PARAMETER volume is a separate remote control and is not
 * overwritten here. */
#include "audio_volume_lubanlite.h"

#if defined(RT_USING_AUDIO)
#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/audio.h>
#include <string.h>

#define VOLUME_DEVICE "sound0"

static rt_device_t volume_device(void)
{
    return rt_device_find(VOLUME_DEVICE);
}

bool carplay_volume_available(void)
{
    rt_device_t dev = volume_device();
    if (!dev)
        return false;
    struct rt_audio_caps caps;
    memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = AUDIO_MIXER_QUERY;
    /* A driver without mixer support answers -RT_ERROR; the launcher then
     * disables the control instead of pretending the value was applied. */
    return rt_device_control(dev, AUDIO_CTL_GETCAPS, &caps) == RT_EOK &&
           (caps.udata.mask & AUDIO_MIXER_VOLUME) != 0;
}

bool carplay_volume_get(unsigned *percent)
{
    if (!percent)
        return false;
    rt_device_t dev = volume_device();
    if (!dev)
        return false;
    struct rt_audio_caps caps;
    memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = AUDIO_MIXER_VOLUME;
    if (rt_device_control(dev, AUDIO_CTL_GETCAPS, &caps) != RT_EOK)
        return false;
    *percent = caps.udata.value > 100 ? 100U : caps.udata.value;
    return true;
}

bool carplay_volume_set(unsigned percent)
{
    rt_device_t dev = volume_device();
    if (!dev)
        return false;
    if (percent > 100U)
        percent = 100U;
    struct rt_audio_caps caps;
    memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = AUDIO_MIXER_VOLUME;
    caps.udata.value = percent;
    if (rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
        return false;
    unsigned readback = 0;
    if (!carplay_volume_get(&readback))
        return false;
    return readback == percent;
}

#else /* RT_USING_AUDIO */

bool carplay_volume_available(void)
{
    return false;
}

bool carplay_volume_get(unsigned *percent)
{
    (void)percent;
    return false;
}

bool carplay_volume_set(unsigned percent)
{
    (void)percent;
    return false;
}

#endif /* RT_USING_AUDIO */

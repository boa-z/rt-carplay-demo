/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Panel backlight for the D213 demo board. The schematic routes PC.7 as
 * LCD_PWM (PWM channel 3, output B) into the LED boost driver, so brightness
 * is the real PWM duty rather than a software tint. When the image is built
 * with the GPIO-only backlight the control stays disabled instead of faking it. */
#include "backlight_lubanlite.h"

#if defined(RT_USING_PWM) && defined(AIC_USING_PWM3)
#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/rt_drv_pwm.h>
#include <string.h>

/* Keep these in step with AIC_USING_PWM3 and the board pinmux entry. */
#define BACKLIGHT_PWM_DEVICE  "pwm"
#define BACKLIGHT_PWM_CHANNEL 3
#define BACKLIGHT_PWM_FREQ    1000U
#define BACKLIGHT_DEFAULT     72U

static struct rt_device_pwm *backlight_device(void)
{
    return (struct rt_device_pwm *)rt_device_find(BACKLIGHT_PWM_DEVICE);
}

bool carplay_backlight_available(void)
{
    return backlight_device() != RT_NULL;
}

static unsigned clamp(unsigned percent)
{
    if (percent < CARPLAY_BRIGHTNESS_MIN)
        return CARPLAY_BRIGHTNESS_MIN;
    if (percent > CARPLAY_BRIGHTNESS_MAX)
        return CARPLAY_BRIGHTNESS_MAX;
    return percent;
}

static bool apply_duty(unsigned percent)
{
    struct rt_device_pwm *pwm = backlight_device();
    if (!pwm)
        return false;
    unsigned level = clamp(percent);
    uint32_t period_ns = 1000000000UL / BACKLIGHT_PWM_FREQ;
    if (rt_pwm_set(pwm, BACKLIGHT_PWM_CHANNEL, period_ns, period_ns * level / 100U) != RT_EOK)
        return false;
    if (rt_pwm_enable(pwm, BACKLIGHT_PWM_CHANNEL) != RT_EOK)
        return false;
    return true;
}

bool carplay_backlight_get(unsigned *percent)
{
    if (!percent)
        return false;
    struct rt_device_pwm *pwm = backlight_device();
    if (!pwm)
        return false;
    struct rt_pwm_configuration cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.channel = BACKLIGHT_PWM_CHANNEL;
    if (rt_device_control((rt_device_t)pwm, PWM_CMD_GET, &cfg) != RT_EOK || !cfg.period)
        return false;
    *percent = (unsigned)((uint64_t)cfg.pulse * 100U / cfg.period);
    return true;
}

bool carplay_backlight_set(unsigned percent)
{
    return apply_duty(percent);
}

void carplay_backlight_restore(unsigned percent)
{
    if (carplay_backlight_available())
        (void)apply_duty(percent ? percent : BACKLIGHT_DEFAULT);
}

#else /* no PWM backlight in this image */

bool carplay_backlight_available(void)
{
    return false;
}

bool carplay_backlight_get(unsigned *percent)
{
    (void)percent;
    return false;
}

bool carplay_backlight_set(unsigned percent)
{
    (void)percent;
    return false;
}

void carplay_backlight_restore(unsigned percent)
{
    (void)percent;
}

#endif

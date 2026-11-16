/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "video_layer_policy.h"
/* Match drv_fb.c's ioctl ABI before any libc header can define _IOWR/_IOW.
 * sys/ioctl.h uses size/direction bits that the SDK driver does not accept. */
#include <aic_common.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdatomic.h>
#include "artinchip_fb.h"

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "fusion_display.h"
#include "rtcp/rtcp.h"

static rt_device_t fusion_fb;
static struct aicfb_layer_data original_layer;
static struct aicfb_alpha_config original_alpha;
static bool captured, applied;
static carplay_display_mode_t applied_mode;

bool carplay_launcher_video_is_active(void)
{
    carplay_fusion_snapshot_t s;
    carplay_fusion_snapshot(rt_tick_get_millisecond(), &s);
    return s.video_active && s.effective == CARPLAY_FULLSCREEN;
}

void carplay_launcher_video_event(bool active, void *context)
{
    (void)context;
    if (!active)
        carplay_fusion_video_stopped();
}

int carplay_launcher_fusion_apply(const carplay_fusion_snapshot_t *s)
{
    if (!s || !s->initialized)
        return RTCP_ESTATE;
    if (applied && s->effective == applied_mode)
        return RTCP_OK;
    applied = false;
    if (!fusion_fb)
        fusion_fb = rt_device_find("aicfb");
    if (!fusion_fb)
        return RTCP_ENOENT;
    struct aicfb_layer_data current = { .layer_id = AICFB_LAYER_TYPE_UI, .rect_id = 0 };
    struct aicfb_alpha_config alpha = { .layer_id = AICFB_LAYER_TYPE_UI };
    if (rt_device_control(fusion_fb, AICFB_GET_LAYER_CONFIG, &current) < 0 ||
        rt_device_control(fusion_fb, AICFB_GET_ALPHA_CONFIG, &alpha) < 0)
        return RTCP_EIO;
    if (!captured)
    {
        original_layer = current;
        original_alpha = alpha;
        captured = true;
    }
    struct aicfb_layer_data next = current;
    /* Preserve the current LVGL buffer address/stride across PAN_DISPLAY.
     * Restoring an old physical address would race double buffering. */
    next.pos = original_layer.pos;
    next.scale_size = original_layer.scale_size;
    next.buf.crop_en = original_layer.buf.crop_en;
    next.buf.crop = original_layer.buf.crop;
    struct aicfb_alpha_config next_alpha = original_alpha;
    if (s->effective == CARPLAY_FUSION)
    {
        if (original_layer.buf.crop_en || original_layer.pos.x || original_layer.pos.y ||
            current.buf.size.width != s->width || current.buf.size.height != s->height ||
            !s->local_width || s->local_width >= s->width)
            return RTCP_EINVAL;
        next.pos.x = s->width - s->local_width;
        next.pos.y = 0;
        next.buf.crop_en = 1;
        next.buf.crop.x = next.pos.x;
        next.buf.crop.y = 0;
        next.buf.crop.width = s->local_width;
        next.buf.crop.height = s->height;
        /* DE keeps a full-panel UI surface even when its only rectangle is
         * cropped to the instrument strip. Keep the empty region transparent
         * through pixel alpha instead of overriding it with global opacity. */
        next_alpha.enable = 1;
        next_alpha.mode = AICFB_PIXEL_ALPHA_MODE;
        next_alpha.value = 255;
    }
    else if (s->effective == CARPLAY_FULLSCREEN)
    {
        /* Native full screen belongs entirely to the phone, including touch.
         * Restore UI geometry but make its hardware layer transparent. */
        next_alpha.enable = 1;
        next_alpha.mode = AICFB_GLOBAL_ALPHA_MODE;
        next_alpha.value = 0;
    }
    if (rt_device_control(fusion_fb, AICFB_UPDATE_LAYER_CONFIG, &next) < 0)
        return RTCP_EIO;
    if (rt_device_control(fusion_fb, AICFB_UPDATE_ALPHA_CONFIG, &next_alpha) < 0 ||
        rt_device_control(fusion_fb, AICFB_WAIT_FOR_VSYNC, NULL) < 0)
    {
        (void)rt_device_control(fusion_fb, AICFB_UPDATE_LAYER_CONFIG, &current);
        (void)rt_device_control(fusion_fb, AICFB_UPDATE_ALPHA_CONFIG, &alpha);
        return RTCP_EIO;
    }
    applied_mode = s->effective;
    applied = true;
    return RTCP_OK;
}
#else
/* The reference application owns the entire launcher UI layer. A host with
 * persistent vehicle/safety HMI must supply its own compositing policy. */
static rt_device_t framebuffer;
static struct aicfb_alpha_config saved_alpha;
static bool saved;
static atomic_bool handed_off;

bool carplay_launcher_video_is_active(void)
{
    return handed_off;
}

void carplay_launcher_video_event(bool active, void *context)
{
    (void)context;
    if (!active)
    {
        if (saved && framebuffer != NULL)
        {
            if (rt_device_control(framebuffer, AICFB_UPDATE_ALPHA_CONFIG,
                                  &saved_alpha) >= 0)
            {
                saved = false;
                handed_off = false;
            }
            else
                rt_kprintf("[carplay-ui] UI alpha restore failed\n");
        }
        return;
    }
    if (saved)
        return;
    framebuffer = rt_device_find("aicfb");
    if (framebuffer == NULL)
        return;
    memset(&saved_alpha, 0, sizeof(saved_alpha));
    saved_alpha.layer_id = AICFB_LAYER_TYPE_UI;
    if (rt_device_control(framebuffer, AICFB_GET_ALPHA_CONFIG,
                          &saved_alpha) < 0)
    {
        rt_kprintf("[carplay-ui] UI alpha query failed, cmd=%#x\n",
                   (unsigned)AICFB_GET_ALPHA_CONFIG);
        return; /* Never hide HMI without a restorable configuration. */
    }
    struct aicfb_alpha_config transparent = { 0 };
    transparent.layer_id = AICFB_LAYER_TYPE_UI;
    transparent.enable = 1;
    transparent.mode = AICFB_GLOBAL_ALPHA_MODE;
    saved = true;
    if (rt_device_control(framebuffer, AICFB_UPDATE_ALPHA_CONFIG,
                          &transparent) < 0)
        rt_kprintf("[carplay-ui] UI alpha handoff failed\n");
    else
    {
        handed_off = true;
        rt_kprintf("[carplay-ui] video visible, UI alpha=0\n");
    }
}

#endif

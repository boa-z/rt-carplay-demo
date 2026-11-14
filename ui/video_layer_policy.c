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
    if (!active) {
        if (saved && framebuffer != NULL) {
            if (rt_device_control(framebuffer, AICFB_UPDATE_ALPHA_CONFIG,
                                  &saved_alpha) >= 0) {
                saved = false;
                handed_off = false;
            } else
                rt_kprintf("[carplay-ui] UI alpha restore failed\n");
        }
        return;
    }
    if (saved) return;
    framebuffer = rt_device_find("aicfb");
    if (framebuffer == NULL) return;
    memset(&saved_alpha, 0, sizeof(saved_alpha));
    saved_alpha.layer_id = AICFB_LAYER_TYPE_UI;
    if (rt_device_control(framebuffer, AICFB_GET_ALPHA_CONFIG,
                          &saved_alpha) < 0) {
        rt_kprintf("[carplay-ui] UI alpha query failed, cmd=%#x\n",
                   (unsigned)AICFB_GET_ALPHA_CONFIG);
        return; /* Never hide HMI without a restorable configuration. */
    }
    struct aicfb_alpha_config transparent = {0};
    transparent.layer_id = AICFB_LAYER_TYPE_UI;
    transparent.enable = 1;
    transparent.mode = AICFB_GLOBAL_ALPHA_MODE;
    saved = true;
    if (rt_device_control(framebuffer, AICFB_UPDATE_ALPHA_CONFIG,
                          &transparent) < 0)
        rt_kprintf("[carplay-ui] UI alpha handoff failed\n");
    else {
        handed_off = true;
        rt_kprintf("[carplay-ui] video visible, UI alpha=0\n");
    }
}

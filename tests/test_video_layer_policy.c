/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Compile the actual host policy with the SDK's framebuffer API and a device
 * model. This checks policy/rollback, not real display or cache behavior. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "../ui/video_layer_policy.c"

static unsigned reads, writes;
static bool missing, fail_read, fail_write;
static struct aicfb_alpha_config current;

rt_device_t rt_device_find(const char *name)
{
    assert(strcmp(name, "aicfb") == 0);
    return missing ? NULL : (rt_device_t)(uintptr_t)1;
}

int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}

int rt_device_control(rt_device_t device, int command, void *argument)
{
    assert(device != NULL);
    struct aicfb_alpha_config *alpha = argument;
    assert(alpha->layer_id == AICFB_LAYER_TYPE_UI);
    if ((unsigned)command == 0x4626U) {
        ++reads;
        if (fail_read) return -1;
        *alpha = current;
        return 0;
    }
    assert((unsigned)command == 0x4627U);
    ++writes;
    if (fail_write) return -1;
    current = *alpha;
    return 0;
}

int main(void)
{
    /* Independent of the caller's macros: these are the commands accepted
     * by drv_fb.c after aic_core.h supplies the SDK ioctl encoding. */
    assert((unsigned)AICFB_GET_ALPHA_CONFIG == 0x4626U);
    assert((unsigned)AICFB_UPDATE_ALPHA_CONFIG == 0x4627U);
    current.layer_id = AICFB_LAYER_TYPE_UI;
    current.enable = 1;
    current.mode = AICFB_GLOBAL_ALPHA_MODE;
    current.value = 173;
    struct aicfb_alpha_config original = current;
    carplay_launcher_video_event(false, NULL);
    assert(reads == 0 && writes == 0 && !carplay_launcher_video_is_active());
    missing = true;
    carplay_launcher_video_event(true, NULL);
    assert(reads == 0 && writes == 0);
    missing = false; fail_read = true;
    carplay_launcher_video_event(true, NULL);
    assert(reads == 1 && writes == 0 && !carplay_launcher_video_is_active());
    fail_read = false;
    carplay_launcher_video_event(true, NULL);
    assert(reads == 2 && writes == 1 && current.value == 0 &&
           carplay_launcher_video_is_active());
    carplay_launcher_video_event(true, NULL);
    assert(reads == 2 && writes == 1);
    carplay_launcher_video_event(false, NULL);
    assert(writes == 2 && memcmp(&current, &original, sizeof(current)) == 0 &&
           !carplay_launcher_video_is_active());
    carplay_launcher_video_event(false, NULL);
    assert(writes == 2);

    /* Reconnect recaptures current host policy; a failed hide is restorable. */
    current.value = 219; original = current;
    fail_write = true;
    carplay_launcher_video_event(true, NULL);
    assert(current.value == 219 && !carplay_launcher_video_is_active());
    fail_write = false;
    carplay_launcher_video_event(false, NULL);
    assert(memcmp(&current, &original, sizeof(current)) == 0);

    /* A transient restore failure retains the snapshot for a later retry. */
    carplay_launcher_video_event(true, NULL);
    assert(current.value == 0);
    fail_write = true;
    carplay_launcher_video_event(false, NULL);
    assert(current.value == 0 && saved && carplay_launcher_video_is_active());
    fail_write = false;
    carplay_launcher_video_event(false, NULL);
    assert(memcmp(&current, &original, sizeof(current)) == 0 && !saved &&
           !carplay_launcher_video_is_active());
    puts("host video UI policy regression passed");
    return 0;
}

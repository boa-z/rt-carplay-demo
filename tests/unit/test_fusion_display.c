/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual application coordinator and hardware policy with the SDK ioctl ABI.
 * No physical panel, MPP, iPhone or vehicle bus is represented by this test. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "../../app/fusion_display.c"
#include "../../ui/video_layer_policy.c"

static unsigned critical_depth, writes;
static bool missing, fail_layer, fail_alpha, fail_vsync;
static struct aicfb_layer_data ui_layer;
static struct aicfb_alpha_config ui_alpha;

rt_base_t rt_hw_interrupt_disable(void)
{
    return critical_depth++;
}
void rt_hw_interrupt_enable(rt_base_t level)
{
    assert(level + 1 == critical_depth);
    --critical_depth;
}
uint32_t rt_tick_get_millisecond(void)
{
    return 123000;
}
int rt_kprintf(const char *format, ...)
{
    (void)format;
    assert(!critical_depth);
    return 0;
}
rt_device_t rt_device_find(const char *name)
{
    assert(!critical_depth && !strcmp(name, "aicfb"));
    return missing ? NULL : (void *)(uintptr_t)1;
}
int rt_device_control(rt_device_t device, int command, void *argument)
{
    assert(!critical_depth && device);
    if (command == AICFB_GET_LAYER_CONFIG)
    {
        *(struct aicfb_layer_data *)argument = ui_layer;
        return 0;
    }
    if (command == AICFB_GET_ALPHA_CONFIG)
    {
        *(struct aicfb_alpha_config *)argument = ui_alpha;
        return 0;
    }
    if (command == AICFB_UPDATE_LAYER_CONFIG)
    {
        ++writes;
        if (fail_layer)
            return -1;
        ui_layer = *(struct aicfb_layer_data *)argument;
        return 0;
    }
    if (command == AICFB_UPDATE_ALPHA_CONFIG)
    {
        ++writes;
        if (fail_alpha)
            return -1;
        ui_alpha = *(struct aicfb_alpha_config *)argument;
        return 0;
    }
    assert(command == AICFB_WAIT_FOR_VSYNC);
    return fail_vsync ? -1 : 0;
}

static carplay_fusion_snapshot_t snapshot(void)
{
    carplay_fusion_snapshot_t s;
    carplay_fusion_snapshot(123000, &s);
    return s;
}
static void latch(void)
{
    rtcp_display_rect_t viewport, destination;
    uint32_t generation;
    assert(carplay_fusion_video_target(1024, 600, &viewport, &generation));
    assert(rtcp_display_fit(&viewport, 1920, 1080, &destination));
    carplay_fusion_video_latched(&destination, 1024, 600, generation, 123000);
}
static carplay_fusion_touch_t touch(uint8_t phase, uint16_t x, uint16_t y)
{
    return carplay_fusion_touch(phase, 7, x, y);
}

int main(void)
{
    assert(carplay_fusion_set_mode(CARPLAY_FUSION) == RTCP_ESTATE);
    assert(carplay_fusion_init(0, 600) == RTCP_EINVAL);
    assert(carplay_fusion_init(1024, 600) == RTCP_OK);
    assert(carplay_fusion_init(1024, 600) == RTCP_OK);
    assert(carplay_fusion_init(800, 480) == RTCP_ESTATE);
    assert(carplay_fusion_set_mode((carplay_display_mode_t)99) == RTCP_EINVAL);
    carplay_fusion_snapshot_t s = snapshot();
    assert(s.requested == CARPLAY_FUSION && s.effective == CARPLAY_LOCAL && s.local_width == 256);
    assert(s.vehicle.available && s.vehicle.simulated && !strcmp(s.vehicle.gear, "D"));
    assert(touch(RTCP_TOUCH_DOWN, 100, 100).owner == CARPLAY_TOUCH_LOCAL);
    latch();
    s = snapshot();
    assert(s.effective == CARPLAY_FUSION && s.video_rect.width == 768 && s.video_rect.height == 432 && s.video_rect.y == 84);
    carplay_fusion_touch_t action = carplay_fusion_cancel_touch();
    assert(action.owner == CARPLAY_TOUCH_LOCAL && action.phase == RTCP_TOUCH_UP);
    assert(touch(RTCP_TOUCH_MOVE, 384, 300).owner == CARPLAY_TOUCH_NONE);
    assert(touch(RTCP_TOUCH_UP, 384, 300).owner == CARPLAY_TOUCH_NONE);
    assert(touch(RTCP_TOUCH_DOWN, 384, 300).owner == CARPLAY_TOUCH_PHONE);
    action = touch(RTCP_TOUCH_MOVE, 384, 300);
    assert(action.x == 512 && action.y == 300);
    /* A second finger cannot take over the captured gesture. */
    assert(carplay_fusion_touch(RTCP_TOUCH_DOWN, 8, 900, 100).owner == CARPLAY_TOUCH_NONE);
    action = touch(RTCP_TOUCH_MOVE, 900, 300);
    assert(action.owner == CARPLAY_TOUCH_PHONE && action.phase == RTCP_TOUCH_UP && action.x == 512);
    assert(touch(RTCP_TOUCH_MOVE, 900, 300).owner == CARPLAY_TOUCH_NONE);
    assert(touch(RTCP_TOUCH_UP, 900, 300).owner == CARPLAY_TOUCH_NONE);
    assert(touch(RTCP_TOUCH_DOWN, 900, 100).owner == CARPLAY_TOUCH_LOCAL);
    assert(touch(RTCP_TOUCH_MOVE, 300, 100).owner == CARPLAY_TOUCH_LOCAL);
    assert(touch(RTCP_TOUCH_UP, 300, 100).owner == CARPLAY_TOUCH_LOCAL);
    assert(touch(RTCP_TOUCH_DOWN, 300, 20).owner == CARPLAY_TOUCH_NONE); /* letterbox */
    assert(touch(RTCP_TOUCH_UP, 300, 20).owner == CARPLAY_TOUCH_NONE);
    action = touch(RTCP_TOUCH_DOWN, 767, 515);
    assert(action.owner == CARPLAY_TOUCH_PHONE && action.x < 1024 && action.y < 600);
    assert(carplay_fusion_set_mode(CARPLAY_FULLSCREEN) == RTCP_OK);
    action = carplay_fusion_cancel_touch();
    assert(action.owner == CARPLAY_TOUCH_PHONE && action.phase == RTCP_TOUCH_UP);
    (void)touch(RTCP_TOUCH_UP, 767, 515);
    assert(touch(RTCP_TOUCH_DOWN, 200, 200).owner == CARPLAY_TOUCH_NONE); /* layout not latched */
    latch();
    assert(touch(RTCP_TOUCH_DOWN, 200, 200).owner == CARPLAY_TOUCH_PHONE);
    action = touch(RTCP_TOUCH_UP, 2000, 2000);
    assert(action.phase == RTCP_TOUCH_UP && action.owner == CARPLAY_TOUCH_PHONE);
    assert(touch(RTCP_TOUCH_DOWN, 200, 200).owner == CARPLAY_TOUCH_PHONE);
    carplay_fusion_video_stopped();
    action = carplay_fusion_cancel_touch();
    assert(action.owner == CARPLAY_TOUCH_PHONE && action.phase == RTCP_TOUCH_UP);
    (void)touch(RTCP_TOUCH_UP, 200, 200);
    assert(snapshot().effective == CARPLAY_LOCAL);
    assert(touch(RTCP_TOUCH_DOWN, 200, 200).owner == CARPLAY_TOUCH_LOCAL);
    (void)touch(RTCP_TOUCH_UP, 200, 200);
    carplay_vehicle_sample_t sample = { .available = true, .simulated = false, .speed_kph = 43, .battery_percent = 77, .gear = "R" };
    assert(carplay_fusion_set_vehicle(&sample) == RTCP_OK);
    sample.speed_kph = 99;
    s = snapshot();
    assert(s.vehicle.speed_kph == 43 && !s.vehicle.simulated);
    sample.battery_percent = 101;
    assert(carplay_fusion_set_vehicle(&sample) == RTCP_EINVAL);
    sample.battery_percent = 77;
    memset(sample.gear, 'X', sizeof(sample.gear));
    assert(carplay_fusion_set_vehicle(&sample) == RTCP_EINVAL);
    carplay_fusion_use_simulated();
    assert(snapshot().vehicle.simulated);

    /* Actual UI crop/alpha policy preserves the current LVGL physical buffer. */
    ui_layer = (struct aicfb_layer_data){ .layer_id = AICFB_LAYER_TYPE_UI, .enable = 1 };
    ui_layer.buf.size.width = 1024;
    ui_layer.buf.size.height = 600;
    ui_layer.buf.phy_addr[0] = 100;
    ui_alpha = (struct aicfb_alpha_config){ .layer_id = AICFB_LAYER_TYPE_UI, .enable = 1, .mode = AICFB_GLOBAL_ALPHA_MODE, .value = 255 };
    (void)carplay_fusion_set_mode(CARPLAY_FUSION);
    latch();
    s = snapshot();
    missing = true;
    assert(carplay_launcher_fusion_apply(&s) == RTCP_ENOENT);
    missing = false;
    assert(carplay_launcher_fusion_apply(&s) == RTCP_OK);
    assert(ui_layer.pos.x == 768 && ui_layer.buf.crop_en && ui_layer.buf.crop.x == 768 && ui_layer.buf.crop.width == 256);
    assert(ui_alpha.enable && ui_alpha.mode == AICFB_PIXEL_ALPHA_MODE && ui_alpha.value == 255);
    unsigned before = writes;
    assert(carplay_launcher_fusion_apply(&s) == RTCP_OK && writes == before);
    ui_layer.buf.phy_addr[0] = 200; /* LVGL PAN_DISPLAY to the other framebuffer. */
    (void)carplay_fusion_set_mode(CARPLAY_LOCAL);
    s = snapshot();
    fail_layer = true;
    assert(carplay_launcher_fusion_apply(&s) == RTCP_EIO && ui_layer.buf.crop_en);
    fail_layer = false;
    fail_alpha = true;
    assert(carplay_launcher_fusion_apply(&s) == RTCP_EIO && ui_layer.buf.crop_en);
    fail_alpha = false;
    fail_vsync = true;
    assert(carplay_launcher_fusion_apply(&s) == RTCP_EIO && ui_layer.buf.crop_en);
    fail_vsync = false;
    assert(carplay_launcher_fusion_apply(&s) == RTCP_OK && !ui_layer.buf.crop_en && !ui_layer.pos.x);
    assert(ui_layer.buf.phy_addr[0] == 200 && ui_alpha.mode == AICFB_GLOBAL_ALPHA_MODE && ui_alpha.value == 255);
    (void)carplay_fusion_set_mode(CARPLAY_FULLSCREEN);
    s = snapshot();
    assert(carplay_launcher_fusion_apply(&s) == RTCP_OK && ui_alpha.mode == AICFB_PIXEL_ALPHA_MODE && ui_alpha.value == 255);
    (void)carplay_fusion_set_mode(CARPLAY_FUSION);
    latch();
    s = snapshot();
    assert(carplay_launcher_fusion_apply(&s) == RTCP_OK && ui_alpha.mode == AICFB_PIXEL_ALPHA_MODE && ui_alpha.value == 255);
    carplay_launcher_video_event(false, NULL);
    s = snapshot();
    assert(s.effective == CARPLAY_LOCAL && carplay_launcher_fusion_apply(&s) == RTCP_OK && ui_alpha.value == 255);
    /* A viewport-sized phone stream fills the left region, including edges. */
    (void)carplay_fusion_set_mode(CARPLAY_FUSION);
    rtcp_display_rect_t viewport, destination;
    uint32_t viewport_generation;
    assert(carplay_fusion_video_target(1024, 600, &viewport, &viewport_generation));
    assert(rtcp_display_fit(&viewport, 768, 600, &destination));
    carplay_fusion_video_latched(&destination, 768, 600, viewport_generation, 123000);
    s = snapshot();
    assert(s.video_rect.x == 0 && s.video_rect.y == 0 && s.logical_width == 768);
    action = touch(RTCP_TOUCH_DOWN, 0, 0);
    assert(action.owner == CARPLAY_TOUCH_PHONE && action.x == 0 && action.y == 0);
    action = touch(RTCP_TOUCH_UP, 767, 599);
    assert(action.owner == CARPLAY_TOUCH_PHONE && action.x == 767 && action.y == 599);
    assert(touch(RTCP_TOUCH_DOWN, 768, 599).owner == CARPLAY_TOUCH_LOCAL);
    assert(touch(RTCP_TOUCH_UP, 768, 599).owner == CARPLAY_TOUCH_LOCAL);
    rtcp_display_rect_t target;
    uint32_t generation;
    (void)carplay_fusion_set_mode(CARPLAY_FUSION);
    assert(carplay_fusion_video_target(800, 480, &target, &generation));
    assert(target.width == 800 && snapshot().requested == CARPLAY_LOCAL && snapshot().error == RTCP_EINVAL);
    assert(!critical_depth);
    puts("Single-screen modes: latched geometry, gesture ownership/cancellation, simulated/provider data and DE UI crop/rollback passed (Host only)");
    return 0;
}

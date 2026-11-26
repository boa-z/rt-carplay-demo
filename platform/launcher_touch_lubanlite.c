/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <rtthread.h>
#include "launcher_touch_lubanlite.h"
#include "carplay_app.h"
#include "connection_manager.h"
#include "touch_panel_lubanlite.h"
#include "../ui/video_layer_policy.h"
#include "video_sink_lubanlite.h"
#include "rtcp/input.h"
#include "lvgl.h"

typedef struct
{
    uint16_t x, y;
    bool down;
} pointer_sample_t;

enum
{
    POINTER_QUEUE_SIZE = 32
};

static struct
{
    rt_mutex_t lock;
    pointer_sample_t queue[POINTER_QUEUE_SIZE], current;
    unsigned head, tail;
    bool active;
    uint16_t id;
} pointer;

bool carplay_launcher_touch_init(void)
{
    if (!pointer.lock)
        pointer.lock = rt_mutex_create("cpdemo", RT_IPC_FLAG_PRIO);
    return pointer.lock != NULL;
}

bool carplay_demo_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y)
{
#if !defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    if (carplay_launcher_video_is_active() &&
        carplay_video_sink_is_running() && carplay_app_is_running())
    {
        if (pointer.lock)
        {
            (void)rt_mutex_take(pointer.lock, RT_WAITING_FOREVER);
            pointer.head = pointer.tail = 0;
            pointer.current.down = false;
            pointer.active = false;
            (void)rt_mutex_release(pointer.lock);
        }
        return false;
    }
#endif
    if (!pointer.lock)
        return true;
    (void)rt_mutex_take(pointer.lock, RT_WAITING_FOREVER);
    if (!pointer.active && phase == RTCP_TOUCH_DOWN)
    {
        pointer.active = true;
        pointer.id = id;
    }
    if (pointer.active && pointer.id == id)
    {
        pointer.active = phase != RTCP_TOUCH_UP;
        unsigned next = (pointer.head + 1U) % POINTER_QUEUE_SIZE;
        if (next == pointer.tail)
            pointer.tail = pointer.head;
        pointer.queue[pointer.head] = (pointer_sample_t){ x, y, pointer.active };
        pointer.head = next;
    }
    (void)rt_mutex_release(pointer.lock);
    return true;
}

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
void carplay_launcher_touch_dispatch(carplay_fusion_touch_t action)
{
    if (action.owner == CARPLAY_TOUCH_PHONE)
        (void)carplay_connection_touch(action.phase, action.id, action.x, action.y);
    else if (action.owner == CARPLAY_TOUCH_LOCAL)
        (void)carplay_demo_touch(action.phase, action.id, action.x, action.y);
}
#endif

static void panel_touch(uint8_t phase, uint16_t id, uint16_t x,
                        uint16_t y, void *context)
{
    (void)context;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    carplay_launcher_touch_dispatch(carplay_fusion_touch(phase, id, x, y));
#else
    if (!carplay_demo_touch(phase, id, x, y))
        (void)carplay_connection_touch(phase, id, x, y);
#endif
}

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    (void)rt_mutex_take(pointer.lock, RT_WAITING_FOREVER);
    if (pointer.tail != pointer.head)
    {
        pointer.current = pointer.queue[pointer.tail];
        pointer.tail = (pointer.tail + 1U) % POINTER_QUEUE_SIZE;
    }
    data->point.x = pointer.current.x;
    data->point.y = pointer.current.y;
    data->state = pointer.current.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = pointer.tail != pointer.head;
    (void)rt_mutex_release(pointer.lock);
}

bool carplay_launcher_touch_bind(void)
{
    lv_indev_t *device = lv_indev_create();
    if (!device)
        return false;
    lv_indev_set_type(device, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(device, pointer_read);
    return true;
}

int carplay_launcher_touch_start(uint16_t width, uint16_t height)
{
    return carplay_touch_panel_start(width, height, panel_touch, NULL);
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fusion_display.h"
#include "rtcp/input.h"
#include "rtcp/rtcp.h"
#include <string.h>

#ifdef RTCARPLAY_FUSION_POSIX
#include <pthread.h>
typedef int fusion_guard_t;
static pthread_mutex_t fusion_mutex = PTHREAD_MUTEX_INITIALIZER;

static fusion_guard_t fusion_lock(void)
{
    return pthread_mutex_lock(&fusion_mutex);
}

static void fusion_unlock(fusion_guard_t guard)
{
    if (guard == 0)
        pthread_mutex_unlock(&fusion_mutex);
}
#else
#include <rtthread.h>
typedef rt_base_t fusion_guard_t;

static fusion_guard_t fusion_lock(void)
{
    return rt_hw_interrupt_disable();
}

static void fusion_unlock(fusion_guard_t guard)
{
    rt_hw_interrupt_enable(guard);
}
#endif

static carplay_fusion_snapshot_t state;
static bool simulate = true;
static bool projection_hidden;
static bool resume_on_frame;

typedef struct
{
    carplay_fusion_touch_t held;
    uint32_t epoch;
    bool blocked;
} fusion_contact_t;

static fusion_contact_t contacts[RTCP_TOUCH_CONTACTS];

static void advance(uint32_t *value)
{
    if (++*value == 0)
        ++*value;
}
static carplay_display_mode_t effective(void)
{
    return state.video_active && !projection_hidden ? state.requested : CARPLAY_LOCAL;
}

void carplay_fusion_set_projection_hidden(bool hidden)
{
    fusion_guard_t guard = fusion_lock();
    resume_on_frame = false;
    if (projection_hidden != hidden)
    {
        projection_hidden = hidden;
        advance(&state.touch_epoch);
    }
    fusion_unlock(guard);
}

void carplay_fusion_resume_projection_on_frame(void)
{
    fusion_guard_t guard = fusion_lock();
    resume_on_frame = projection_hidden;
    fusion_unlock(guard);
}

bool carplay_fusion_resume_latched_projection(void)
{
    fusion_guard_t guard = fusion_lock();
    bool resumed = projection_hidden && resume_on_frame && state.video_active &&
                   state.video_generation == state.generation && state.requested != CARPLAY_LOCAL;
    if (resumed)
    {
        projection_hidden = false;
        resume_on_frame = false;
        advance(&state.touch_epoch);
    }
    fusion_unlock(guard);
    return resumed;
}

const char *carplay_fusion_mode_name(carplay_display_mode_t mode)
{
    static const char * const names[] = { "FULLSCREEN", "FUSION", "LOCAL" };
    return (unsigned)mode < 3 ? names[mode] : "INVALID";
}

int carplay_fusion_init(uint16_t width, uint16_t height)
{
    if (width < 512 || height < 320)
        return RTCP_EINVAL;
    fusion_guard_t guard = fusion_lock();
    int result = RTCP_OK;
    if (state.initialized)
    {
        if (state.width != width || state.height != height)
            result = RTCP_ESTATE;
    }
    else
    {
        state = (carplay_fusion_snapshot_t){ .initialized = true, .requested = CARPLAY_FUSION, .width = width, .height = height, .local_width = width / 4, .generation = 1, .touch_epoch = 1 };
    }
    fusion_unlock(guard);
    return result;
}

int carplay_fusion_set_mode(carplay_display_mode_t mode)
{
    if ((unsigned)mode > CARPLAY_LOCAL)
        return RTCP_EINVAL;
    fusion_guard_t guard = fusion_lock();
    int result = state.initialized ? RTCP_OK : RTCP_ESTATE;
    if (!result && state.requested != mode)
    {
        state.requested = mode;
        advance(&state.generation);
        advance(&state.touch_epoch);
    }
    fusion_unlock(guard);
    return result;
}

void carplay_fusion_snapshot(uint32_t now, carplay_fusion_snapshot_t *out)
{
    if (!out)
        return;
    fusion_guard_t guard = fusion_lock();
    *out = state;
    out->effective = effective();
    bool simulated = simulate;
    fusion_unlock(guard);
    if (simulated)
    {
        unsigned t = (now / 1000U) % 80U;
        out->vehicle = (carplay_vehicle_sample_t){ .speed_kph = (uint16_t)(t < 40 ? t * 2 : (80 - t) * 2),
                                                   .battery_percent = 82,
                                                   .available = true,
                                                   .simulated = true,
                                                   .gear = "D" };
    }
}

int carplay_fusion_set_vehicle(const carplay_vehicle_sample_t *sample)
{
    if (!sample || sample->battery_percent > 100 || !memchr(sample->gear, 0, sizeof(sample->gear)))
        return RTCP_EINVAL;
    for (unsigned i = 0; i < sizeof(sample->gear) && sample->gear[i]; ++i)
        if ((uint8_t)sample->gear[i] < 32 || (uint8_t)sample->gear[i] > 126)
            return RTCP_EINVAL;
    fusion_guard_t guard = fusion_lock();
    int result = state.initialized ? RTCP_OK : RTCP_ESTATE;
    if (!result)
    {
        state.vehicle = *sample;
        simulate = false;
    }
    fusion_unlock(guard);
    return result;
}

void carplay_fusion_use_simulated(void)
{
    fusion_guard_t guard = fusion_lock();
    simulate = true;
    fusion_unlock(guard);
}

bool carplay_fusion_video_target(uint16_t width, uint16_t height,
                                 rtcp_display_rect_t *rect, uint32_t *generation)
{
    if (!rect || !generation || !width || !height)
        return false;
    fusion_guard_t guard = fusion_lock();
    bool ready = state.initialized;
    if (ready)
    {
        if (width != state.width || height != state.height)
        {
            state.error = RTCP_EINVAL;
            if (state.requested != CARPLAY_LOCAL)
            {
                state.requested = CARPLAY_LOCAL;
                advance(&state.generation);
                advance(&state.touch_epoch);
            }
        }
        *rect = (rtcp_display_rect_t){ .width = width, .height = height };
        if (state.requested == CARPLAY_FUSION)
            rect->width = (uint16_t)(width - state.local_width);
        *generation = state.generation;
    }
    fusion_unlock(guard);
    return ready;
}

void carplay_fusion_video_latched(const rtcp_display_rect_t *rect, uint16_t width,
                                  uint16_t height, uint32_t generation, uint32_t now,
                                  bool new_frame)
{
    if (!rect || !rect->width || !rect->height || !width || !height)
        return;
    fusion_guard_t guard = fusion_lock();
    if (state.initialized)
    {
        bool resuming = resume_on_frame && new_frame;
        if (resuming || !state.video_active || memcmp(&state.video_rect, rect, sizeof(*rect)) ||
            state.logical_width != width || state.logical_height != height)
            advance(&state.touch_epoch);
        if (resuming)
        {
            projection_hidden = false;
            resume_on_frame = false;
        }
        state.video_rect = *rect;
        state.logical_width = width;
        state.logical_height = height;
        state.video_generation = generation;
        state.video_active = true;
        state.last_present_ms = now;
    }
    fusion_unlock(guard);
}

void carplay_fusion_video_stopped(void)
{
    fusion_guard_t guard = fusion_lock();
    if (state.video_active)
        advance(&state.touch_epoch);
    state.video_active = false;
    state.video_generation = 0;
    fusion_unlock(guard);
}

void carplay_fusion_display_error(int error)
{
    fusion_guard_t guard = fusion_lock();
    state.error = error;
    fusion_unlock(guard);
}

static carplay_fusion_touch_t release_contact(fusion_contact_t *contact, bool wait_for_up)
{
    carplay_fusion_touch_t action = contact->held;
    action.phase = RTCP_TOUCH_UP;
    if (wait_for_up)
        contact->blocked = true;
    else
        *contact = (fusion_contact_t){ 0 };
    return action;
}

carplay_fusion_touch_t carplay_fusion_cancel_touch(void)
{
    carplay_fusion_touch_t action = { 0 };
    fusion_guard_t guard = fusion_lock();
    for (unsigned index = 0; index < RTCP_TOUCH_CONTACTS; ++index)
    {
        fusion_contact_t *contact = &contacts[index];
        if (contact->held.owner != CARPLAY_TOUCH_NONE && !contact->blocked &&
            contact->epoch != state.touch_epoch)
        {
            action = release_contact(contact, true);
            break;
        }
    }
    fusion_unlock(guard);
    return action;
}

carplay_fusion_touch_t carplay_fusion_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y)
{
    carplay_fusion_touch_t action = { 0 };
    if (phase != RTCP_TOUCH_DOWN && phase != RTCP_TOUCH_MOVE && phase != RTCP_TOUCH_UP)
        return action;
    fusion_guard_t guard = fusion_lock();
    fusion_contact_t *contact = NULL;
    fusion_contact_t *free_slot = NULL;
    carplay_touch_owner_t active_owner = CARPLAY_TOUCH_NONE;
    bool current_phone_contact = false;
    for (unsigned index = 0; index < RTCP_TOUCH_CONTACTS; ++index)
    {
        fusion_contact_t *slot = &contacts[index];
        if (slot->held.owner == CARPLAY_TOUCH_NONE)
            free_slot = slot;
        else if (slot->held.id == id)
            contact = slot;
        if (!slot->blocked && slot->held.owner != CARPLAY_TOUCH_NONE)
        {
            active_owner = slot->held.owner;
            current_phone_contact = active_owner == CARPLAY_TOUCH_PHONE &&
                                    slot->epoch == state.touch_epoch;
        }
    }
    if (contact && contact->blocked)
    {
        if (phase == RTCP_TOUCH_UP)
            *contact = (fusion_contact_t){ 0 };
        goto done;
    }
    if (contact && contact->epoch != state.touch_epoch)
    {
        action = release_contact(contact, phase != RTCP_TOUCH_UP);
        goto done;
    }
    if (!state.initialized)
        goto done;
    if (x >= state.width || y >= state.height)
    {
        if (contact)
            action = release_contact(contact, phase != RTCP_TOUCH_UP);
        goto done;
    }
    uint16_t px = x, py = y;
    bool phone = state.video_active && state.video_generation == state.generation &&
                 effective() != CARPLAY_LOCAL && rtcp_display_map_point(&state.video_rect, state.logical_width, state.logical_height, x, y, &px, &py);
    if (!contact)
    {
        if (!free_slot || phase != RTCP_TOUCH_DOWN)
            goto done;
        if (phone && active_owner != CARPLAY_TOUCH_LOCAL)
        {
            free_slot->held.owner = CARPLAY_TOUCH_PHONE;
            if (current_phone_contact)
                ++state.two_finger_starts;
        }
        else if (active_owner == CARPLAY_TOUCH_NONE &&
                 (effective() == CARPLAY_LOCAL ||
                  (effective() == CARPLAY_FUSION && x >= state.width - state.local_width)))
            free_slot->held.owner = CARPLAY_TOUCH_LOCAL;
        else
            goto done;
        free_slot->held.id = id;
        free_slot->epoch = state.touch_epoch;
        contact = free_slot;
    }
    if (contact->held.owner == CARPLAY_TOUCH_PHONE && !phone)
    {
        action = release_contact(contact, phase != RTCP_TOUCH_UP);
        goto done;
    }
    contact->held.phase = phase;
    contact->held.x = contact->held.owner == CARPLAY_TOUCH_PHONE ? px : x;
    contact->held.y = contact->held.owner == CARPLAY_TOUCH_PHONE ? py : y;
    action = contact->held;
    if (phase == RTCP_TOUCH_UP)
        *contact = (fusion_contact_t){ 0 };
done:
    fusion_unlock(guard);
    return action;
}

#ifndef RTCARPLAY_FUSION_POSIX
static int cmd_carplay_display(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "simulate"))
        carplay_fusion_use_simulated();
    else if (argc == 2 && strcmp(argv[1], "status"))
    {
        carplay_display_mode_t mode;
        if (!strcmp(argv[1], "fullscreen"))
            mode = CARPLAY_FULLSCREEN;
        else if (!strcmp(argv[1], "fusion"))
            mode = CARPLAY_FUSION;
        else if (!strcmp(argv[1], "local"))
            mode = CARPLAY_LOCAL;
        else
            return RTCP_EINVAL;
        int result = carplay_fusion_set_mode(mode);
        if (result)
            return result;
    }
    else if (argc != 2)
    {
        rt_kprintf("carplay_display status|fullscreen|fusion|local|simulate\n");
        return RTCP_EINVAL;
    }
    carplay_fusion_snapshot_t s;
    carplay_fusion_snapshot(rt_tick_get_millisecond(), &s);
    rt_kprintf("[carplay-display] requested=%s effective=%s generation=%u latched=%u video=%u error=%d\n",
               carplay_fusion_mode_name(s.requested), carplay_fusion_mode_name(s.effective),
               s.generation, s.video_generation, s.video_active, s.error);
    rt_kprintf("[carplay-display] rect=%u,%u %ux%u logical=%ux%u vehicle=%s speed=%u gear=%s battery=%u%%\n",
               s.video_rect.x, s.video_rect.y, s.video_rect.width, s.video_rect.height, s.logical_width, s.logical_height,
               s.vehicle.simulated ? "SIMULATED" : s.vehicle.available ? "PROVIDER"
                                                                       : "UNAVAILABLE",
               s.vehicle.speed_kph, s.vehicle.gear, s.vehicle.battery_percent);
    rt_kprintf("[carplay-display] two_finger_starts=%u\n", (unsigned)s.two_finger_starts);
    return RTCP_OK;
}
MSH_CMD_EXPORT_ALIAS(cmd_carplay_display, carplay_display, Single screen CarPlay display modes and vehicle data);
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_FUSION_DISPLAY_H
#define CARPLAY_FUSION_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>
#include "rtcp/display.h"

typedef enum
{
    CARPLAY_FULLSCREEN,
    CARPLAY_FUSION,
    CARPLAY_LOCAL
} carplay_display_mode_t;
typedef struct
{
    uint16_t speed_kph;
    uint8_t battery_percent;
    char gear[8];
    bool available, simulated;
} carplay_vehicle_sample_t;
typedef struct
{
    bool initialized, video_active;
    carplay_display_mode_t requested, effective;
    uint16_t width, height, local_width, logical_width, logical_height;
    rtcp_display_rect_t video_rect;
    uint32_t generation, video_generation, touch_epoch, last_present_ms;
    int error;
    carplay_vehicle_sample_t vehicle;
} carplay_fusion_snapshot_t;
typedef enum
{
    CARPLAY_TOUCH_NONE,
    CARPLAY_TOUCH_LOCAL,
    CARPLAY_TOUCH_PHONE
} carplay_touch_owner_t;
typedef struct
{
    carplay_touch_owner_t owner;
    uint8_t phase;
    uint16_t id, x, y;
} carplay_fusion_touch_t;

/* Application-only, single-core D213 coordination. Each call copies bounded
 * state with interrupts masked; no LVGL, device ioctl or callback in that span.
 * Initialize once from the GUI before starting the connection owner. */
int carplay_fusion_init(uint16_t width, uint16_t height);
int carplay_fusion_set_mode(carplay_display_mode_t);
void carplay_fusion_snapshot(uint32_t now_ms, carplay_fusion_snapshot_t *);
/* Real vehicle integration supplies samples here; no guessed CAN mappings.
 * The sample is copied. Default data is explicitly simulated until replaced. */
int carplay_fusion_set_vehicle(const carplay_vehicle_sample_t *);
void carplay_fusion_use_simulated(void);
/* Decoder-worker geometry and latch observations. LOCAL keeps any connected
 * video behind opaque local UI; disconnected LOCAL has no decoder workload. */
bool carplay_fusion_video_target(uint16_t panel_width, uint16_t panel_height,
                                 rtcp_display_rect_t *, uint32_t *generation);
void carplay_fusion_video_latched(const rtcp_display_rect_t *, uint16_t logical_width,
                                  uint16_t logical_height, uint32_t generation, uint32_t now_ms);
void carplay_fusion_video_stopped(void);
void carplay_fusion_display_error(int error);
/* Single-pointer gesture ownership. A changed layout/session cancels a held
 * gesture; a phone gesture leaving its latched video rectangle releases at
 * its last valid position. The remainder is swallowed until physical UP.
 * GUI calls cancel periodically even when the finger has not moved. */
carplay_fusion_touch_t carplay_fusion_touch(uint8_t phase, uint16_t id, uint16_t x, uint16_t y);
carplay_fusion_touch_t carplay_fusion_cancel_touch(void);
const char *carplay_fusion_mode_name(carplay_display_mode_t);
#endif

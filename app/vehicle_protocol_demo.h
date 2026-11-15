/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_VEHICLE_PROTOCOL_DEMO_H
#define CARPLAY_VEHICLE_PROTOCOL_DEMO_H
#include "rtcp/receiver.h"

/* Explicit protocol bench. These are simulated combustion-engine RPM values,
 * never road speed, gear, battery SOC or a physical vehicle provider. Owner only. */
typedef struct {
    rtcp_vehicle_t *model;
    uint64_t sample_slot;
} carplay_vehicle_protocol_demo_t;
typedef struct {
    uint32_t rpm;
    int sample_error;
    rtcp_vehicle_status_t model;
    rtcp_vehicle_delivery_status_t delivery;
    rtcp_data_stream_status_t normal, high;
} carplay_vehicle_protocol_snapshot_t;

int carplay_vehicle_protocol_demo_create(carplay_vehicle_protocol_demo_t *);
void carplay_vehicle_protocol_demo_destroy(carplay_vehicle_protocol_demo_t *);
/* 250 ms simulation cadence; not a protocol deadline or presentation clock. */
int carplay_vehicle_protocol_demo_tick(carplay_vehicle_protocol_demo_t *, uint64_t now_ms);
/* Copy only on the Receiver owner. Publish under the application's lock before
 * allowing another thread to inspect this snapshot. Never publish model pointers. */
void carplay_vehicle_protocol_demo_snapshot(const carplay_vehicle_protocol_demo_t *,
    rtcp_receiver_t *, carplay_vehicle_protocol_snapshot_t *);
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI).
 * Catalogue reference: vehicle_state/engines.rs and REVOLUTIONS_PER_MINUTE
 * in vehicle_state.rs, revision 64a05b68358db52ed887169143afd5944fd6f1f7.
 * Only the supported combustion-engine metadata is re-expressed here. See
 * ../docs/vehicle-protocol-demo.md and the bundled rt-carplay NOTICE.
 */
#include "vehicle_protocol_demo.h"
#include <string.h>

#define ENGINE_RPM 2562
#define RPM_MAX 6000
#define RPM_REDLINE 5000

int carplay_vehicle_protocol_demo_create(carplay_vehicle_protocol_demo_t *demo)
{
    if (!demo) return RTCP_EINVAL;
    if (demo->model) return RTCP_ESTATE;
    rtcp_vehicle_characteristic_t characteristics[3] = {0};
    const int64_t types[] = {0x30000024, 0x3000004E, 0x3000004F};
    const int64_t initial[] = {0, RPM_MAX, RPM_REDLINE};
    for (size_t i=0; i<3; ++i) {
        characteristics[i] = (rtcp_vehicle_characteristic_t){
            .accessory_iid=2560, .accessory_type=0x09000005,
            .service_iid=2561, .service_type=0x1A00000F,
            .iid=ENGINE_RPM+(int64_t)i, .type=types[i],
            .format=RTCP_VEHICLE_UINT32, .owner=RTCP_VEHICLE_CAR,
            .mutable_value=i==0, .has_range=true, .minimum=0,
            .maximum=RPM_MAX, .step=1, .has_units=true, .units=981,
            .has_initial=true,
            .initial={.format=RTCP_VEHICLE_UINT32, .as.integer=initial[i]}
        };
    }
    const rtcp_vehicle_config_t config = {
        .struct_size=sizeof(config), .plugin_name="rt-carplay SIMULATED",
        .plugin_id=0, .characteristics=characteristics, .count=3
    };
    int rc=rtcp_vehicle_create(&config, &demo->model);
    if (!rc) demo->sample_slot=UINT64_MAX;
    return rc;
}

void carplay_vehicle_protocol_demo_destroy(carplay_vehicle_protocol_demo_t *demo)
{
    if (!demo) return;
    rtcp_vehicle_destroy(demo->model);
    memset(demo,0,sizeof(*demo));
}

int carplay_vehicle_protocol_demo_tick(carplay_vehicle_protocol_demo_t *demo, uint64_t now_ms)
{
    if (!demo || !demo->model) return RTCP_EINVAL;
    uint64_t slot=now_ms/250U;
    if (slot==demo->sample_slot) return RTCP_OK;
    uint32_t phase=(uint32_t)(slot%40U);
    rtcp_vehicle_value_t value={.format=RTCP_VEHICLE_UINT32,
        .as.integer=(phase<=20U ? phase : 40U-phase)*250U};
    int rc=rtcp_vehicle_set(demo->model,ENGINE_RPM,&value);
    if (!rc) demo->sample_slot=slot;
    return rc;
}

void carplay_vehicle_protocol_demo_snapshot(const carplay_vehicle_protocol_demo_t *demo,
    rtcp_receiver_t *receiver, carplay_vehicle_protocol_snapshot_t *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!demo || !demo->model) return;
    rtcp_vehicle_value_t value; bool present=false;
    if (!rtcp_vehicle_get(demo->model,ENGINE_RPM,&value,&present) && present)
        out->rpm=(uint32_t)value.as.integer;
    (void)rtcp_vehicle_status(demo->model,&out->model);
    if (!receiver) return;
    (void)rtcp_receiver_get_vehicle_delivery_status(receiver,&out->delivery);
    (void)rtcp_receiver_get_data_stream_status(receiver,RTCP_DATA_STREAM_VEHICLE,&out->normal);
    (void)rtcp_receiver_get_data_stream_status(receiver,RTCP_DATA_STREAM_VEHICLE_HIGH,&out->high);
}

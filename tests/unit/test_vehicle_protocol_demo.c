/* SPDX-License-Identifier: GPL-3.0-or-later
 * Based on LIVI by Lasse Heitgres (https://github.com/f-io/LIVI).
 * Host-only tests of the application protocol bench; not iPhone evidence.
 */
#include "vehicle_protocol_demo.h"
#include "rtcp/opack.h"
#include "airplay_info_plist.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static rtcp_opack_node_t nodes[512], decoded[512];
static uint8_t input[16384], output[16384], inner[4096];
static size_t used, count;
static size_t add(rtcp_opack_type_t type)
{
    size_t i = used++;
    assert(used <= 512);
    nodes[i] = (rtcp_opack_node_t){ .type = type, .end = (uint16_t)used };
    return i;
}
static void text(const char *s)
{
    size_t i = add(RTCP_OPACK_STRING);
    nodes[i].value.blob.bytes = (const uint8_t *)s;
    nodes[i].value.blob.length = strlen(s);
}
static void number(int64_t n)
{
    nodes[add(RTCP_OPACK_INT)].value.integer = n;
}
static size_t request(const char *command, bool write)
{
    used = 0;
    size_t root = add(RTCP_OPACK_DICT);
    text("command");
    text(command);
    text("transactionID");
    number(17);
    text("values");
    size_t values = add(write ? RTCP_OPACK_DICT : RTCP_OPACK_ARRAY);
    for (int i = 0; i < 3; ++i)
    {
        number(2562 + i);
        if (write)
            number(1234);
    }
    nodes[values].count = 3;
    nodes[values].end = (uint16_t)used;
    nodes[root].count = 3;
    nodes[root].end = (uint16_t)used;
    size_t length;
    assert(rtcp_opack_encode(nodes, used, inner, sizeof(inner), &length) == RTCP_OPACK_OK);
    used = 0;
    root = add(RTCP_OPACK_DICT);
    text("pluginID");
    number(0);
    text("pluginData");
    size_t data = add(RTCP_OPACK_DATA);
    nodes[data].value.blob.bytes = inner;
    nodes[data].value.blob.length = length;
    nodes[root].count = 2;
    nodes[root].end = (uint16_t)used;
    assert(rtcp_opack_encode(nodes, used, input, sizeof(input), &length) == RTCP_OPACK_OK);
    return length;
}
static size_t key(const char *s)
{
    size_t i = rtcp_opack_dict_get(decoded, count, 0, s);
    assert(i != SIZE_MAX);
    return i;
}
static void open_reply(size_t length, const char *command)
{
    assert(rtcp_opack_decode(output, length, decoded, 512, &count) == RTCP_OPACK_OK);
    assert(decoded[key("pluginID")].value.integer == 0);
    size_t data = key("pluginData");
    const uint8_t *bytes = decoded[data].value.blob.bytes;
    size_t n = decoded[data].value.blob.length;
    assert(rtcp_opack_decode(bytes, n, decoded, 512, &count) == RTCP_OPACK_OK);
    size_t cmd = key("command");
    assert(decoded[cmd].type == RTCP_OPACK_STRING && decoded[cmd].value.blob.length == strlen(command));
    assert(!memcmp(decoded[cmd].value.blob.bytes, command, strlen(command)));
}
static int64_t entry(const char *dictionary, int64_t iid)
{
    size_t at = key(dictionary);
    assert(decoded[at].type == RTCP_OPACK_DICT);
    size_t n = decoded[at].count;
    ++at;
    for (size_t i = 0; i < n; ++i)
    {
        size_t value = decoded[at].end;
        if (decoded[at].value.integer == iid)
        {
            assert(decoded[value].type == RTCP_OPACK_INT);
            return decoded[value].value.integer;
        }
        at = decoded[value].end;
    }
    assert(!"missing characteristic");
    return -1;
}
static int64_t read_value(rtcp_vehicle_t *model, int64_t iid)
{
    rtcp_vehicle_value_t value;
    bool present = false;
    assert(rtcp_vehicle_get(model, iid, &value, &present) == RTCP_OK && present);
    assert(value.format == RTCP_VEHICLE_UINT32);
    return value.as.integer;
}
int main(int argc, char **argv)
{
    carplay_vehicle_protocol_demo_t demo = { 0 };
    assert(carplay_vehicle_protocol_demo_create(NULL) == RTCP_EINVAL);
    assert(carplay_vehicle_protocol_demo_create(&demo) == RTCP_OK);
    assert(carplay_vehicle_protocol_demo_create(&demo) == RTCP_ESTATE);
    assert(read_value(demo.model, 2562) == 0);
    assert(read_value(demo.model, 2563) == 6000 && read_value(demo.model, 2564) == 5000);
    rtcp_vehicle_value_t bad = { .format = RTCP_VEHICLE_UINT32, .as.integer = 1 };
    assert(rtcp_vehicle_set(demo.model, 2563, &bad) == RTCP_ESTATE);
    assert(rtcp_vehicle_set(demo.model, 2564, &bad) == RTCP_ESTATE);

    size_t length = request("registerRequest", false), reply = 0;
    assert(rtcp_vehicle_handle(demo.model, RTCP_VEHICLE_NORMAL, input, length, output, sizeof(output), &reply) == RTCP_OK);
    open_reply(reply, "registerResponse");
    assert(entry("values", 2562) == 0 && entry("values", 2563) == 6000 && entry("values", 2564) == 5000);
    assert(carplay_vehicle_protocol_demo_tick(&demo, 250) == RTCP_OK);
    assert(read_value(demo.model, 2562) == 250);
    assert(carplay_vehicle_protocol_demo_tick(&demo, 499) == RTCP_OK);
    assert(read_value(demo.model, 2562) == 250);
    rtcp_vehicle_lane_t lane;
    assert(rtcp_vehicle_notification(demo.model, &lane, output, sizeof(output), &reply) == RTCP_OK);
    assert(lane == RTCP_VEHICLE_HIGH);
    open_reply(reply, "updateNotify");
    assert(decoded[key("values")].count == 1 && entry("values", 2562) == 250);
    assert(rtcp_vehicle_notification(demo.model, &lane, output, sizeof(output), &reply) == RTCP_ENOENT);

    length = request("writeRequest", true);
    assert(rtcp_vehicle_handle(demo.model, RTCP_VEHICLE_HIGH, input, length, output, sizeof(output), &reply) == RTCP_OK);
    open_reply(reply, "writeResponse");
    assert(entry("errors", 2562) == -6727 && entry("errors", 2563) == -6727 && entry("errors", 2564) == -6727);
    assert(read_value(demo.model, 2562) == 250);
    assert(carplay_vehicle_protocol_demo_tick(&demo, 5000) == RTCP_OK && read_value(demo.model, 2562) == 5000);
    assert(carplay_vehicle_protocol_demo_tick(&demo, 5250) == RTCP_OK && read_value(demo.model, 2562) == 4750);
    assert(carplay_vehicle_protocol_demo_tick(&demo, 10000) == RTCP_OK && read_value(demo.model, 2562) == 0);
    rtcp_vehicle_reset_session(demo.model);
    assert(carplay_vehicle_protocol_demo_tick(&demo, 10250) == RTCP_OK && read_value(demo.model, 2562) == 250);
    assert(rtcp_vehicle_notification(demo.model, &lane, output, sizeof(output), &reply) == RTCP_ENOENT);
    carplay_vehicle_protocol_snapshot_t snapshot;
    carplay_vehicle_protocol_demo_snapshot(&demo, NULL, &snapshot);
    assert(snapshot.rpm == 250 && snapshot.model.characteristics == 3 && snapshot.model.subscribed == 0);
    assert(snapshot.model.refused_writes == 3 && !snapshot.delivery.attached);
    assert(snapshot.model.method_requests[RTCP_VEHICLE_METHOD_REGISTER] == 1);
    assert(snapshot.model.method_responses[RTCP_VEHICLE_METHOD_WRITE] == 1);
    assert(snapshot.model.last_method == RTCP_VEHICLE_METHOD_WRITE && snapshot.model.last_result == RTCP_OK);
    assert(snapshot.model.last_lane == RTCP_VEHICLE_HIGH); /* Response contains per-IID refusals. */
    carplay_vehicle_protocol_demo_destroy(&demo);
    assert(!demo.model);
    carplay_vehicle_protocol_demo_destroy(&demo);
    assert(carplay_vehicle_protocol_demo_tick(&demo, 0) == RTCP_EINVAL);
    assert(carplay_vehicle_protocol_demo_create(&demo) == RTCP_OK);
    assert(read_value(demo.model, 2562) == 0);
    if (argc == 2)
    {
        airplay_info_plist_config_t info = { .device_id = "02:00:00:00:00:01",
                                             .name = "SIMULATED protocol bench",
                                             .width_pixels = 1024,
                                             .height_pixels = 600,
                                             .vehicle = demo.model };
        size_t bytes;
        assert(airplay_info_plist_build(&info, output, sizeof(output), &bytes));
        FILE *file = fopen(argv[1], "wb");
        assert(file);
        assert(fwrite(output, 1, bytes, file) == bytes && fclose(file) == 0);
    }
    carplay_vehicle_protocol_demo_destroy(&demo);
    puts("SIMULATED engine Vehicle State bench: PASS (Host only)");
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_GNSS_DEMO_H
#define CARPLAY_GNSS_DEMO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "rtcp/navigation.h"

#define CARPLAY_GNSS_MAX_SPEED_KMH 300U

typedef struct
{
    int32_t latitude, longitude; /* Signed millionths of a degree. */
    int32_t end_latitude, end_longitude;
    unsigned speed_kmh, revision;
    bool moving;
    bool enabled;
} carplay_gnss_config_t;

typedef enum
{
    CARPLAY_GNSS_FIXED,
    CARPLAY_GNSS_MOVING,
    CARPLAY_GNSS_ARRIVED
} carplay_gnss_motion_t;

typedef struct
{
    int32_t latitude, longitude;
    unsigned speed_knots_centi, course_deci;
    carplay_gnss_motion_t motion;
} carplay_gnss_fix_t;

typedef struct
{
    bool tunnel, advertised, fixture_clock;
    carplay_gnss_motion_t motion;
    unsigned requested, gga_queued, rmc_queued, pascd_queued, failed;
} carplay_gnss_status_t;

bool carplay_gnss_get_config(carplay_gnss_config_t *);
bool carplay_gnss_valid_config(const carplay_gnss_config_t *);
bool carplay_gnss_set_config(const carplay_gnss_config_t *);
bool carplay_gnss_parse(const char *, unsigned limit, int32_t *);
void carplay_gnss_format_coordinate(int32_t, char *, size_t);
carplay_gnss_fix_t carplay_gnss_sample(const carplay_gnss_config_t *, uint64_t elapsed_ms);
size_t carplay_gnss_sentence(const carplay_gnss_fix_t *, unsigned kind,
                             const struct tm *utc, char out[RTCP_LOCATION_NMEA_MAX + 1U]);
void carplay_gnss_get_status(carplay_gnss_status_t *);
void carplay_gnss_publish_status(const carplay_gnss_status_t *);

#endif

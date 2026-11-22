/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gnss_demo.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

#define DEGREE_SCALE        1000000
#define MAX_SENTENCE        RTCP_LOCATION_NMEA_MAX
#define PI                  3.14159265358979323846
#define RAD_PER_MICRODEGREE (PI / (180.0 * DEGREE_SCALE))
#define EARTH_RADIUS_M      6371008.8

static carplay_gnss_config_t config = {
    .latitude = 31190000,
    .longitude = 121436000,
    .end_latitude = 31190000,
    .end_longitude = 121437000,
    .speed_kmh = 36,
    .revision = 1
};
static atomic_flag config_lock = ATOMIC_FLAG_INIT;
static atomic_uint status_flags, gga_queued, rmc_queued, failed;

bool carplay_gnss_get_config(carplay_gnss_config_t *out)
{
    if (!out || atomic_flag_test_and_set_explicit(&config_lock, memory_order_acquire))
        return false;
    *out = config;
    atomic_flag_clear_explicit(&config_lock, memory_order_release);
    return true;
}

static bool valid_coordinate(int32_t latitude, int32_t longitude)
{
    return latitude >= -90 * DEGREE_SCALE && latitude <= 90 * DEGREE_SCALE &&
           longitude >= -180 * DEGREE_SCALE && longitude <= 180 * DEGREE_SCALE;
}

bool carplay_gnss_valid_config(const carplay_gnss_config_t *next)
{
    if (!next || !valid_coordinate(next->latitude, next->longitude))
        return false;
    if (!next->moving)
        return true;
    if (!valid_coordinate(next->end_latitude, next->end_longitude) || !next->speed_kmh ||
        next->speed_kmh > CARPLAY_GNSS_MAX_SPEED_KMH)
        return false;
    int32_t longitude_gap = next->end_longitude - next->longitude;
    bool pole = next->latitude == 90 * DEGREE_SCALE || next->latitude == -90 * DEGREE_SCALE;
    bool same = next->latitude == next->end_latitude &&
                (pole || longitude_gap == 0 || longitude_gap == 360 * DEGREE_SCALE ||
                 longitude_gap == -360 * DEGREE_SCALE);
    bool antipodal = next->latitude == -next->end_latitude &&
                     (pole || longitude_gap == 180 * DEGREE_SCALE || longitude_gap == -180 * DEGREE_SCALE);
    return !same && !antipodal;
}

bool carplay_gnss_set_config(const carplay_gnss_config_t *next)
{
    if (!carplay_gnss_valid_config(next) ||
        atomic_flag_test_and_set_explicit(&config_lock, memory_order_acquire))
        return false;
    unsigned revision = config.revision + 1U;
    config = *next;
    config.revision = revision ? revision : 1U;
    atomic_flag_clear_explicit(&config_lock, memory_order_release);
    return true;
}

bool carplay_gnss_parse(const char *text, unsigned limit, int32_t *out)
{
    if (!text || !out || limit > 180U)
        return false;
    bool negative = *text == '-';
    if (negative || *text == '+')
        ++text;
    if (*text < '0' || *text > '9')
        return false;
    unsigned degrees = 0;
    while (*text >= '0' && *text <= '9')
    {
        degrees = degrees * 10U + (unsigned)(*text++ - '0');
        if (degrees > limit)
            return false;
    }
    unsigned fraction = 0, scale = DEGREE_SCALE;
    if (*text == '.')
    {
        ++text;
        if (*text < '0' || *text > '9')
            return false;
        while (*text >= '0' && *text <= '9' && scale > 1U)
        {
            scale /= 10U;
            fraction += (unsigned)(*text++ - '0') * scale;
        }
    }
    if (*text || (degrees == limit && fraction))
        return false;
    int32_t magnitude = (int32_t)(degrees * DEGREE_SCALE + fraction);
    *out = negative ? -magnitude : magnitude;
    return true;
}

void carplay_gnss_format_coordinate(int32_t value, char *out, size_t capacity)
{
    unsigned magnitude = (unsigned)(value < 0 ? -(int64_t)value : value);
    snprintf(out, capacity, "%s%u.%06u", value < 0 ? "-" : "",
             magnitude / DEGREE_SCALE, magnitude % DEGREE_SCALE);
}

static void coordinate_nmea(int32_t value, bool latitude, char out[16])
{
    unsigned magnitude = (unsigned)(value < 0 ? -value : value);
    unsigned minutes = (magnitude % DEGREE_SCALE) * 60U;
    char hemisphere = latitude ? (value < 0 ? 'S' : 'N') : (value < 0 ? 'W' : 'E');
    snprintf(out, 16, "%0*u%02u.%05u,%c", latitude ? 2 : 3, magnitude / DEGREE_SCALE,
             minutes / DEGREE_SCALE, minutes / 10U % 100000U, hemisphere);
}

static double bearing_to(double latitude, double end_latitude, double longitude_delta)
{
    return atan2(sin(longitude_delta) * cos(end_latitude),
                 cos(latitude) * sin(end_latitude) -
                     sin(latitude) * cos(end_latitude) * cos(longitude_delta));
}

carplay_gnss_fix_t carplay_gnss_sample(const carplay_gnss_config_t *route, uint64_t elapsed_ms)
{
    carplay_gnss_fix_t fix = { .latitude = route->latitude, .longitude = route->longitude };
    if (!route->moving)
        return fix;

    double latitude = route->latitude * RAD_PER_MICRODEGREE;
    double longitude = route->longitude * RAD_PER_MICRODEGREE;
    double end_latitude = route->end_latitude * RAD_PER_MICRODEGREE;
    double longitude_delta = remainder((route->end_longitude - route->longitude) *
                                           RAD_PER_MICRODEGREE,
                                       2.0 * PI);
    double east = sin(longitude_delta) * cos(end_latitude);
    double north = cos(latitude) * sin(end_latitude) -
                   sin(latitude) * cos(end_latitude) * cos(longitude_delta);
    double central_angle = atan2(hypot(east, north), sin(latitude) * sin(end_latitude) +
                                                         cos(latitude) * cos(end_latitude) * cos(longitude_delta));
    double travelled = (double)elapsed_ms * route->speed_kmh / (3600.0 * EARTH_RADIUS_M);
    if (travelled >= central_angle)
    {
        fix.latitude = route->end_latitude;
        fix.longitude = route->end_longitude;
        fix.motion = CARPLAY_GNSS_ARRIVED;
        return fix;
    }

    double course = atan2(east, north);
    if (elapsed_ms)
    {
        double along_meridian = sin(travelled) * cos(course);
        double horizontal_x = cos(latitude) * cos(travelled) - sin(latitude) * along_meridian;
        double horizontal_y = sin(travelled) * sin(course);
        double vertical = sin(latitude) * cos(travelled) + cos(latitude) * along_meridian;
        double current_latitude = atan2(vertical, hypot(horizontal_x, horizontal_y));
        double offset = atan2(horizontal_y, horizontal_x);
        double current_longitude = remainder(longitude + offset, 2.0 * PI);
        fix.latitude = (int32_t)lround(current_latitude / RAD_PER_MICRODEGREE);
        fix.longitude = (int32_t)lround(current_longitude / RAD_PER_MICRODEGREE);
        course = bearing_to(current_latitude, end_latitude, longitude_delta - offset);
    }
    fix.speed_knots_centi = (route->speed_kmh * 100000U + 926U) / 1852U;
    fix.course_deci = (unsigned)lround((course < 0 ? course + 2.0 * PI : course) * (1800.0 / PI)) % 3600U;
    fix.motion = CARPLAY_GNSS_MOVING;
    return fix;
}

size_t carplay_gnss_sentence(const carplay_gnss_fix_t *sample, unsigned kind,
                             const struct tm *utc, char out[MAX_SENTENCE + 1U])
{
    if (!sample || !utc || !out || !valid_coordinate(sample->latitude, sample->longitude))
        return 0;
    char latitude[16], longitude[16], clock[7];
    coordinate_nmea(sample->latitude, true, latitude);
    coordinate_nmea(sample->longitude, false, longitude);
    if (snprintf(clock, sizeof(clock), "%02d%02d%02d", utc->tm_hour, utc->tm_min, utc->tm_sec) != 6)
        return 0;
    int length;
    if (kind == RTCP_LOCATION_GGA)
        length = snprintf(out, MAX_SENTENCE + 1U, "$GPGGA,%s,%s,%s,1,08,1.0,0.0,M,0.0,M,,",
                          clock, latitude, longitude);
    else if (kind == RTCP_LOCATION_RMC)
        length = snprintf(out, MAX_SENTENCE + 1U, "$GPRMC,%s,A,%s,%s,%u.%02u,%u.%u,%02d%02d%02d,,,A",
                          clock, latitude, longitude, sample->speed_knots_centi / 100U,
                          sample->speed_knots_centi % 100U, sample->course_deci / 10U,
                          sample->course_deci % 10U, utc->tm_mday, utc->tm_mon + 1, utc->tm_year % 100);
    else
        return 0;
    if (length < 1 || length > (int)MAX_SENTENCE - 3)
        return 0;
    unsigned checksum = 0;
    for (int offset = 1; offset < length; ++offset)
        checksum ^= (unsigned char)out[offset];
    snprintf(out + length, 4, "*%02X", checksum);
    return (size_t)length + 3U;
}

void carplay_gnss_publish_status(const carplay_gnss_status_t *status)
{
    atomic_store(&gga_queued, status->gga_queued);
    atomic_store(&rmc_queued, status->rmc_queued);
    atomic_store(&failed, status->failed);
    atomic_store(&status_flags, status->requested | (status->tunnel ? 4U : 0U) |
                                    (status->advertised ? 8U : 0U) | (status->fixture_clock ? 16U : 0U) |
                                    ((unsigned)status->motion << 5));
}

void carplay_gnss_get_status(carplay_gnss_status_t *out)
{
    unsigned flags = atomic_load(&status_flags);
    *out = (carplay_gnss_status_t){
        .tunnel = (flags & 4U) != 0,
        .advertised = (flags & 8U) != 0,
        .fixture_clock = (flags & 16U) != 0,
        .motion = (carplay_gnss_motion_t)(flags >> 5),
        .requested = flags & (RTCP_LOCATION_GGA | RTCP_LOCATION_RMC),
        .gga_queued = atomic_load(&gga_queued),
        .rmc_queued = atomic_load(&rmc_queued),
        .failed = atomic_load(&failed),
    };
}

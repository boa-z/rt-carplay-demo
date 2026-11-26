#ifndef CARPLAY_WIFI_STA_LUBANLITE_H
#define CARPLAY_WIFI_STA_LUBANLITE_H

#include <stdbool.h>
#include <stdint.h>

#define CARPLAY_WIFI_SCAN_MAX 12

typedef struct
{
    char ssid[33];
    char security[12];
    int channel, rssi;
} carplay_wifi_access_point_t;

typedef struct
{
    bool enabled, busy, connected, addressed, ap_active;
    unsigned scan_revision, count;
    char connected_ssid[33];
    char message[96];
    carplay_wifi_access_point_t results[CARPLAY_WIFI_SCAN_MAX];
} carplay_wifi_status_t;

void carplay_wifi_status(carplay_wifi_status_t *out);
int carplay_wifi_enable(bool enabled);
int carplay_wifi_scan(void);
int carplay_wifi_join(unsigned revision, unsigned index, const char *password);
int carplay_wifi_forget(void);

#endif

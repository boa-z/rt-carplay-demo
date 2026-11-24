/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Firmware version and CPIO A/B update service.
 *
 * The launcher only reads a snapshot; every blocking step (TLS handshake,
 * HTTP transfer, flash write) runs in the OTA worker thread so the LVGL task
 * never stalls.
 */
#ifndef RTCARPLAY_OTA_LUBANLITE_H
#define RTCARPLAY_OTA_LUBANLITE_H

#include <rtthread.h>
#include <stdbool.h>
#include <stdint.h>

#define CARPLAY_OTA_SOURCE_MAX 256

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    CARPLAY_OTA_IDLE = 0,
    CARPLAY_OTA_DOWNLOADING,
    CARPLAY_OTA_VERIFYING,
    CARPLAY_OTA_READY_TO_INSTALL,
    CARPLAY_OTA_APPLYING,
    CARPLAY_OTA_REBOOTING,
    CARPLAY_OTA_TRIAL_BOOT,
    CARPLAY_OTA_CONFIRMED,
    CARPLAY_OTA_FAILED,
} carplay_ota_state_t;

typedef struct
{
    carplay_ota_state_t state;
    int progress;            /* 0..100 while downloading/applying, -1 unknown */
    char current[24];        /* version compiled into the running image */
    char current_commit[48]; /* full source commit, optionally -dirty */
    char remote[24];         /* version in the verified image */
    char slot[8];            /* active A/B system slot, "A" or "B" */
    char message[112];       /* human-readable detail for the settings page */
    int busy;                /* includes worker cleanup after terminal state */
    uint32_t candidate;
    char source[CARPLAY_OTA_SOURCE_MAX];
} carplay_ota_status_t;

/* Copy the current state; safe to call from the LVGL timer. */
void carplay_ota_status(carplay_ota_status_t *status);
void carplay_ota_boot_health_ready(void);

int carplay_ota_download(void);
int carplay_ota_install(uint32_t candidate);
int carplay_ota_cancel(void);
int carplay_ota_source_set(const char *url);
bool carplay_ota_file_write_begin(void);
void carplay_ota_file_write_end(void);

#ifdef __cplusplus
}
#endif

#endif /* RTCARPLAY_OTA_LUBANLITE_H */

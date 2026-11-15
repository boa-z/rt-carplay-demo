/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Firmware version and wired-network A/B OTA service.
 *
 * The launcher only reads a snapshot; every blocking step (TLS handshake,
 * HTTP transfer, flash write) runs in the OTA worker thread so the LVGL task
 * never stalls.
 */
#ifndef RTCARPLAY_OTA_LUBANLITE_H
#define RTCARPLAY_OTA_LUBANLITE_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARPLAY_OTA_IDLE = 0,
    CARPLAY_OTA_CHECKING,
    CARPLAY_OTA_UP_TO_DATE,
    CARPLAY_OTA_AVAILABLE,
    CARPLAY_OTA_DOWNLOADING,
    CARPLAY_OTA_APPLYING,
    CARPLAY_OTA_REBOOTING,
    CARPLAY_OTA_FAILED,
} carplay_ota_state_t;

typedef struct {
    carplay_ota_state_t state;
    int progress;            /* 0..100 while downloading/applying, -1 unknown */
    char current[24];        /* version compiled into the running image */
    char current_commit[48]; /* full source commit, optionally -dirty */
    char remote[24];         /* version advertised by the release */
    char remote_commit[41];  /* target identity confirmed before install */
    char slot[8];            /* active A/B system slot, "A" or "B" */
    char message[112];       /* human-readable detail for the settings page */
    int busy;                /* includes worker cleanup after terminal state */
} carplay_ota_status_t;

/* Copy the current state; safe to call from the LVGL timer. */
void carplay_ota_status(carplay_ota_status_t *status);

/* Start a background check against the release version file. */
int carplay_ota_check(void);

/* Start a background download; reboots into the inactive slot on success. */
int carplay_ota_install(const char *expected_commit);

#ifdef __cplusplus
}
#endif

#endif /* RTCARPLAY_OTA_LUBANLITE_H */

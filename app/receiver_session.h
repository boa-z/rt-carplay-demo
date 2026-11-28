/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "rtcp/receiver.h"

typedef struct
{
    rtcp_now_playing_details_cb_t now_playing;
    rtcp_host_ui_cb_t host_ui;
    void (*prepare)(rtcp_receiver_t *receiver, void *context);
    void *context;
} carplay_session_callbacks_t;

/* The caller serializes lifecycle operations and closes any Receiver left
 * after a start failure in its normal platform teardown order. */
int carplay_session_open(rtcp_receiver_t **receiver, const rtcp_receiver_config_t *config,
                         const carplay_session_callbacks_t *callbacks);
int carplay_session_close(rtcp_receiver_t **receiver);

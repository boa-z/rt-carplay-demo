/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "receiver_session.h"
#include "rtcp/rtcp.h"

int carplay_session_open(rtcp_receiver_t **receiver, const rtcp_receiver_config_t *config,
                         const carplay_session_callbacks_t *callbacks)
{
    if (*receiver)
        return RTCP_ESTATE;
    int result = rtcp_receiver_create(config, receiver);
    if (result != RTCP_OK)
        return result;
    if (callbacks)
    {
        rtcp_receiver_set_now_playing_details_callback(*receiver, callbacks->now_playing,
                                                       callbacks->context);
        rtcp_receiver_set_host_ui_callback(*receiver, callbacks->host_ui, callbacks->context);
        if (callbacks->prepare)
            callbacks->prepare(*receiver, callbacks->context);
    }
    return rtcp_receiver_start(*receiver);
}

int carplay_session_close(rtcp_receiver_t **receiver)
{
    int result = rtcp_receiver_destroy(*receiver);
    if (result == RTCP_OK)
        *receiver = NULL;
    return result;
}

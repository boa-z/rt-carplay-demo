/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <rtthread.h>
#include "connectivity_page.h"
#include "carplay_ui_theme.h"
#include "ui_language.h"
#include "icons/carplay_icons.h"
#include "settings_navigation.h"
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
#include "carplay_app.h"
#include "fusion_dashboard.h"
#include "rtcp/rtcp.h"
#endif

LV_FONT_DECLARE(carplay_ui_34);
LV_FONT_DECLARE(carplay_ui_21);
LV_FONT_DECLARE(carplay_ui_18);

#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
static bool can_resume(const demo_status_t *status)
{
    if (!status->receiver.session_established || status->display_reconfiguring ||
        (status->stage != DEMO_LIVE && status->stage != DEMO_CONNECT))
        return false;
    carplay_fusion_snapshot_t display;
    carplay_fusion_snapshot(rt_tick_get_millisecond(), &display);
    return display.effective == CARPLAY_LOCAL;
}
#endif

static void connect_phone(lv_event_t *event)
{
    (void)event;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    demo_status_t status = { 0 };
    if (carplay_connection_status(&status) && can_resume(&status))
    {
        if (carplay_app_set_projection_enabled(true) == RTCP_OK)
            carplay_fusion_ui_resume();
        return;
    }
#endif
    carplay_connection_retry();
}

static lv_obj_t *connection_label(lv_obj_t *parent, const char *title, const lv_font_t *font,
                                  carplay_style_role_t style)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    carplay_ui_style(label, style);
    carplay_ui_set_text(label, title);
    return label;
}

void carplay_connectivity_build(carplay_connectivity_view_t *view, lv_obj_t *page)
{
    lv_obj_t *card = lv_obj_create(page);
    lv_obj_remove_style_all(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(card, 24, 63);
    lv_obj_set_size(card, 976, 99);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    carplay_ui_style(card, CARPLAY_STYLE_CARD);

    lv_obj_t *phone = lv_image_create(card);
    lv_image_set_src(phone, &carplay_icon_smartphone_42);
    lv_obj_set_pos(phone, 18, 16);
    carplay_ui_style(phone, CARPLAY_STYLE_ICON_ACCENT);

    lv_obj_t *title = connection_label(card, "Apple CarPlay", &carplay_ui_21, CARPLAY_STYLE_TEXT);
    lv_obj_set_pos(title, 76, 12);
    view->state = connection_label(card, "Waiting for phone", &carplay_ui_18, CARPLAY_STYLE_TEXT);
    lv_obj_set_pos(view->state, 76, 40);
    view->transport = connection_label(card, "", &carplay_ui_18, CARPLAY_STYLE_MUTED);
    lv_obj_set_pos(view->transport, 76, 66);
    view->capabilities = connection_label(card, "", &carplay_ui_18, CARPLAY_STYLE_MUTED);
    lv_obj_set_pos(view->capabilities, 76, 66);
    view->action = carplay_settings_row(card, "Connect phone", &carplay_icon_link_24,
                                        false, connect_phone, NULL);
    lv_obj_set_size(view->action, 254, 64);
    lv_obj_align(view->action, LV_ALIGN_RIGHT_MID, -16, 0);
    view->action_label = lv_obj_get_child(view->action, 1);
    carplay_ui_style(view->action, CARPLAY_STYLE_ACCENT_CARD);
    carplay_ui_style_part(view->action, CARPLAY_STYLE_CARD_ALT, LV_STATE_DISABLED);
}

const char *carplay_connectivity_state(const demo_status_t *status)
{
    switch (status ? status->stage : DEMO_BOOT)
    {
    case DEMO_PAIR:
        return "Confirm pairing on iPhone";
    case DEMO_NETWORK:
        return "Connecting to wireless network";
    case DEMO_CONNECT:
        return "Starting CarPlay";
    case DEMO_LIVE:
        return "Connected";
    case DEMO_CLOSING:
        return "Disconnecting";
    case DEMO_FAILED:
        return "Connection failed";
    default:
        return "Waiting for phone";
    }
}

void carplay_connectivity_update(carplay_connectivity_view_t *view,
                                 const demo_status_t *status)
{
    demo_stage_t stage = status ? status->stage : DEMO_BOOT;
    bool busy = stage == DEMO_PAIR || stage == DEMO_NETWORK || stage == DEMO_CONNECT;
    bool connected = stage == DEMO_LIVE;
    bool resume = false;
#if defined(RTCARPLAY_SINGLE_SCREEN_FUSION)
    resume = status && can_resume(status);
    connected |= resume;
    busy &= !resume;
#endif
    carplay_ui_set_text(view->state, resume ? "Connected" : carplay_connectivity_state(status));
    carplay_ui_set_text(view->transport, (connected || busy) && status
                                             ? (status->wireless ? "iPhone · Wireless" : "iPhone · USB")
                                             : "");
    carplay_ui_set_text(view->capabilities, connected || busy ? "" : status && status->wireless_enabled ? "Available over USB and Wi-Fi"
                                                                                                        : "Available over USB");
    carplay_ui_set_text(view->action_label, resume ? "Resume CarPlay" : stage == DEMO_FAILED ? "Retry"
                                                                                             : "Connect phone");
    if (busy || stage == DEMO_BOOT || stage == DEMO_CLOSING || (connected && !resume))
        lv_obj_add_state(view->action, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(view->action, LV_STATE_DISABLED);
}

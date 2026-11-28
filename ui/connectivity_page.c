/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "connectivity_page.h"
#include "launcher_layout.h"
#include "ui_language.h"

LV_FONT_DECLARE(carplay_ui_18);

void carplay_connectivity_build(carplay_connectivity_view_t *view, lv_obj_t *page,
                                lv_event_cb_t activate)
{
    lv_obj_t *card = carplay_connection_card_build(page, &view->state, &view->transport);
    view->capabilities = carplay_launcher_label(card, "", 76, 66, 500,
                                                &carplay_ui_18, CARPLAY_STYLE_MUTED);
    view->action = carplay_connection_action(card, activate, &view->action_label);
}

void carplay_connectivity_update(const carplay_connectivity_view_t *view,
                                 const carplay_connectivity_snapshot_t *snapshot)
{
    carplay_ui_set_text(view->state, snapshot->state);
    carplay_ui_set_text(view->transport, snapshot->transport);
    carplay_ui_set_text(view->capabilities, snapshot->availability);
    carplay_ui_set_text(view->action_label, snapshot->resume  ? "Resume CarPlay"
                                            : snapshot->retry ? "Retry"
                                                              : "Connect phone");
    if (snapshot->actionable)
        lv_obj_remove_state(view->action, LV_STATE_DISABLED);
    else
        lv_obj_add_state(view->action, LV_STATE_DISABLED);
}

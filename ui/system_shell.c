/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "system_shell.h"
#include "carplay_ui_theme.h"
#include "settings_navigation.h"
#include "icons/carplay_icons.h"

static lv_obj_t *region(lv_obj_t *parent, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(obj, lv_pct(100), height);
    return obj;
}

void carplay_system_shell_build(carplay_system_shell_t *shell, lv_obj_t *parent,
                                lv_event_cb_t navigate)
{
    static const char * const captions[] = { "Home", "Phone Hub", "Settings" };
    static const lv_image_dsc_t * const icons[] = {
        &carplay_icon_info_26, &carplay_icon_smartphone_24, &carplay_icon_settings_24
    };
    lv_obj_t *frame = region(parent, lv_pct(100));
    lv_obj_set_flex_flow(frame, LV_FLEX_FLOW_COLUMN);
    shell->status = region(frame, CARPLAY_SHELL_STATUS_HEIGHT);
    carplay_ui_style(shell->status, CARPLAY_STYLE_STRIP);
    shell->content = region(frame, 0);
    lv_obj_set_flex_grow(shell->content, 1);
    shell->navigation = region(frame, CARPLAY_SHELL_NAV_HEIGHT);
    carplay_ui_style(shell->navigation, CARPLAY_STYLE_STRIP);
    lv_obj_set_flex_flow(shell->navigation, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shell->navigation, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(shell->navigation, 16, 0);
    lv_obj_set_style_pad_column(shell->navigation, 12, 0);
    for (unsigned index = 0; index < CARPLAY_SHELL_DESTINATIONS; ++index)
    {
        shell->tabs[index] = carplay_settings_tab(shell->navigation, captions[index],
                                                  icons[index], navigate, (void *)(uintptr_t)index);
        lv_obj_set_width(shell->tabs[index], 0);
        lv_obj_set_flex_grow(shell->tabs[index], 1);
    }
}

void carplay_system_shell_select(carplay_system_shell_t *shell, unsigned destination)
{
    for (unsigned index = 0; index < CARPLAY_SHELL_DESTINATIONS; ++index)
        carplay_settings_tab_select(shell->tabs[index], index == destination);
}

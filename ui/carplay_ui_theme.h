/* SPDX-License-Identifier: GPL-3.0-or-later */
/* V4 palette tokens and live light/dark switching for the native LVGL UI.
 * Only presentation lives here; persistence is owned by launcher_preferences. */
#ifndef CARPLAY_UI_THEME_H
#define CARPLAY_UI_THEME_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef enum
{
    CARPLAY_UI_BG,
    CARPLAY_UI_SURFACE,
    CARPLAY_UI_SURFACE2,
    CARPLAY_UI_SURFACE3,
    CARPLAY_UI_BORDER,
    CARPLAY_UI_TEXT,
    CARPLAY_UI_MUTED,
    CARPLAY_UI_FAINT,
    CARPLAY_UI_ACCENT,
    CARPLAY_UI_SOFT,
    CARPLAY_UI_AMBER,
    CARPLAY_UI_RED,
    CARPLAY_UI_PANEL,
    CARPLAY_UI_BAR,
    CARPLAY_UI_ROLE_COUNT
} carplay_ui_role_t;

/* A role fully describes the visual of the object it is applied to, including
 * background opacity and border width, so a theme change can restyle objects
 * without the owner re-running its layout code. */
typedef enum
{
    CARPLAY_STYLE_SCREEN,      /* full-bleed page background */
    CARPLAY_STYLE_CARD,        /* surface card with hairline border */
    CARPLAY_STYLE_CARD_ALT,    /* secondary surface, no border */
    CARPLAY_STYLE_ACCENT_CARD, /* soft accent fill with accent hairline */
    CARPLAY_STYLE_TILE,        /* soft accent tile behind an icon */
    CARPLAY_STYLE_TEXT,
    CARPLAY_STYLE_MUTED,
    CARPLAY_STYLE_FAINT,
    CARPLAY_STYLE_ACCENT_TEXT,
    CARPLAY_STYLE_WARN_TEXT,
    CARPLAY_STYLE_BAR_BG, /* track / unfilled indicator */
    CARPLAY_STYLE_BAR_FILL,
    CARPLAY_STYLE_SLIDER_KNOB,
    CARPLAY_STYLE_SEGMENT,    /* unselected segmented button */
    CARPLAY_STYLE_SEGMENT_ON, /* selected segmented button */
    CARPLAY_STYLE_SEGMENT_TEXT,
    CARPLAY_STYLE_SEGMENT_TEXT_ON,
    CARPLAY_STYLE_LINE_TEXT,   /* lv_line stroke in the primary text colour */
    CARPLAY_STYLE_LINE_ACCENT, /* lv_line stroke in the accent colour */
    CARPLAY_STYLE_LINE_BORDER, /* 1 px divider fill */
    CARPLAY_STYLE_DOT_OFF,
    CARPLAY_STYLE_DOT_ON,
    CARPLAY_STYLE_ICON_ACCENT, /* A8 icon mask recoloured to the accent colour */
    CARPLAY_STYLE_ICON_TEXT,   /* A8 icon mask recoloured to the primary text colour */
    CARPLAY_STYLE_ICON_MUTED,  /* A8 icon mask recoloured to the muted colour */
    CARPLAY_STYLE_ICON_FAINT,  /* A8 icon mask recoloured to the faint colour */
    CARPLAY_STYLE_STRIP,       /* fusion instrument panel background */
    CARPLAY_STYLE_STRIP_BUTTON /* fusion back button surface */
} carplay_style_role_t;

bool carplay_ui_dark(void);
void carplay_ui_set_dark(bool dark);
uint32_t carplay_ui_color(carplay_ui_role_t role);
/* Applies the role now and re-applies it whenever the theme changes. */
void carplay_ui_style(lv_obj_t *obj, carplay_style_role_t role);
void carplay_ui_style_part(lv_obj_t *obj, carplay_style_role_t role,
                           lv_style_selector_t selector);
/* Theme changes repaint registered objects and call the optional hook. */
void carplay_ui_on_theme_change(void (*hook)(void));

#endif

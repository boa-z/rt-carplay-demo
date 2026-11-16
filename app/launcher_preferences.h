/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_LAUNCHER_PREFERENCES_H
#define CARPLAY_LAUNCHER_PREFERENCES_H

typedef enum
{
    CARPLAY_PREF_LANGUAGE,   /* 0 english, 1 simplified chinese */
    CARPLAY_PREF_THEME,      /* 0 light, 1 dark */
    CARPLAY_PREF_DISPLAY,    /* carplay_display_mode_t */
    CARPLAY_PREF_WIRELESS,   /* 0 disabled, 1 enabled */
    CARPLAY_PREF_MIC_MUTED,  /* 0 enabled, 1 muted */
    CARPLAY_PREF_VOLUME,     /* 0..100 percent of the local mixer */
    CARPLAY_PREF_BRIGHTNESS, /* 5..100 percent of the panel PWM */
    CARPLAY_PREF_COUNT
} carplay_preference_t;

/* Per-key accepted maximum; values outside the range are never stored. */
#define CARPLAY_PREF_VOLUME_MAX     100U
#define CARPLAY_PREF_BRIGHTNESS_MAX 100U
#define CARPLAY_PREF_BRIGHTNESS_MIN 5U

#if defined(RT_USING_DFS) || defined(CARPLAY_PREFERENCES_EXTERNAL_PROVIDER)
/* Initialize on the application startup owner before worker/UI requests. */
int carplay_preferences_init(void);
unsigned carplay_preferences_get(carplay_preference_t key, unsigned fallback);
/* Returns -EINVAL for an out-of-range value, -EIO when the record could not be
 * written. A successful call is the only confirmation that the choice persists. */
int carplay_preferences_set(carplay_preference_t key, unsigned value);
int carplay_preferences_error(void);
#else
static inline int carplay_preferences_init(void)
{
    return 0;
}
static inline unsigned carplay_preferences_get(carplay_preference_t key, unsigned fallback)
{
    (void)key;
    return fallback;
}
static inline int carplay_preferences_set(carplay_preference_t key, unsigned value)
{
    (void)key;
    (void)value;
    return 0;
}
static inline int carplay_preferences_error(void)
{
    return 0;
}
#endif
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_LAUNCHER_PREFERENCES_H
#define CARPLAY_LAUNCHER_PREFERENCES_H

typedef enum
{
    CARPLAY_PREF_LANGUAGE,
    CARPLAY_PREF_WIRELESS,
    CARPLAY_PREF_MIC_MUTED,
    CARPLAY_PREF_DISPLAY,
    CARPLAY_PREF_COUNT
} carplay_preference_t;

#if defined(RT_USING_DFS)
/* Initialize on the application startup owner before worker/UI requests. */
int carplay_preferences_init(void);
unsigned carplay_preferences_get(carplay_preference_t key, unsigned fallback);
int carplay_preferences_set(carplay_preference_t key, unsigned value);
int carplay_preferences_error(void);
#else
/* Non-filesystem hosts retain volatile settings and the existing defaults. */
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

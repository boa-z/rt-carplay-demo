/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>

#define CARPLAY_PREFERENCES_DIR "."
#include "../../app/launcher_preferences.c"

rt_mutex_t rt_mutex_create(const char *name, int flags)
{
    assert(name && flags == RT_IPC_FLAG_PRIO);
    return &present;
}

int rt_mutex_take(rt_mutex_t mutex, int ticks)
{
    assert(mutex == &present && ticks == RT_WAITING_FOREVER);
    return RT_EOK;
}

int rt_mutex_release(rt_mutex_t mutex)
{
    assert(mutex == &present);
    return RT_EOK;
}

int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}

int main(void)
{
    char name[CARPLAY_BT_NAME_MAX + 1];
    (void)remove(PREFERENCES_FILE);
    (void)remove(PREFERENCES_BAK);
    uint8_t legacy[17] = { 'C', 'P', 'U', 'I', 2, 1U << CARPLAY_PREF_VOLUME };
    legacy[6 + CARPLAY_PREF_VOLUME] = 25;
    uint32_t hash = checksum(legacy, sizeof(legacy) - 4);
    for (unsigned index = 0; index < 4; ++index)
        legacy[sizeof(legacy) - 4 + index] = (uint8_t)(hash >> (index * 8));
    FILE *file = fopen(PREFERENCES_FILE, "wb");
    assert(file && fwrite(legacy, sizeof(legacy), 1, file) == 1);
    assert(fclose(file) == 0);

    assert(carplay_preferences_init() == 0);
    carplay_preferences_bt_name(name);
    assert(!strcmp(name, CARPLAY_BT_NAME_DEFAULT));
    assert(carplay_preferences_get(CARPLAY_PREF_VOLUME, 60) == 25);
    assert(carplay_preferences_get(CARPLAY_PREF_HIGH_FPS, CARPLAY_PREF_HIGH_FPS_DEFAULT) == 0);
    assert(carplay_preferences_set(CARPLAY_PREF_HIGH_FPS, 2) == -EINVAL);
    assert(carplay_preferences_set(CARPLAY_PREF_HIGH_FPS, 0) == 0);
    assert(carplay_preferences_set_bt_name("") == -EINVAL);
    assert(carplay_preferences_set_bt_name(" D213") == -EINVAL);
    assert(carplay_preferences_set_bt_name("D213\n") == -EINVAL);
    assert(carplay_preferences_set_bt_name("12345678901234567890123456789") == -EINVAL);
    assert(carplay_preferences_set_bt_name("D213 CarPlay") == 0);

    lock = NULL;
    present = 0;
    memset(values, 0, sizeof(values));
    strcpy(bt_name, CARPLAY_BT_NAME_DEFAULT);
    assert(carplay_preferences_init() == 0);
    carplay_preferences_bt_name(name);
    assert(!strcmp(name, "D213 CarPlay"));
    assert(carplay_preferences_get(CARPLAY_PREF_HIGH_FPS, CARPLAY_PREF_HIGH_FPS_DEFAULT) == 0);
    assert(carplay_preferences_get(CARPLAY_PREF_VOLUME, 60) == 25);
    assert(carplay_preferences_get(CARPLAY_PREF_BRIGHTNESS, 72) == 72);
    assert(remove(PREFERENCES_FILE) == 0);
    puts("Launcher preferences: version 2 migration, name validation and persisted frame rate passed");
    return 0;
}

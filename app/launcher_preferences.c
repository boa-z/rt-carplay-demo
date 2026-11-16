/* SPDX-License-Identifier: GPL-3.0-or-later */
#if defined(RT_USING_DFS)
#include <rtthread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "launcher_preferences.h"

#define PREFERENCES_FILE "/data/carplay-ui.bin"
#define PREFERENCES_TMP  "/data/carplay-ui.tmp"
#define PREFERENCES_BAK  "/data/carplay-ui.bak"

/* Version 2 record: "CPUI" | version | present mask | values | FNV-1a.
 * Bytes 0..5 are the header, one value byte per key follows, and the last four
 * bytes checksum everything before them. Version 1 (16 bytes, four keys) is
 * rejected and replaced with defaults; no build in the field uses it. */
#define PREFERENCE_BYTES      (6 + CARPLAY_PREF_COUNT + 4)
#define PREFERENCE_HASH_BYTES (6 + CARPLAY_PREF_COUNT)

static rt_mutex_t lock;
static uint8_t values[CARPLAY_PREF_COUNT], present;
static int last_error;
static bool backup_loaded;

static uint32_t checksum(const uint8_t *bytes)
{
    uint32_t hash = 2166136261U;
    for (unsigned i = 0; i < PREFERENCE_HASH_BYTES; ++i)
        hash = (hash ^ bytes[i]) * 16777619U;
    return hash;
}

/* Volume and brightness store percentages; the remaining keys are booleans or
 * the three-value display mode. */
static unsigned ceiling(carplay_preference_t key)
{
    switch (key)
    {
    case CARPLAY_PREF_DISPLAY:
        return 2U;
    case CARPLAY_PREF_VOLUME:
        return CARPLAY_PREF_VOLUME_MAX;
    case CARPLAY_PREF_BRIGHTNESS:
        return CARPLAY_PREF_BRIGHTNESS_MAX;
    default:
        return 1U;
    }
}

static bool valid(carplay_preference_t key, unsigned value)
{
    if (key == CARPLAY_PREF_BRIGHTNESS)
        return value >= CARPLAY_PREF_BRIGHTNESS_MIN && value <= CARPLAY_PREF_BRIGHTNESS_MAX;
    return value <= ceiling(key);
}

static int load(const char *path)
{
    uint8_t bytes[PREFERENCE_BYTES + 1];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return errno == ENOENT ? -ENOENT : -EIO;
    ssize_t size = read(fd, bytes, sizeof(bytes));
    (void)close(fd);
    if (size != PREFERENCE_BYTES || memcmp(bytes, "CPUI", 4) || bytes[4] != 2 ||
        (bytes[5] & ~((1U << CARPLAY_PREF_COUNT) - 1U)))
        return -EINVAL;
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i)
        stored |= (uint32_t)bytes[PREFERENCE_HASH_BYTES + i] << (i * 8);
    if (stored != checksum(bytes))
        return -EINVAL;
    for (unsigned i = 0; i < CARPLAY_PREF_COUNT; ++i)
        if (!valid((carplay_preference_t)i, bytes[6 + i]))
            return -EINVAL;
    present = bytes[5];
    memcpy(values, bytes + 6, sizeof(values));
    return 0;
}

int carplay_preferences_init(void)
{
    if (lock)
        return 0;
    lock = rt_mutex_create("cp-pref", RT_IPC_FLAG_PRIO);
    if (!lock)
        return -ENOMEM;
    int result = load(PREFERENCES_FILE);
    if (result && load(PREFERENCES_BAK) == 0)
    {
        result = 0;
        backup_loaded = true;
    }
    if (result != -ENOENT)
        last_error = result;
    if (result && result != -ENOENT)
        rt_kprintf("[carplay-settings] invalid/unavailable preferences; using defaults (%d)\n", result);
    return 0;
}

unsigned carplay_preferences_get(carplay_preference_t key, unsigned fallback)
{
    if (!lock || (unsigned)key >= CARPLAY_PREF_COUNT)
        return fallback;
    (void)rt_mutex_take(lock, RT_WAITING_FOREVER);
    unsigned value = present & (1U << key) ? values[key] : fallback;
    (void)rt_mutex_release(lock);
    return value;
}

int carplay_preferences_set(carplay_preference_t key, unsigned value)
{
    if (!lock || (unsigned)key >= CARPLAY_PREF_COUNT || !valid(key, value))
        return -EINVAL;
    (void)rt_mutex_take(lock, RT_WAITING_FOREVER);
    if ((present & (1U << key)) && values[key] == value && !last_error)
    {
        (void)rt_mutex_release(lock);
        return 0;
    }
    uint8_t bytes[PREFERENCE_BYTES] = { 'C', 'P', 'U', 'I', 2, (uint8_t)(present | (1U << key)) };
    memcpy(bytes + 6, values, sizeof(values));
    bytes[6 + key] = (uint8_t)value;
    uint32_t hash = checksum(bytes);
    for (unsigned i = 0; i < 4; ++i)
        bytes[PREFERENCE_HASH_BYTES + i] = (uint8_t)(hash >> (i * 8));
    int result = -EIO;
    int fd = open(PREFERENCES_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0)
    {
        bool complete = write(fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes);
        if (complete)
            complete = fsync(fd) == 0;
        if (close(fd) != 0)
            complete = false;
        if (complete)
        {
            if (rename(PREFERENCES_TMP, PREFERENCES_FILE) == 0)
                result = 0;
            else
            {
                /* FAT may not replace a destination. Keep a recoverable
                 * previous record until the new file is promoted. */
                bool backed_up = backup_loaded;
                if (backup_loaded)
                    (void)remove(PREFERENCES_FILE);
                else
                {
                    int previous = open(PREFERENCES_FILE, O_RDONLY);
                    if (previous >= 0)
                    {
                        (void)close(previous);
                        (void)remove(PREFERENCES_BAK);
                        backed_up = rename(PREFERENCES_FILE, PREFERENCES_BAK) == 0;
                    }
                }
                if (rename(PREFERENCES_TMP, PREFERENCES_FILE) == 0)
                    result = 0;
                else if (backed_up)
                    (void)rename(PREFERENCES_BAK, PREFERENCES_FILE);
            }
        }
    }
    (void)remove(PREFERENCES_TMP);
    if (!result)
    {
        present = bytes[5];
        memcpy(values, bytes + 6, sizeof(values));
        backup_loaded = false;
        (void)remove(PREFERENCES_BAK);
    }
    else
        rt_kprintf("[carplay-settings] could not save %s (%d)\n", PREFERENCES_FILE, result);
    last_error = result;
    (void)rt_mutex_release(lock);
    return result;
}

int carplay_preferences_error(void)
{
    if (!lock)
        return -ENOMEM;
    (void)rt_mutex_take(lock, RT_WAITING_FOREVER);
    int result = last_error;
    (void)rt_mutex_release(lock);
    return result;
}
#endif

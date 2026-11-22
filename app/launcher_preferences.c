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

#ifndef CARPLAY_PREFERENCES_DIR
#define CARPLAY_PREFERENCES_DIR "/data"
#endif
#define PREFERENCES_FILE CARPLAY_PREFERENCES_DIR "/carplay-ui.bin"
#define PREFERENCES_TMP  CARPLAY_PREFERENCES_DIR "/carplay-ui.tmp"
#define PREFERENCES_BAK  CARPLAY_PREFERENCES_DIR "/carplay-ui.bak"

#define PREFERENCE_NAME_OFFSET (6 + CARPLAY_PREF_COUNT)
#define PREFERENCE_HASH_BYTES  (PREFERENCE_NAME_OFFSET + CARPLAY_BT_NAME_MAX + 1)
#define PREFERENCE_BYTES       (PREFERENCE_HASH_BYTES + 4)

static rt_mutex_t lock;
static uint8_t values[CARPLAY_PREF_COUNT], present;
static char bt_name[CARPLAY_BT_NAME_MAX + 1] = CARPLAY_BT_NAME_DEFAULT;
static int last_error;
static bool backup_loaded;

static uint32_t checksum(const uint8_t *bytes, size_t length)
{
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < length; ++i)
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

static bool valid_bt_name(const char *name)
{
    if (!name || name[0] == ' ' || name[0] == '\0')
        return false;
    for (size_t index = 0; index <= CARPLAY_BT_NAME_MAX; ++index)
    {
        unsigned char byte = (unsigned char)name[index];
        if (byte == '\0')
            return name[index - 1] != ' ';
        if (byte < 0x20 || byte > 0x7e)
            return false;
    }
    return false;
}

static int load(const char *path)
{
    uint8_t bytes[PREFERENCE_BYTES + 1];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return errno == ENOENT ? -ENOENT : -EIO;
    ssize_t size = read(fd, bytes, sizeof(bytes));
    (void)close(fd);
    unsigned key_count = CARPLAY_PREF_COUNT;
    size_t hash_bytes = PREFERENCE_HASH_BYTES;
    if (size == 17 && bytes[4] == 2)
    {
        key_count = CARPLAY_PREF_HIGH_FPS;
        hash_bytes = 6 + key_count;
    }
    else if (size != PREFERENCE_BYTES || bytes[4] != 3)
        return -EINVAL;
    if (memcmp(bytes, "CPUI", 4) || (bytes[5] & ~((1U << key_count) - 1U)))
        return -EINVAL;
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i)
        stored |= (uint32_t)bytes[hash_bytes + i] << (i * 8);
    if (stored != checksum(bytes, hash_bytes))
        return -EINVAL;
    for (unsigned i = 0; i < key_count; ++i)
        if ((bytes[5] & (1U << i)) && !valid((carplay_preference_t)i, bytes[6 + i]))
            return -EINVAL;
    if (bytes[4] == 3)
    {
        const char *name = (const char *)bytes + PREFERENCE_NAME_OFFSET;
        if (!valid_bt_name(name))
            return -EINVAL;
        memcpy(bt_name, name, sizeof(bt_name));
    }
    present = bytes[5];
    memcpy(values, bytes + 6, key_count);
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

static int save(uint8_t bytes[PREFERENCE_BYTES])
{
    uint32_t hash = checksum(bytes, PREFERENCE_HASH_BYTES);
    for (unsigned i = 0; i < 4; ++i)
        bytes[PREFERENCE_HASH_BYTES + i] = (uint8_t)(hash >> (i * 8));
    int result = -EIO;
    int fd = open(PREFERENCES_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0)
    {
        bool complete = write(fd, bytes, PREFERENCE_BYTES) == PREFERENCE_BYTES;
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
        memcpy(bt_name, bytes + PREFERENCE_NAME_OFFSET, sizeof(bt_name));
        backup_loaded = false;
        (void)remove(PREFERENCES_BAK);
    }
    else
        rt_kprintf("[carplay-settings] could not save %s (%d)\n", PREFERENCES_FILE, result);
    last_error = result;
    return result;
}

int carplay_preferences_set(carplay_preference_t key, unsigned value)
{
    if (!lock || (unsigned)key >= CARPLAY_PREF_COUNT || !valid(key, value))
        return -EINVAL;
    (void)rt_mutex_take(lock, RT_WAITING_FOREVER);
    int result = 0;
    if (!(present & (1U << key)) || values[key] != value || last_error)
    {
        uint8_t bytes[PREFERENCE_BYTES] = { 'C', 'P', 'U', 'I', 3, (uint8_t)(present | (1U << key)) };
        memcpy(bytes + 6, values, sizeof(values));
        bytes[6 + key] = (uint8_t)value;
        memcpy(bytes + PREFERENCE_NAME_OFFSET, bt_name, sizeof(bt_name));
        result = save(bytes);
    }
    (void)rt_mutex_release(lock);
    return result;
}

void carplay_preferences_bt_name(char name[CARPLAY_BT_NAME_MAX + 1])
{
    if (!lock)
    {
        memcpy(name, CARPLAY_BT_NAME_DEFAULT, sizeof(CARPLAY_BT_NAME_DEFAULT));
        return;
    }
    (void)rt_mutex_take(lock, RT_WAITING_FOREVER);
    memcpy(name, bt_name, sizeof(bt_name));
    (void)rt_mutex_release(lock);
}

int carplay_preferences_set_bt_name(const char *name)
{
    if (!valid_bt_name(name))
        return -EINVAL;
    if (!lock)
        return -EIO;
    (void)rt_mutex_take(lock, RT_WAITING_FOREVER);
    int result = 0;
    if (strcmp(bt_name, name) || last_error)
    {
        uint8_t bytes[PREFERENCE_BYTES] = { 'C', 'P', 'U', 'I', 3, present };
        memcpy(bytes + 6, values, sizeof(values));
        memcpy(bytes + PREFERENCE_NAME_OFFSET, name, strlen(name) + 1);
        result = save(bytes);
    }
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

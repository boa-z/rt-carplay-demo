#define _GNU_SOURCE
#include "linux_demo.h"
#include "pairing_files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "airplay_pairing_store.h"
#include "mbedtls/platform_util.h"
#include "rtcp_linux.h"

static int file_load(const char *path, uint8_t *data, size_t capacity, size_t *length, bool *exists)
{
    *length = 0;
    *exists = false;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT ? RTCP_OK : RTCP_EIO;
    *exists = true;
    struct stat attributes;
    if (fstat(fd, &attributes) != 0 || !S_ISREG(attributes.st_mode) ||
        (attributes.st_mode & 0077) != 0 || (uintmax_t)attributes.st_size > capacity)
    {
        close(fd);
        return RTCP_EIO;
    }
    while (*length < (size_t)attributes.st_size)
    {
        ssize_t count = read(fd, data + *length, (size_t)attributes.st_size - *length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            close(fd);
            return RTCP_EIO;
        }
        *length += (size_t)count;
    }
    return close(fd) == 0 ? RTCP_OK : RTCP_EIO;
}

int carplay_file_load(const char *path, uint8_t *data, size_t capacity, size_t *length)
{
    bool exists;
    return file_load(path, data, capacity, length, &exists);
}

int carplay_file_save(const char *path, const uint8_t *data, size_t length)
{
    char directory[256], temporary[272];
    int count = snprintf(directory, sizeof(directory), "%s", path);
    if (count < 0 || (size_t)count >= sizeof(directory))
        return RTCP_EINVAL;
    char *separator = strrchr(directory, '/');
    if (!separator || separator == directory)
        return RTCP_EINVAL;
    *separator = '\0';
    count = snprintf(temporary, sizeof(temporary), "%s/.pair-XXXXXX", directory);
    if (count < 0 || (size_t)count >= sizeof(temporary))
        return RTCP_EINVAL;
    int parent = open(directory, O_DIRECTORY | O_CLOEXEC | O_RDONLY);
    if (parent < 0)
        return RTCP_EIO;
    int fd = mkostemp(temporary, O_CLOEXEC);
    if (fd < 0)
    {
        close(parent);
        return RTCP_EIO;
    }
    size_t offset = 0;
    while (offset < length)
    {
        ssize_t written = write(fd, data + offset, length - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            break;
        offset += (size_t)written;
    }
    bool complete = offset == length && fsync(fd) == 0;
    if (close(fd) != 0)
        complete = false;
    complete = complete && rename(temporary, path) == 0 && fsync(parent) == 0;
    if (!complete)
        unlink(temporary);
    close(parent);
    return complete ? RTCP_OK : RTCP_EIO;
}

static int load(uint8_t *bytes, size_t capacity, size_t *length, void *context)
{
    return carplay_file_load(context, bytes, capacity, length);
}

static int save(const uint8_t *bytes, size_t length, void *context)
{
    return carplay_file_save(context, bytes, length);
}

static bool random_bytes(uint8_t *output, size_t length, void *context)
{
    (void)context;
    return rtcp_os_linux.random(output, length);
}

static bool sign(const uint8_t *body, size_t length, uint8_t signature[64], void *context)
{
    const demo_identity_t *identity = context;
    return airplay_ed25519_sign(&identity->keys, body, length, signature);
}

static bool verify(const uint8_t key[32], const uint8_t *body, size_t length,
                   const uint8_t signature[64], void *context)
{
    (void)context;
    return airplay_ed25519_verify(key, body, length, signature);
}

int demo_identity_open(demo_identity_t *identity, const char *directory,
                       rtcp_pairing_provider_t *pairing)
{
    char path[256];
    if (snprintf(path, sizeof(path), "%s/identity.seed", directory) >= (int)sizeof(path) ||
        snprintf(identity->store, sizeof(identity->store), "%s/airplay-pairings", directory) >=
            (int)sizeof(identity->store))
        return RTCP_EINVAL;
    uint8_t seed[AIRPLAY_ED25519_SEED_BYTES];
    size_t length;
    bool exists;
    int result = file_load(path, seed, sizeof(seed), &length, &exists);
    if (result == RTCP_OK && !exists)
    {
        result = random_bytes(seed, sizeof(seed), NULL) ? carplay_file_save(path, seed, sizeof(seed)) : RTCP_EIO;
        length = sizeof(seed);
    }
    bool ready = result == RTCP_OK && length == sizeof(seed) &&
                 airplay_ed25519_keypair_from_seed(seed, &identity->keys);
    mbedtls_platform_zeroize(seed, sizeof(seed));
    if (!ready)
        return RTCP_EIO;

    const uint8_t *key = identity->keys.public_key;
    snprintf(identity->device_id, sizeof(identity->device_id), "%02X:%02X:%02X:%02X:%02X:%02X",
             (key[0] & 0xfcU) | 0x02U, key[1], key[2], key[3], key[4], key[5]);
    for (size_t index = 0; index < AIRPLAY_ED25519_PUBLIC_KEY_BYTES; ++index)
        snprintf(identity->public_key + index * 2, 3, "%02x", key[index]);

    airplay_pairing_store_backend_t backend = { .load = load, .save = save, .context = identity->store };
    result = airplay_pairing_store_init(&backend);
    if (result != RTCP_OK)
        return result;
    pairing->pair_setup = (rtcp_pair_setup_provider_t){
        .pairing_code = "3939",
        .accessory_identifier = (const uint8_t *)identity->device_id,
        .accessory_identifier_len = strlen(identity->device_id),
        .accessory_public_key = key,
        .random = random_bytes,
        .sign_accessory = sign,
        .verify_controller = verify,
        .save_pairing = airplay_pairing_store_save_pairing,
        .provider_context = identity,
    };
    pairing->pair_verify = (rtcp_pair_verify_provider_t){
        .accessory_identifier = pairing->pair_setup.accessory_identifier,
        .accessory_identifier_len = pairing->pair_setup.accessory_identifier_len,
        .random = random_bytes,
        .sign_accessory = sign,
        .verify_controller = verify,
        .lookup_controller = airplay_pairing_store_lookup_controller,
        .provider_context = identity,
    };
    return RTCP_OK;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <drivers/mtd_nand.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <dfs_fs.h>
#include <webclient.h>
#include <aic_crc32.h>
#include <boot_param.h>
#include <burn.h>
#include <env.h>
#include <absystem_os.h>
#include "rtcp/rtcp.h"
#include "carplay_app.h"
#include "ota_lubanlite.h"
#include "connection_manager.h"
#include "network_manager.h"

#define OTA_PAGE        2048U
#define OTA_CHUNK       4096U
#define OTA_MAX_SIZE    (32U * 1024U * 1024U)
#define IMG_HEADER_SIZE 2048U
#define IMG_META_SIZE   512U
#ifndef RTCARPLAY_OTA_STAGE
#define RTCARPLAY_OTA_STAGE "/data/.carplay-update.img"
#endif
#ifndef RTCARPLAY_OTA_SOURCE_FILE
#define RTCARPLAY_OTA_SOURCE_FILE "/data/.carplay-ota-source"
#endif
#define STRINGIFY_(value) #value
#define STRINGIFY(value)  STRINGIFY_(value)
#ifndef RTCARPLAY_FW_VERSION
#define RTCARPLAY_FW_VERSION unknown
#endif
#ifndef RTCARPLAY_BUILD_COMMIT
#define RTCARPLAY_BUILD_COMMIT unknown
#endif

enum ota_operation
{
    OTA_DOWNLOAD,
    OTA_INSTALL,
    OTA_SOURCE,
    OTA_CONFIRM
};
enum image_member
{
    IMG_DDR,
    IMG_LOADER,
    IMG_INFO,
    IMG_SPL,
    IMG_ENV,
    IMG_ENV_R,
    IMG_OS,
    IMG_RODATA,
    IMG_DATA,
    IMG_MEMBER_COUNT
};

static const char * const member_names[] = {
    "image.updater.ddr",
    "image.updater.spl",
    "image.info",
    "image.target.spl",
    "image.target.env",
    "image.target.env_r",
    "image.target.os",
    "image.target.rodata",
    "image.target.data",
};
static const struct
{
    const char *name;
    uint32_t offset;
    uint32_t capacity;
} target_layout[] = {
    [IMG_SPL] = { "spl", 0, 0x100000 },
    [IMG_ENV] = { "env", 0x100000, 0x40000 },
    [IMG_ENV_R] = { "env_r", 0x140000, 0x40000 },
    [IMG_OS] = { "os", 0x180000, 0x800000 },
    [IMG_RODATA] = { "rodata", 0x1180000, 0xc00000 },
    [IMG_DATA] = { "data", 0x2980000, 0x2000000 },
};

struct image_part
{
    uint32_t offset, size, crc;
};
struct update_image
{
    struct image_part os, rodata;
    uint32_t metadata_crc;
    char version[24];
};

static struct rt_mutex ota_mutex, stage_guard;
static bool initialized, worker_active, cancel_requested, boot_health_checked;
static carplay_ota_status_t status;
static struct update_image staged;
static char requested_source[CARPLAY_OTA_SOURCE_MAX];

static bool uses_tls(const char *url)
{
    return !strncmp(url, "https://", 8);
}

static const char *url_path(const char *url)
{
    const char *host = uses_tls(url) ? url + 8 : !strncmp(url, "http://", 7) ? url + 7
                                                                             : NULL;
    const char *path = host ? strchr(host, '/') : NULL;
    if (!path || host == path)
        return NULL;
    const char *next = host;
    for (; next < path && *next != ':'; ++next)
        if (!((*next >= 'a' && *next <= 'z') || (*next >= 'A' && *next <= 'Z') ||
              (*next >= '0' && *next <= '9') || *next == '.' || *next == '-'))
            return NULL;
    if (next == host)
        return NULL;
    if (next != path)
    {
        unsigned port = 0;
        for (++next; next != path; ++next)
        {
            if (*next < '0' || *next > '9' || port > 6553)
                return NULL;
            port = port * 10 + (unsigned)(*next - '0');
        }
        if (!port || port > 65535)
            return NULL;
    }
    for (next = path; *next; ++next)
        if ((unsigned char)*next <= ' ' || (unsigned char)*next >= 127 ||
            *next == '#' || *next == '\\')
            return NULL;
    return path;
}

static bool image_url(const char *url)
{
    if (!url || strlen(url) >= CARPLAY_OTA_SOURCE_MAX)
        return false;
    const char *path = url_path(url);
    if (!path)
        return false;
    size_t length = strcspn(path, "?");
    return length > 5 && !memcmp(path + length - 4, ".img", 4) && path[length - 5] != '/';
}

static void set_status(carplay_ota_state_t state, int progress, const char *fmt, ...)
{
    va_list args;
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    status.state = state;
    status.progress = progress;
    va_start(args, fmt);
    vsnprintf(status.message, sizeof(status.message), fmt, args);
    va_end(args);
    rt_mutex_release(&ota_mutex);
}

static int ota_service_init(void)
{
    if (rt_mutex_init(&ota_mutex, "cpota", RT_IPC_FLAG_PRIO) != RT_EOK ||
        rt_mutex_init(&stage_guard, "cpstage", RT_IPC_FLAG_PRIO) != RT_EOK)
        return -RT_ENOMEM;
    snprintf(status.current, sizeof(status.current), "%s", STRINGIFY(RTCARPLAY_FW_VERSION));
    snprintf(status.current_commit, sizeof(status.current_commit), "%s", STRINGIFY(RTCARPLAY_BUILD_COMMIT));
    strcpy(status.slot, "?");
    FILE *file = fopen(RTCARPLAY_OTA_SOURCE_FILE, "rb");
    if (!file)
        file = fopen(RTCARPLAY_OTA_SOURCE_FILE ".bak", "rb");
    if (file)
    {
        char saved[CARPLAY_OTA_SOURCE_MAX];
        size_t length = fread(saved, 1, sizeof(saved) - 1, file);
        saved[length] = 0;
        if (fgetc(file) == EOF && !ferror(file) && strlen(saved) == length && image_url(saved))
            strcpy(status.source, saved);
        fclose(file);
    }
    remove(RTCARPLAY_OTA_STAGE);
    if (!status.source[0])
        remove("/data/.carplay-update.cpio");
    set_status(CARPLAY_OTA_IDLE, -1, "Enter a firmware .img URL to get started.");
    initialized = true;
    return RT_EOK;
}
INIT_APP_EXPORT(ota_service_init);

bool carplay_ota_file_write_begin(void)
{
    return initialized && rt_mutex_take(&stage_guard, 0) == RT_EOK;
}

void carplay_ota_file_write_end(void)
{
    rt_mutex_release(&stage_guard);
}

void carplay_ota_status(carplay_ota_status_t *out)
{
    if (!out)
        return;
    if (!initialized)
    {
        memset(out, 0, sizeof(*out));
        out->state = CARPLAY_OTA_FAILED;
        out->progress = -1;
        strcpy(out->message, "Update service is unavailable.");
        return;
    }
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    *out = status;
    out->busy = worker_active || status.state == CARPLAY_OTA_REBOOTING;
    rt_mutex_release(&ota_mutex);
}

static bool carplay_active(void)
{
    rtcp_receiver_state_t state;
    return carplay_app_is_running() &&
           (carplay_app_session_state(&state) != RTCP_OK || state.transport_connected || state.session_established);
}

static char read_slot(void)
{
    char slot = 0;
    if (fw_env_open() == 0)
    {
        const char *os = fw_getenv("osAB_now");
        const char *rodata = fw_getenv("rodataAB_now");
        const char *pending = fw_getenv("upgrade_available");
        const char *baseline = fw_getenv("carplay_ab_baseline");
        if (os && (!strcmp(os, "A") || !strcmp(os, "B")) && rodata && !strcmp(os, rodata) &&
            pending && !strcmp(pending, "0") && baseline && !strcmp(baseline, "2"))
            slot = os[0];
        fw_env_close();
    }
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    snprintf(status.slot, sizeof(status.slot), "%c", slot ? slot : '?');
    rt_mutex_release(&ota_mutex);
    return slot;
}

static bool commit_recorded(char current)
{
    bool recorded = false;
    char next[] = { current == 'A' ? 'B' : 'A', 0 };
    if (fw_env_open() == 0)
    {
        const char *os = fw_getenv("osAB_next");
        const char *rodata = fw_getenv("rodataAB_next");
        const char *available = fw_getenv("upgrade_available");
        recorded = os && !strcmp(os, next) && rodata && !strcmp(rodata, next) &&
                   available && !strcmp(available, "1");
        fw_env_close();
    }
    return recorded;
}

static bool cancelled(void)
{
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    bool result = cancel_requested;
    rt_mutex_release(&ota_mutex);
    return result;
}

static bool allowed_redirect(const char *initial, const char *target)
{
    const char *from = url_path(initial), *to = url_path(target);
    if (!from || !to)
        return false;
    if (from - initial == to - target && !memcmp(initial, target, (size_t)(from - initial)))
        return true;
    return !strncmp(initial, "https://github.com/", sizeof("https://github.com/") - 1) &&
           (!strncmp(target, "https://release-assets.githubusercontent.com/",
                     sizeof("https://release-assets.githubusercontent.com/") - 1) ||
            !strncmp(target, "https://objects.githubusercontent.com/",
                     sizeof("https://objects.githubusercontent.com/") - 1));
}

static struct webclient_session *http_get(const char *url)
{
    char current[1536];
    strcpy(current, url);
    for (unsigned hop = 0; hop <= 4; ++hop)
    {
        bool tls = uses_tls(current);
        if (cancelled() || !carplay_network_valid())
            break;
        struct webclient_session *session = webclient_session_create(WEBCLIENT_HEADER_BUFSZ);
        if (!session)
            break;
        session->outbound_netif = carplay_network_lease_name();
        webclient_set_timeout(session, 60000);
        int response = webclient_get_once(session, current);
        if (response == 200 && !!session->is_tls == tls && carplay_network_valid())
            return session;
        const char *location = webclient_header_fields_get(session, "Location");
        bool follow = location && (response == 301 || response == 302 || response == 303 ||
                                   response == 307 || response == 308);
        if (follow)
        {
            size_t offset = location[0] == '/' && location[1] != '/' ? (size_t)(url_path(current) - current) : 0;
            follow = strlen(location) < sizeof(current) - offset;
            if (follow)
                strcpy(current + offset, location);
        }
        webclient_close(session);
        if (!follow || !allowed_redirect(url, current))
            break;
    }
    return NULL;
}

static uint32_t read_le32(const unsigned char *field)
{
    return (uint32_t)field[0] | (uint32_t)field[1] << 8 |
           (uint32_t)field[2] << 16 | (uint32_t)field[3] << 24;
}

static bool field_equals(const unsigned char *field, const char *value)
{
    return !memcmp(field, value, strlen(value) + 1);
}

static bool check_component(FILE *file, unsigned char *buffer, struct image_part part)
{
    if (fseek(file, (long)part.offset, SEEK_SET))
        return false;
    uint32_t crc = 0;
    for (uint32_t remaining = part.size; remaining;)
    {
        uint32_t count = remaining > OTA_CHUNK ? OTA_CHUNK : remaining;
        if (fread(buffer, 1, count, file) != count)
            return false;
        crc = crc32(crc, buffer, count);
        remaining -= count;
    }
    return crc == part.crc;
}

static bool verify_image(FILE *file, unsigned char *buffer, struct update_image *image)
{
    memset(image, 0, sizeof(*image));
    if (fseek(file, 0, SEEK_END))
        return false;
    long length = ftell(file);
    if (length < (long)IMG_HEADER_SIZE || length > (long)OTA_MAX_SIZE ||
        fseek(file, 0, SEEK_SET) || fread(buffer, 1, IMG_HEADER_SIZE, file) != IMG_HEADER_SIZE ||
        memcmp(buffer, "AIC.FW\0", 8) || !field_equals(buffer + 8, "d21x") ||
        !field_equals(buffer + 72, "demo128_nand_carplay_page_2k_block_128k") ||
        !field_equals(buffer + 200, "spi-nand") || read_le32(buffer + 264) ||
        !field_equals(buffer + 268, "P=2K,B=128K"))
        return false;
    const unsigned char *version_end = memchr(buffer + 136, 0, sizeof(image->version));
    if (!version_end || version_end == buffer + 136)
        return false;
    size_t version_length = (size_t)(version_end - (buffer + 136));
    if (strspn((const char *)buffer + 136, "0123456789.") != version_length)
        return false;
    memcpy(image->version, buffer + 136, version_length);
    uint32_t meta_size = read_le32(buffer + 336), files = read_le32(buffer + 340);
    if (read_le32(buffer + 332) != IMG_HEADER_SIZE || !meta_size ||
        meta_size > IMG_MEMBER_COUNT * IMG_META_SIZE || meta_size % IMG_META_SIZE ||
        files != IMG_HEADER_SIZE + meta_size || files > (uint32_t)length ||
        read_le32(buffer + 344) != (uint32_t)length - files)
        return false;

    uint32_t seen = 0, next_payload = files;
    for (uint32_t offset = IMG_HEADER_SIZE; offset < files; offset += IMG_META_SIZE)
    {
        if (fseek(file, (long)offset, SEEK_SET) || fread(buffer, 1, IMG_META_SIZE, file) != IMG_META_SIZE ||
            memcmp(buffer, "META\0\0\0", 8))
            return false;
        unsigned member = 0;
        while (member < IMG_MEMBER_COUNT && !field_equals(buffer + 8, member_names[member]))
            ++member;
        if (member == IMG_MEMBER_COUNT || (seen & (1U << member)))
            return false;
        seen |= 1U << member;
        struct image_part part = { read_le32(buffer + 136), read_le32(buffer + 140), read_le32(buffer + 144) };
        if (!part.size || part.offset > (uint32_t)length || part.size > (uint32_t)length - part.offset)
            return false;
        if (member == IMG_INFO)
        {
            if (part.offset || part.size != IMG_HEADER_SIZE)
                return false;
        }
        else
        {
            if (part.offset != next_payload)
                return false;
            next_payload += (part.size + OTA_PAGE - 1) & ~(OTA_PAGE - 1);
        }
        if (member >= IMG_SPL &&
            (!field_equals(buffer + 72, target_layout[member].name) ||
             read_le32(buffer + 104) != target_layout[member].offset ||
             read_le32(buffer + 108) != target_layout[member].capacity ||
             !field_equals(buffer + 112, "spi-nand") || read_le32(buffer + 132) ||
             part.size > target_layout[member].capacity))
            return false;
        if (member == IMG_OS)
            image->os = part;
        else if (member == IMG_RODATA)
            image->rodata = part;
        image->metadata_crc = crc32(image->metadata_crc, buffer, IMG_META_SIZE);
        if (!check_component(file, buffer, part))
            return false;
    }
    const uint32_t required = (1U << IMG_DDR) | (1U << IMG_LOADER) | (1U << IMG_INFO) |
                              (1U << IMG_SPL) | (1U << IMG_OS) | (1U << IMG_RODATA);
    return (seen & required) == required && next_payload == (uint32_t)length;
}

static bool download(FILE *file, unsigned char *buffer)
{
    struct statfs fs;
    if (dfs_statfs("/data", &fs))
        return false;
    uint64_t free_space = (uint64_t)fs.f_bfree * fs.f_bsize;
    uint32_t limit = free_space < OTA_MAX_SIZE ? (uint32_t)free_space : OTA_MAX_SIZE;
    struct webclient_session *session = http_get(status.source);
    if (!session)
        return false;
    int length = webclient_content_length_get(session);
    uint32_t total = 0;
    bool complete = false;
    if (length < 0 || (length >= (int)IMG_HEADER_SIZE && (uint32_t)length <= limit))
    {
        while (!cancelled() && carplay_network_valid() && !carplay_active())
        {
            int count = webclient_read(session, buffer, OTA_CHUNK);
            if (count <= 0)
            {
                complete = count == 0 && (length < 0 || total == (uint32_t)length);
                break;
            }
            if ((uint32_t)count > limit - total ||
                fwrite(buffer, 1, (size_t)count, file) != (size_t)count)
                break;
            total += (uint32_t)count;
            set_status(CARPLAY_OTA_DOWNLOADING, length > 0 ? (int)((uint64_t)total * 100 / length) : -1,
                       "Downloaded %u KB.", (unsigned)(total / 1024));
        }
    }
    webclient_close(session);
    return complete && carplay_network_valid() && !cancelled() && !fflush(file);
}

static void download_worker(void)
{
    if (!read_slot())
    {
        set_status(CARPLAY_OTA_FAILED, -1, "A/B slots unavailable. Check the USB installation baseline.");
        return;
    }
    if (uses_tls(status.source) && time(NULL) < 1704067200)
    {
        set_status(CARPLAY_OTA_FAILED, -1, "Set the clock before using HTTPS.");
        return;
    }
    if (carplay_active() || !carplay_network_acquire(!uses_tls(status.source)))
    {
        set_status(CARPLAY_OTA_FAILED, -1, "Connect Ethernet or Wi-Fi and disconnect CarPlay.");
        return;
    }
    rt_mutex_take(&stage_guard, RT_WAITING_FOREVER);
    unsigned char *buffer = rt_malloc(OTA_CHUNK);
    FILE *file = buffer ? fopen(RTCARPLAY_OTA_STAGE, "wb+") : NULL;
    bool complete = file && download(file, buffer);
    carplay_network_release();
    if (complete)
        set_status(CARPLAY_OTA_VERIFYING, -1, "Checking image layout and component checksums.");
    bool verified = complete && !cancelled() && verify_image(file, buffer, &staged);
    if (file && fclose(file))
        verified = false;
    rt_free(buffer);
    if (!verified)
        remove(RTCARPLAY_OTA_STAGE);
    rt_mutex_release(&stage_guard);
    if (!verified)
    {
        bool was_cancelled = cancelled();
        set_status(was_cancelled ? CARPLAY_OTA_IDLE : CARPLAY_OTA_FAILED, -1,
                   was_cancelled ? "Download cancelled." : complete ? "Invalid or incompatible firmware image."
                                                                    : "Download failed. Check URL, network and free space.");
        return;
    }
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    strcpy(status.remote, staged.version);
    rt_mutex_release(&ota_mutex);
    set_status(CARPLAY_OTA_READY_TO_INSTALL, 100, "Verified image ready to install.");
}

static bool nand_capacity(const char *name, uint32_t bytes)
{
    rt_device_t device = rt_device_find(name);
    if (!device || device->type != RT_Device_Class_MTD)
        return false;
    struct rt_mtd_nand_device *nand = (struct rt_mtd_nand_device *)device;
    if (nand->page_size != OTA_PAGE || nand->pages_per_block != 64)
        return false;
    uint32_t good = 0;
    for (uint32_t block = 0; block < nand->block_total; ++block)
        if (rt_mtd_nand_check_block(nand, block) == RT_EOK)
            good += OTA_PAGE * 64;
    return bytes <= good;
}

static bool flash_part(FILE *file, struct image_part part, char *name,
                       unsigned char *buffer, unsigned char *readback, unsigned index)
{
    if (carplay_active() || fseek(file, (long)part.offset, SEEK_SET) ||
        aic_ota_find_part(name) || aic_ota_erase_part() || !nand_capacity(name, part.size))
        return false;
    uint32_t crc = 0;
    for (uint32_t written = 0; written < part.size;)
    {
        uint32_t count = part.size - written;
        if (count > OTA_CHUNK)
            count = OTA_CHUNK;
        uint32_t aligned = (count + OTA_PAGE - 1) & ~(OTA_PAGE - 1);
        memset(buffer, 0xff, aligned);
        if (carplay_active() || fread(buffer, 1, count, file) != count ||
            aic_ota_part_write(written, buffer, aligned) ||
            aic_ota_part_read(written, readback, aligned) || memcmp(buffer, readback, aligned))
            return false;
        crc = crc32(crc, readback, count);
        written += count;
        set_status(CARPLAY_OTA_APPLYING, (int)(index * 50 + (uint64_t)written * 50 / part.size),
                   "Writing inactive A/B slots. Keep power connected.");
    }
    return crc == part.crc;
}

static void install_worker(void)
{
    char slot = read_slot();
    if (!slot || aic_get_boot_device() != BD_SPINAND || carplay_active() ||
        !carplay_connection_maintenance_acquire())
    {
        set_status(CARPLAY_OTA_READY_TO_INSTALL, -1, "Leave STA maintenance, disconnect CarPlay and check A/B baseline.");
        return;
    }
    rt_mutex_take(&stage_guard, RT_WAITING_FOREVER);
    FILE *file = fopen(RTCARPLAY_OTA_STAGE, "rb");
    unsigned char *buffer = rt_malloc(2 * OTA_CHUNK);
    struct update_image image;
    char *os = slot == 'A' ? "os_r" : "os";
    char *rodata = slot == 'A' ? "rodata_r" : "rodata";
    bool success = false;
    if (!file || !buffer || !verify_image(file, buffer, &image) ||
        image.metadata_crc != staged.metadata_crc)
        set_status(CARPLAY_OTA_FAILED, -1, "Staged image verification failed. Download it again.");
    else if (!nand_capacity(os, image.os.size) || !nand_capacity(rodata, image.rodata.size))
        set_status(CARPLAY_OTA_FAILED, -1, "Inactive slots have insufficient usable NAND space.");
    else
    {
        set_status(CARPLAY_OTA_APPLYING, 0, "Writing inactive A/B slots. Keep power connected.");
        if (!flash_part(file, image.os, os, buffer, buffer + OTA_CHUNK, 0) ||
            !flash_part(file, image.rodata, rodata, buffer, buffer + OTA_CHUNK, 1) || carplay_active())
            set_status(CARPLAY_OTA_FAILED, -1, "Inactive-slot write or readback failed. Current slot retained.");
        else
        {
            aic_reset_upgrade_status();
            aic_set_upgrade_status("d21x_os.itb");
            aic_set_upgrade_status("rodata.fatfs");
            success = aic_upgrade_end() == 0 && commit_recorded(slot);
            aic_reset_upgrade_status();
            set_status(success ? CARPLAY_OTA_REBOOTING : CARPLAY_OTA_FAILED, success ? 100 : -1,
                       success ? "Restarting into the inactive slots." : "A/B commit failed. Inspect the boot environment.");
        }
    }
    rt_free(buffer);
    if (file)
        fclose(file);
    remove(RTCARPLAY_OTA_STAGE);
    rt_mutex_release(&stage_guard);
    if (success)
    {
        rt_thread_mdelay(400);
        rt_hw_cpu_reset();
    }
    carplay_connection_maintenance_release();
}

static void source_worker(void)
{
    const char *temporary = RTCARPLAY_OTA_SOURCE_FILE ".tmp";
    const char *backup = RTCARPLAY_OTA_SOURCE_FILE ".bak";
    rt_mutex_take(&stage_guard, RT_WAITING_FOREVER);
    FILE *file = fopen(temporary, "wb");
    bool ok = false;
    if (file)
    {
        ok = fputs(requested_source, file) >= 0 && !fflush(file) && !fsync(fileno(file));
        if (fclose(file))
            ok = false;
    }
    if (ok && rename(temporary, RTCARPLAY_OTA_SOURCE_FILE))
    {
        FILE *old = fopen(RTCARPLAY_OTA_SOURCE_FILE, "rb");
        if (old)
        {
            fclose(old);
            remove(backup);
            ok = rename(RTCARPLAY_OTA_SOURCE_FILE, backup) == 0;
        }
        if (ok && rename(temporary, RTCARPLAY_OTA_SOURCE_FILE))
        {
            rename(backup, RTCARPLAY_OTA_SOURCE_FILE);
            ok = false;
        }
    }
    if (ok)
    {
        remove(backup);
        remove(RTCARPLAY_OTA_STAGE);
        rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
        strcpy(status.source, requested_source);
        status.remote[0] = 0;
        ++status.candidate;
        rt_mutex_release(&ota_mutex);
        set_status(CARPLAY_OTA_IDLE, -1, "Firmware URL saved.");
    }
    else
    {
        remove(temporary);
        set_status(status.state, -1, "Could not save the firmware URL in /data.");
    }
    rt_mutex_release(&stage_guard);
}

static bool mounted(const char *path)
{
    struct dfs_filesystem *fs = dfs_filesystem_lookup(path);
    return fs && !strcmp(fs->path, path);
}

static void confirm_boot(void)
{
    if (fw_env_open())
        return;
    const char *trial = fw_getenv("upgrade_available");
    const char *result = fw_getenv("carplay_update_result");
    if (!trial || strcmp(trial, "1"))
    {
        bool fallback = result && !strcmp(result, "boot-limit");
        fw_env_close();
        if (fallback)
            set_status(CARPLAY_OTA_FAILED, -1, "Previous trial exceeded the boot limit. Restored the confirmed slots.");
        return;
    }
    set_status(CARPLAY_OTA_TRIAL_BOOT, -1, "Trial boot. Checking local services and slots.");
    const char *os = fw_getenv("osAB_now");
    const char *rodata = fw_getenv("rodataAB_now");
    const char *next_os = fw_getenv("osAB_next");
    const char *next_rodata = fw_getenv("rodataAB_next");
    bool healthy = mounted("/rodata") && mounted("/data") && os && rodata && next_os && next_rodata &&
                   (!strcmp(os, "A") || !strcmp(os, "B")) &&
                   !strcmp(os, rodata) && !strcmp(os, next_os) && !strcmp(os, next_rodata);
    if (healthy)
    {
        char confirmed[] = { *os, 0 };
        healthy = !fw_env_write("carplay_confirmed_slot", confirmed) &&
                  !fw_env_write("carplay_update_result", "confirmed") &&
                  !fw_env_write("bootcount", "0") && !fw_env_write("upgrade_available", "0") &&
                  !fw_env_flush();
    }
    fw_env_close();
    set_status(healthy ? CARPLAY_OTA_CONFIRMED : CARPLAY_OTA_TRIAL_BOOT, -1,
               healthy ? "Trial confirmed." : "Trial remains pending. Check mounts, slots and boot environment.");
}

static void ota_worker(void *arg)
{
    switch ((enum ota_operation)(uintptr_t)arg)
    {
    case OTA_DOWNLOAD:
        download_worker();
        break;
    case OTA_INSTALL:
        install_worker();
        break;
    case OTA_SOURCE:
        source_worker();
        break;
    case OTA_CONFIRM:
        confirm_boot();
        break;
    }
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    worker_active = false;
    rt_mutex_release(&ota_mutex);
}

static int launch(enum ota_operation operation, uint32_t candidate, const char *source)
{
    if (!initialized)
        return -RT_ERROR;
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    if (worker_active || status.state == CARPLAY_OTA_REBOOTING)
    {
        rt_mutex_release(&ota_mutex);
        return -RT_EBUSY;
    }
    if ((operation == OTA_DOWNLOAD && !status.source[0]) ||
        (operation == OTA_INSTALL && (status.state != CARPLAY_OTA_READY_TO_INSTALL || candidate != status.candidate)))
    {
        rt_mutex_release(&ota_mutex);
        return -RT_EINVAL;
    }
    worker_active = true;
    cancel_requested = false;
    if (operation == OTA_SOURCE)
        strcpy(requested_source, source);
    else if (operation == OTA_DOWNLOAD)
    {
        ++status.candidate;
        status.remote[0] = 0;
        status.state = CARPLAY_OTA_DOWNLOADING;
        status.progress = -1;
        strcpy(status.message, "Connecting to firmware server.");
    }
    else if (operation == OTA_INSTALL)
    {
        status.state = CARPLAY_OTA_VERIFYING;
        status.progress = -1;
    }
    rt_mutex_release(&ota_mutex);
    rt_thread_t thread = rt_thread_create("cpota", ota_worker, (void *)(uintptr_t)operation, 16384, 22, 10);
    if (thread && rt_thread_startup(thread) == RT_EOK)
        return RT_EOK;
    if (thread)
        rt_thread_delete(thread);
    set_status(CARPLAY_OTA_FAILED, -1, "Cannot start the update worker.");
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    worker_active = false;
    rt_mutex_release(&ota_mutex);
    return -RT_ERROR;
}

void carplay_ota_boot_health_ready(void)
{
    if (!boot_health_checked && launch(OTA_CONFIRM, 0, NULL) == RT_EOK)
        boot_health_checked = true;
}

int carplay_ota_download(void)
{
    return launch(OTA_DOWNLOAD, 0, NULL);
}

int carplay_ota_install(uint32_t candidate)
{
    return launch(OTA_INSTALL, candidate, NULL);
}

int carplay_ota_cancel(void)
{
    if (!initialized)
        return -RT_ERROR;
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    bool running = worker_active && status.state == CARPLAY_OTA_DOWNLOADING;
    if (running)
        cancel_requested = true;
    rt_mutex_release(&ota_mutex);
    return running ? RT_EOK : -RT_EBUSY;
}

int carplay_ota_source_set(const char *url)
{
    return image_url(url) ? launch(OTA_SOURCE, 0, url) : -RT_EINVAL;
}

static int ota_command(int argc, char **argv)
{
    carplay_ota_status_t out;
    carplay_ota_status(&out);
    if (argc == 2 && !strcmp(argv[1], "download"))
        return carplay_ota_download();
    if (argc == 2 && !strcmp(argv[1], "cancel"))
        return carplay_ota_cancel();
    if (argc == 3 && !strcmp(argv[1], "install"))
    {
        char candidate[16];
        snprintf(candidate, sizeof(candidate), "%u", (unsigned)out.candidate);
        if (!strcmp(candidate, argv[2]))
            return carplay_ota_install(out.candidate);
    }
    if (argc == 3 && !strcmp(argv[1], "source"))
        return carplay_ota_source_set(argv[2]);
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "status")))
    {
        rt_kprintf("Firmware %s (%.8s), slot %s, image %s\n%s\n%s\n", out.current,
                   out.current_commit, out.slot, out.remote, out.source, out.message);
        if (out.state == CARPLAY_OTA_READY_TO_INSTALL)
            rt_kprintf("Confirm with: carplay_ota install %u\n", (unsigned)out.candidate);
        return RT_EOK;
    }
    rt_kprintf("usage: carplay_ota status|source <img-url>|download|cancel|install <candidate>\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(ota_command, carplay_ota, Update inactive A / B slots from a firmware image);

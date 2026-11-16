/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Application-owned update policy; webclient, mbedTLS and SDK A/B flash
 * operations retain their existing implementations. No flash writes until
 * the complete staged package matches the confirmed release manifest. */
#include <rtthread.h>
#include <rthw.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <webclient.h>
#include <mbedtls/sha256.h>
#include <ota.h>
#include <env.h>
#include <absystem_os.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include "rtcp/rtcp.h"
#include "carplay_app.h"
#include "ota_lubanlite.h"

#define OTA_CHUNK    4096
#define OTA_MAX_SIZE (8U * 1024U * 1024U)
#ifndef RTCARPLAY_OTA_STAGE
#define RTCARPLAY_OTA_STAGE "/data/.carplay-update.cpio"
#endif
#define STRINGIFY_(x) #x
#define STRINGIFY(x)  STRINGIFY_(x)
#ifndef RTCARPLAY_FW_VERSION
#define RTCARPLAY_FW_VERSION unknown
#endif
#ifndef RTCARPLAY_BUILD_COMMIT
#define RTCARPLAY_BUILD_COMMIT unknown
#endif

static struct rt_mutex ota_mutex;
static int initialized;
static int worker_active;
static carplay_ota_status_t status;
static uint32_t package_size;
static char package_sha[65];

static int busy(carplay_ota_state_t state)
{
    return state == CARPLAY_OTA_CHECKING || state == CARPLAY_OTA_DOWNLOADING ||
           state == CARPLAY_OTA_APPLYING || state == CARPLAY_OTA_REBOOTING;
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
    if (rt_mutex_init(&ota_mutex, "cpota", RT_IPC_FLAG_PRIO) != RT_EOK)
        return -RT_ENOMEM;
    snprintf(status.current, sizeof(status.current), "%s", STRINGIFY(RTCARPLAY_FW_VERSION));
    snprintf(status.current_commit, sizeof(status.current_commit), "%s", STRINGIFY(RTCARPLAY_BUILD_COMMIT));
    snprintf(status.slot, sizeof(status.slot), "?");
    snprintf(status.message, sizeof(status.message), "Connect Ethernet, then check for updates.");
    status.progress = -1;
    initialized = 1;
    return RT_EOK;
}
INIT_APP_EXPORT(ota_service_init);

void carplay_ota_status(carplay_ota_status_t *out)
{
    if (!out)
        return;
    if (!initialized)
    {
        memset(out, 0, sizeof(*out));
        out->state = CARPLAY_OTA_FAILED;
        out->progress = -1;
        snprintf(out->message, sizeof(out->message), "Update service is unavailable.");
        return;
    }
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    *out = status;
    out->busy = worker_active || busy(status.state);
    rt_mutex_release(&ota_mutex);
}
static int network_ready(void)
{
    int ready;
    LOCK_TCPIP_CORE();
    struct netif *wired = netif_find("e0");
    ready = wired && wired == netif_default && netif_is_up(wired) && netif_is_link_up(wired) &&
            !ip4_addr_isany_val(*netif_ip4_addr(wired)) && !ip4_addr_isany_val(*netif_ip4_gw(wired));
    UNLOCK_TCPIP_CORE();
    return ready;
}
static int carplay_active(void)
{
    rtcp_receiver_state_t state;
    return carplay_app_is_running() &&
           (carplay_app_session_state(&state) != RTCP_OK || state.transport_connected || state.session_established);
}
/* Environment I/O is worker-only, never part of the GUI status getter. */
static int read_slot(void)
{
    char slot = 0;
    if (fw_env_open() == 0)
    {
        const char *os = fw_getenv("osAB_now");
        if (os && (strcmp(os, "A") == 0 || strcmp(os, "B") == 0))
            slot = os[0];
        const char *rodata = fw_getenv("rodataAB_now");
        if (!rodata || rodata[0] != slot || rodata[1])
            slot = 0;
        fw_env_close();
    }
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    snprintf(status.slot, sizeof(status.slot), "%c", slot ? slot : '?');
    rt_mutex_release(&ota_mutex);
    return slot;
}
/* The SDK does not propagate fw_env_flush() failures. Reopen the stored
 * environment and require the intended next slots before requesting reset. */
static int commit_recorded(char current)
{
    int recorded = 0;
    const char next = current == 'A' ? 'B' : 'A';
    if (fw_env_open() == 0)
    {
        const char *os = fw_getenv("osAB_next");
        const char *rodata = fw_getenv("rodataAB_next");
        const char *available = fw_getenv("upgrade_available");
        recorded = os && os[0] == next && !os[1] &&
                   rodata && rodata[0] == next && !rodata[1] &&
                   available && !strcmp(available, "1");
        fw_env_close();
    }
    return recorded;
}
static int hex_string(const char *s, size_t length)
{
    if (strlen(s) != length)
        return 0;
    for (size_t i = 0; i < length; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return 1;
}
/* Exact keys, no duplicate or truncated values. The nightly manifest is
 * deliberately strict: old version-only files cannot authorize flashing. */
static int value(const char *text, const char *key, char *out, size_t capacity)
{
    size_t keylen = strlen(key);
    int found = 0;
    out[0] = 0;
    for (const char *p = text; *p;)
    {
        const char *end = strchr(p, '\n');
        if (!end)
            end = p + strlen(p);
        const char *trim = end;
        if (trim > p && trim[-1] == '\r')
            --trim;
        if ((size_t)(trim - p) > keylen && !memcmp(p, key, keylen) && p[keylen] == '=')
        {
            size_t n = (size_t)(trim - p) - keylen - 1;
            if (found || !n || n >= capacity)
                return 0;
            memcpy(out, p + keylen + 1, n);
            out[n] = 0;
            found = 1;
        }
        p = *end ? end + 1 : end;
    }
    return found;
}
static struct webclient_session *http_get(const char *url)
{
    if (strncmp(url, "https://", 8))
        return NULL;
    struct webclient_session *s = webclient_session_create(WEBCLIENT_HEADER_BUFSZ);
    if (!s)
        return NULL;
    webclient_set_timeout(s, 60000);
    if (webclient_get(s, url) != 200 || !s->is_tls)
    {
        webclient_close(s);
        return NULL;
    }
    return s;
}
static void check_worker(void *arg)
{
    (void)arg;
    char text[512], version[24], commit[41], sha[65], size[16], board[48];
    size_t used = 0;
    int n = -1;
    struct webclient_session *s = NULL;
    if (!network_ready())
        goto failed_network;
    if (!read_slot())
    {
        set_status(CARPLAY_OTA_FAILED, -1, "A/B slots are unavailable or inconsistent.");
        return;
    }
    s = http_get(RTCARPLAY_OTA_VERSION_URL);
    if (!s)
        goto failed_manifest;
    while (used < sizeof(text) - 1)
    {
        n = webclient_read(s, text + used, sizeof(text) - 1 - used);
        if (n <= 0)
            break;
        used += (size_t)n;
    }
    int content = webclient_content_length_get(s);
    webclient_close(s);
    s = NULL;
    text[used] = 0;
    if (n != 0 || !used || used >= sizeof(text) - 1 || (content >= 0 && used != (size_t)content) ||
        strlen(text) != used || !value(text, "version", version, sizeof(version)) ||
        !value(text, "commit", commit, sizeof(commit)) || !hex_string(commit, 40) ||
        !value(text, "sha256", sha, sizeof(sha)) || !hex_string(sha, 64) ||
        !value(text, "board", board, sizeof(board)) || strcmp(board, "d21x_demo128-nand-carplay") ||
        !value(text, "size", size, sizeof(size)))
        goto failed_manifest;
    uint32_t expected = 0;
    for (const char *p = size; *p; ++p)
    {
        if (*p < '0' || *p > '9' || expected > OTA_MAX_SIZE / 10)
            goto failed_manifest;
        expected = expected * 10 + (uint32_t)(*p - '0');
        if (expected > OTA_MAX_SIZE)
            goto failed_manifest;
    }
    if (expected < 512)
        goto failed_manifest;
    for (const char *p = version; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || *p == '.'))
            goto failed_manifest;
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    strcpy(status.remote, version);
    strcpy(status.remote_commit, commit);
    strcpy(package_sha, sha);
    package_size = (uint32_t)expected;
    int same = !strcmp(commit, status.current_commit);
    status.state = same ? CARPLAY_OTA_UP_TO_DATE : CARPLAY_OTA_AVAILABLE;
    snprintf(status.message, sizeof(status.message), same ? "Installed build %.8s matches the preview." : "Preview build %.8s is available. Disconnect CarPlay before installing.", commit);
    rt_mutex_release(&ota_mutex);
    return;
failed_network:
    set_status(CARPLAY_OTA_FAILED, -1, "Ethernet e0 needs a link, IPv4 address and default gateway.");
    return;
failed_manifest:
    if (s)
        webclient_close(s);
    set_status(CARPLAY_OTA_FAILED, -1, "Release manifest is unavailable, incomplete or incompatible.");
}
static int download(FILE *file)
{
    struct webclient_session *s = http_get(RTCARPLAY_OTA_URL);
    if (!s)
        return -RT_ERROR;
    int length = webclient_content_length_get(s), n = -1, result = -RT_ERROR;
    uint32_t total = 0;
    unsigned char *buffer = rt_malloc(OTA_CHUNK), digest[32];
    char hex[65];
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    if (!buffer || (length >= 0 && (uint32_t)length != package_size) ||
        mbedtls_sha256_starts_ret(&hash, 0))
        goto out;
    while ((n = webclient_read(s, buffer, OTA_CHUNK)) > 0)
    {
        if (!network_ready() || carplay_active() || (uint32_t)n > package_size - total ||
            fwrite(buffer, 1, (size_t)n, file) != (size_t)n)
            goto out;
        total += (uint32_t)n;
        set_status(CARPLAY_OTA_DOWNLOADING, (int)((uint64_t)total * 100 / package_size),
                   "Downloading and verifying: %u / %u KB", total / 1024, package_size / 1024);
    }
    if (n != 0 || total != package_size || fflush(file) || fseek(file, 0, SEEK_SET))
        goto out;
    /* Hash the stored bytes that will actually be supplied to the updater. */
    total = 0;
    while (total < package_size)
    {
        size_t want = package_size - total;
        if (want > OTA_CHUNK)
            want = OTA_CHUNK;
        size_t got = fread(buffer, 1, want, file);
        if (got != want || mbedtls_sha256_update_ret(&hash, buffer, got))
            goto out;
        total += (uint32_t)got;
    }
    if (mbedtls_sha256_finish_ret(&hash, digest))
        goto out;
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    if (strcmp(hex, package_sha) || fflush(file) || fseek(file, 0, SEEK_SET))
        goto out;
    result = RT_EOK;
out:
    mbedtls_sha256_free(&hash);
    if (buffer)
        rt_free(buffer);
    webclient_close(s);
    return result;
}
static void install_worker(void *arg)
{
    (void)arg;
    FILE *file = NULL;
    unsigned char *buffer = NULL;
    int started = 0, success = 0;
    int slot = read_slot();
    if (!network_ready() || carplay_active() || !slot)
    {
        set_status(CARPLAY_OTA_FAILED, -1, "Connect Ethernet, disconnect CarPlay and verify A/B slots.");
        return;
    }
    file = fopen(RTCARPLAY_OTA_STAGE, "wb+");
    if (!file)
    {
        set_status(CARPLAY_OTA_FAILED, -1, "Cannot stage update in /data; check free space.");
        return;
    }
    if (download(file) != RT_EOK)
    {
        set_status(CARPLAY_OTA_FAILED, -1, "Download, storage or SHA256 check failed. Check for updates again.");
        goto out;
    }
    if (carplay_active())
    {
        set_status(CARPLAY_OTA_FAILED, -1, "Disconnect CarPlay before installing.");
        goto out;
    }
    buffer = rt_malloc(OTA_CHUNK);
    if (!buffer || ota_init() != RT_EOK)
    {
        set_status(CARPLAY_OTA_FAILED, -1, "Cannot initialize the flash updater.");
        goto out;
    }
    started = 1;
    set_status(CARPLAY_OTA_APPLYING, 0, "Package verified. Writing inactive A/B slot; keep power connected.");
    uint32_t total = 0;
    while (total < package_size)
    {
        size_t want = package_size - total;
        if (want > OTA_CHUNK)
            want = OTA_CHUNK;
        size_t n = fread(buffer, 1, want, file);
        if (carplay_active() || n != want || ota_shard_download_fun((char *)buffer, (int)n) < 0)
        {
            set_status(CARPLAY_OTA_FAILED, -1, "Inactive-slot write failed. Current slot retained.");
            goto out;
        }
        total += (uint32_t)n;
        set_status(CARPLAY_OTA_APPLYING, (int)((uint64_t)total * 100 / package_size),
                   "Writing inactive A/B slot; keep power connected.");
    }
    if (carplay_active())
    {
        set_status(CARPLAY_OTA_FAILED, -1, "CarPlay became active. Update not committed; disconnect and retry.");
        goto out;
    }
    if (aic_upgrade_end() != 0 || !commit_recorded((char)slot))
    {
        set_status(CARPLAY_OTA_FAILED, -1, "A/B environment update failed; inspect boot state before retrying.");
        goto out;
    }
    success = 1;
    set_status(CARPLAY_OTA_REBOOTING, 100, "Update written. Rebooting into the inactive slot...");
out:
    if (started)
        ota_deinit();
    if (buffer)
        rt_free(buffer);
    if (file)
        fclose(file);
    remove(RTCARPLAY_OTA_STAGE);
    if (success)
    {
        rt_thread_mdelay(400);
        rt_hw_cpu_reset();
    }
}
static void ota_worker(void *arg)
{
    if ((uintptr_t)arg)
        install_worker(NULL);
    else
        check_worker(NULL);
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    worker_active = 0;
    rt_mutex_release(&ota_mutex);
}
/* Reserve state before scheduling: all callers (including CLI) share one gate.
 * The installation target must still be the exact commit shown for consent. */
static int launch(int install, const char *expected_commit)
{
    if (!initialized)
        return -RT_ERROR;
    rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
    if (worker_active || busy(status.state))
    {
        rt_mutex_release(&ota_mutex);
        return -RT_EBUSY;
    }
    if (install && (status.state != CARPLAY_OTA_AVAILABLE || !expected_commit ||
                    strcmp(expected_commit, status.remote_commit)))
    {
        rt_mutex_release(&ota_mutex);
        return -RT_EINVAL;
    }
    status.state = install ? CARPLAY_OTA_DOWNLOADING : CARPLAY_OTA_CHECKING;
    worker_active = 1;
    status.progress = install ? 0 : -1;
    snprintf(status.message, sizeof(status.message), "%s", install ? "Starting download..." : "Checking release...");
    rt_mutex_release(&ota_mutex);
    rt_thread_t thread = rt_thread_create("cpota", ota_worker,
                                          (void *)(uintptr_t)install, 16384, 22, 10);
    if (!thread)
    {
        set_status(CARPLAY_OTA_FAILED, -1, "Cannot create the update worker.");
        rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
        worker_active = 0;
        rt_mutex_release(&ota_mutex);
        return -RT_ENOMEM;
    }
    if (rt_thread_startup(thread) != RT_EOK)
    {
        rt_thread_delete(thread);
        set_status(CARPLAY_OTA_FAILED, -1, "Cannot start the update worker.");
        rt_mutex_take(&ota_mutex, RT_WAITING_FOREVER);
        worker_active = 0;
        rt_mutex_release(&ota_mutex);
        return -RT_ERROR;
    }
    return RT_EOK;
}
int carplay_ota_check(void)
{
    return launch(0, NULL);
}
int carplay_ota_install(const char *expected_commit)
{
    return launch(1, expected_commit);
}
static int ota_command(int argc, char **argv)
{
    carplay_ota_status_t out;
    carplay_ota_status(&out);
    if (argc == 2 && !strcmp(argv[1], "check"))
        return carplay_ota_check();
    if (argc == 2 && !strcmp(argv[1], "install"))
        return carplay_ota_install(out.remote_commit);
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "status")))
    {
        rt_kprintf("Firmware %s (%.8s), slot %s, remote %s (%.8s)\n%s\n", out.current,
                   out.current_commit, out.slot, out.remote, out.remote_commit, out.message);
        return RT_EOK;
    }
    rt_kprintf("usage: carplay_ota [status|check|install]\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(ota_command, carplay_ota, Check or install a verified wired A / B update);

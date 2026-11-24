#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual IMG update worker with modeled HTTP, NAND and RTOS."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import zlib

app = Path(__file__).resolve().parents[1]
sdk = app.parents[2]
headers = {
    "rtthread.h": """
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
struct rt_mutex { bool locked; };
struct test_thread;
typedef struct test_thread *rt_thread_t;
#define RT_EOK 0
#define RT_ERROR 1
#define RT_ENOMEM 12
#define RT_EBUSY 16
#define RT_EINVAL 22
#define RT_WAITING_FOREVER (-1)
#define RT_IPC_FLAG_PRIO 1
#define INIT_APP_EXPORT(fn)
#define MSH_CMD_EXPORT_ALIAS(fn, ...) static void *fn##_used __attribute__((used)) = (void *)&fn;
#define rt_malloc malloc
#define rt_free free
int rt_mutex_init(struct rt_mutex *, const char *, int);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
int rt_kprintf(const char *, ...);
rt_thread_t rt_thread_create(const char *, void (*)(void *), void *, unsigned, unsigned, unsigned);
int rt_thread_startup(rt_thread_t);
int rt_thread_delete(rt_thread_t);
void rt_thread_mdelay(int);
""",
    "rthw.h": "void rt_hw_cpu_reset(void);\n",
    "aic_common.h": "#include <stddef.h>\n#include <stdint.h>\n",
    "aic_core.h": "#define cpu_to_le32(value) (value)\n#define le32_to_cpu(value) (value)\n",
    "boot_param.h": "enum { BD_SPINAND }; int aic_get_boot_device(void);\n",
    "rtdevice.h": """
#pragma once
enum { RT_Device_Class_MTD };
typedef struct rt_device { int type; } *rt_device_t;
rt_device_t rt_device_find(const char *);
""",
    "drivers/mtd_nand.h": """
struct rt_mtd_nand_device { struct rt_device parent; unsigned page_size, pages_per_block, block_total; };
int rt_mtd_nand_check_block(struct rt_mtd_nand_device *, unsigned);
""",
    "env.h": """
int fw_env_open(void);
const char *fw_getenv(const char *);
void fw_env_close(void);
int fw_env_write(char *, char *);
int fw_env_flush(void);
""",
    "dfs_fs.h": """
struct statfs { unsigned f_bsize, f_bfree; };
struct dfs_filesystem { const char *path; };
int dfs_statfs(const char *, struct statfs *);
struct dfs_filesystem *dfs_filesystem_lookup(const char *);
""",
    "webclient.h": """
#define WEBCLIENT_HEADER_BUFSZ 2048
struct webclient_session { int is_tls; size_t offset; const char *outbound_netif; };
struct webclient_session *webclient_session_create(unsigned);
void webclient_set_timeout(struct webclient_session *, int);
int webclient_get_once(struct webclient_session *, const char *);
const char *webclient_header_fields_get(struct webclient_session *, const char *);
int webclient_read(struct webclient_session *, void *, unsigned long);
int webclient_content_length_get(struct webclient_session *);
void webclient_close(struct webclient_session *);
""",
}
source = r'''
#include <assert.h>
#include <string.h>
#define RTCARPLAY_OTA_STAGE "stage.img"
#define RTCARPLAY_OTA_SOURCE_FILE "source.cfg"
#include "ota_lubanlite.c"

struct test_thread { void (*entry)(void *); void *arg; };
static rt_thread_t pending;
static unsigned char *body;
static size_t body_size;
static struct rt_mtd_nand_device nand = {{RT_Device_Class_MTD}, 2048, 64, 96};
static unsigned char flash[4096];
static unsigned char masks, upgraded, erases, commits, resets;
static bool active, maintenance, route = true, cancel_read, readback_failure, commit_failure;
static bool available, unknown_length;
static char current_slot = 'A';

static char *current(void)
{
    static char value[2];
    value[0] = current_slot;
    return value;
}
int fw_env_open(void) { return 0; }
void fw_env_close(void) {}
int fw_env_write(char *key, char *value) { (void)key; (void)value; return 0; }
int fw_env_flush(void) { return 0; }
const char *fw_getenv(const char *name)
{
    if (!strcmp(name, "carplay_ab_baseline")) return "2";
    if (!strcmp(name, "upgrade_available")) return available ? "1" : "0";
    if (strstr(name, "_next") && available) return current_slot == 'A' ? "B" : "A";
    return current();
}

int rt_mutex_init(struct rt_mutex *mutex, const char *name, int flags)
{ (void)name; (void)flags; mutex->locked = false; return 0; }
int rt_mutex_take(struct rt_mutex *mutex, int wait)
{ (void)wait; assert(!mutex->locked); mutex->locked = true; return 0; }
int rt_mutex_release(struct rt_mutex *mutex) { assert(mutex->locked); mutex->locked = false; return 0; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
rt_thread_t rt_thread_create(const char *name, void (*entry)(void *), void *arg,
                             unsigned stack, unsigned priority, unsigned ticks)
{
    (void)name; (void)stack; (void)priority; (void)ticks;
    rt_thread_t thread = malloc(sizeof(*thread));
    *thread = (struct test_thread){entry, arg};
    return thread;
}
int rt_thread_startup(rt_thread_t thread) { assert(!pending); pending = thread; return 0; }
int rt_thread_delete(rt_thread_t thread) { free(thread); return 0; }
void rt_thread_mdelay(int ms) { assert(ms == 400); }
void rt_hw_cpu_reset(void) { ++resets; }
static void run(void)
{
    assert(pending);
    rt_thread_t thread = pending;
    pending = NULL;
    thread->entry(thread->arg);
    free(thread);
    assert(!worker_active && !maintenance);
}

bool carplay_network_acquire(bool local) { assert(local == !uses_tls(status.source)); return route; }
bool carplay_network_valid(void) { return route; }
void carplay_network_release(void) {}
const char *carplay_network_lease_name(void) { return "w0"; }
bool carplay_app_is_running(void) { return active; }
int carplay_app_session_state(rtcp_receiver_state_t *state)
{ memset(state, 0, sizeof(*state)); state->transport_connected = active; return 0; }
bool carplay_connection_maintenance_acquire(void) { assert(!maintenance); maintenance = true; return true; }
void carplay_connection_maintenance_release(void) { assert(maintenance); maintenance = false; }
int dfs_statfs(const char *path, struct statfs *fs)
{ assert(!strcmp(path, "/data")); *fs = (struct statfs){512, 65536}; return 0; }
struct dfs_filesystem *dfs_filesystem_lookup(const char *path) { (void)path; return NULL; }

struct webclient_session *webclient_session_create(unsigned cap)
{ (void)cap; return calloc(1, sizeof(struct webclient_session)); }
void webclient_set_timeout(struct webclient_session *session, int ms)
{ (void)session; assert(ms == 60000); }
int webclient_get_once(struct webclient_session *session, const char *url)
{ assert(!strcmp(session->outbound_netif, "w0")); session->is_tls = uses_tls(url); return 200; }
const char *webclient_header_fields_get(struct webclient_session *session, const char *key)
{ (void)session; (void)key; return NULL; }
int webclient_read(struct webclient_session *session, void *out, unsigned long cap)
{
    assert(carplay_ota_download() == -RT_EBUSY);
    assert(stage_guard.locked);
    if (cancel_read) assert(!carplay_ota_cancel());
    size_t length = body_size - session->offset;
    if (length > cap) length = cap;
    memcpy(out, body + session->offset, length);
    session->offset += length;
    return (int)length;
}
int webclient_content_length_get(struct webclient_session *session)
{ (void)session; return unknown_length ? -1 : (int)body_size; }
void webclient_close(struct webclient_session *session) { free(session); }

int aic_get_boot_device(void) { return BD_SPINAND; }
int rt_mtd_nand_check_block(struct rt_mtd_nand_device *device, unsigned block)
{ assert(device == &nand && block < nand.block_total); return RT_EOK; }
rt_device_t rt_device_find(const char *name)
{
    assert(!strcmp(name, current_slot == 'A' ? "os_r" : "os") ||
           !strcmp(name, current_slot == 'A' ? "rodata_r" : "rodata"));
    return &nand.parent;
}
int aic_ota_find_part(char *name)
{
    assert(maintenance && stage_guard.locked);
    rt_device_find(name);
    masks |= !strncmp(name, "os", 2) ? 1 : 2;
    return 0;
}
int aic_ota_erase_part(void) { ++erases; return 0; }
int aic_ota_part_write(uint32_t addr, const uint8_t *buffer, size_t size)
{ (void)addr; assert(size <= sizeof(flash)); memcpy(flash, buffer, size); return 0; }
int aic_ota_part_read(uint32_t addr, uint8_t *buffer, size_t size)
{
    (void)addr;
    memcpy(buffer, flash, size);
    if (readback_failure) buffer[0] ^= 1;
    return 0;
}
void aic_reset_upgrade_status(void) { upgraded = 0; }
void aic_set_upgrade_status(char *name)
{
    assert(!strcmp(name, "d21x_os.itb") || !strcmp(name, "rodata.fatfs"));
    upgraded |= !strcmp(name, "d21x_os.itb") ? 1 : 2;
}
int aic_upgrade_end(void)
{
    assert(masks == 3 && upgraded == 3);
    ++commits;
    available = !commit_failure;
    return commit_failure ? -1 : 0;
}

static void download_and_expect(carplay_ota_state_t expected)
{
    assert(carplay_ota_download() == RT_EOK);
    run();
    assert(status.state == expected && erases == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *fixture = fopen(argv[1], "rb");
    assert(fixture && !fseek(fixture, 0, SEEK_END));
    body_size = (size_t)ftell(fixture);
    body = malloc(body_size);
    assert(!fseek(fixture, 0, SEEK_SET) && fread(body, 1, body_size, fixture) == body_size);
    fclose(fixture);
    assert(!ota_service_init() && !status.source[0]);
    assert(carplay_ota_download() == -RT_EINVAL);
    assert(carplay_ota_source_set("http://example.test/version.txt") == -RT_EINVAL);
    assert(carplay_ota_source_set("http://example.test/fw.img#fragment") == -RT_EINVAL);
    assert(image_url("https://example.test/fw.img?download=1"));
    assert(!allowed_redirect("https://github.com/any/fw.img", "http://github.com/fw.img"));
    assert(allowed_redirect("https://github.com/any/fw.img", "https://release-assets.githubusercontent.com/fw.img"));
    assert(!carplay_ota_source_set("http://192.168.1.20:8000/fw.img"));
    run();
    assert(!ota_service_init() && !strcmp(status.source, "http://192.168.1.20:8000/fw.img"));

    cancel_read = true;
    download_and_expect(CARPLAY_OTA_IDLE);
    cancel_read = false;
    --body_size;
    download_and_expect(CARPLAY_OTA_FAILED);
    ++body_size;
    body[72] ^= 1;
    download_and_expect(CARPLAY_OTA_FAILED);
    body[72] ^= 1;
    unsigned char *os = NULL, *rodata = NULL;
    for (unsigned char *meta = body + IMG_HEADER_SIZE; meta < body + read_le32(body + 340); meta += IMG_META_SIZE)
    {
        if (field_equals(meta + 8, "image.target.os")) os = meta;
        if (field_equals(meta + 8, "image.target.rodata")) rodata = meta;
    }
    assert(os && rodata);
    os[108] ^= 1;
    download_and_expect(CARPLAY_OTA_FAILED);
    os[108] ^= 1;
    unsigned char *payload = body + read_le32(os + 136);
    *payload ^= 1;
    download_and_expect(CARPLAY_OTA_FAILED);
    *payload ^= 1;
    unsigned char original[64];
    memcpy(original, rodata + 8, sizeof(original));
    memcpy(rodata + 8, os + 8, sizeof(original));
    download_and_expect(CARPLAY_OTA_FAILED);
    memcpy(rodata + 8, original, sizeof(original));
    unknown_length = true;
    download_and_expect(CARPLAY_OTA_READY_TO_INSTALL);
    assert(carplay_ota_install(status.candidate - 1) == -RT_EINVAL);
    FILE *stage = fopen(RTCARPLAY_OTA_STAGE, "rb+");
    assert(stage && !fseek(stage, (long)(payload - body), SEEK_SET));
    assert(fputc(*payload ^ 1, stage) != EOF && !fclose(stage));
    assert(!carplay_ota_install(status.candidate));
    run();
    assert(status.state == CARPLAY_OTA_FAILED && !erases);

    download_and_expect(CARPLAY_OTA_READY_TO_INSTALL);
    route = false;
    readback_failure = true;
    assert(!carplay_ota_install(status.candidate));
    run();
    assert(status.state == CARPLAY_OTA_FAILED && erases == 1 && !commits && !resets);
    readback_failure = false;
    route = true;
    erases = masks = 0;
    current_slot = 'B';
    download_and_expect(CARPLAY_OTA_READY_TO_INSTALL);
    commit_failure = true;
    assert(!carplay_ota_install(status.candidate));
    run();
    assert(status.state == CARPLAY_OTA_FAILED && erases == 2 && commits == 1 && !resets);
    commit_failure = false;
    erases = masks = 0;
    download_and_expect(CARPLAY_OTA_READY_TO_INSTALL);
    route = false;
    assert(!carplay_ota_install(status.candidate));
    run();
    assert(status.state == CARPLAY_OTA_REBOOTING && erases == 2 && commits == 2 && resets == 1);
    free(body);
    puts("IMG OTA validation, staged-image recheck, inactive NAND writes and A/B commit passed.");
}
'''


def image_fixture():
    parts = [
        ("updater.ddr", b"DDR", None),
        ("updater.spl", b"updater", None),
        ("info", None, None),
        ("target.spl", b"SPL", ("spl", 0, 0x100000)),
        ("target.os", b"OS" * 3000, ("os", 0x180000, 0x800000)),
        ("target.rodata", b"RODATA" * 800, ("rodata", 0x1180000, 0xc00000)),
        ("target.data", b"DATA", ("data", 0x2980000, 0x2000000)),
    ]
    header = bytearray(2048)
    for offset, value in [(0, b"AIC.FW"), (8, b"d21x"),
                          (72, b"demo128_nand_carplay_page_2k_block_128k"),
                          (136, b"1.0.1"), (200, b"spi-nand"), (268, b"P=2K,B=128K")]:
        header[offset:offset + len(value)] = value
    padded = lambda size: (size + 2047) & ~2047
    data_offset = len(header) + len(parts) * 512
    file_size = sum(padded(len(data)) for _, data, _ in parts if data)
    struct.pack_into("<4I", header, 332, len(header), len(parts) * 512, data_offset, file_size)
    metadata, payload = bytearray(), bytearray()
    for name, data, target in parts:
        entry = bytearray(512)
        entry[:4] = b"META"
        identity = f"image.{name}".encode()
        entry[8:8 + len(identity)] = identity
        if target:
            partition, address, capacity = target
            entry[72:72 + len(partition)] = partition.encode()
            entry[112:120] = b"spi-nand"
            struct.pack_into("<2I", entry, 104, address, capacity)
        offset = data_offset + len(payload) if data else 0
        component = data if data else header
        struct.pack_into("<3I", entry, 136, offset, len(component), zlib.crc32(component))
        metadata.extend(entry)
        if data:
            payload.extend(data.ljust(padded(len(data)), b"\0"))
    return header + metadata + payload


with tempfile.TemporaryDirectory(prefix="carplay-ota-") as directory:
    path = Path(directory)
    for name, text in headers.items():
        header = path / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(text)
    (path / "test.c").write_text(source)
    fixture = path / "firmware.img"
    fixture.write_bytes(image_fixture())
    includes = (path, app / "app", app / "platform", sdk / "packages/third-party/rt-carplay/include",
                sdk / "bsp/common/include", sdk / "packages/artinchip/env", sdk / "packages/artinchip/ota")
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fsanitize=address,undefined",
                    "-fno-pie", "-no-pie", "-g", *["-I" + str(include) for include in includes],
                    str(path / "test.c"), str(sdk / "bsp/common/crc32/crc32.c"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test"), str(fixture)], cwd=path, check=True)
    if len(sys.argv) == 2:
        (path / "source.cfg").unlink()
        subprocess.run([str(path / "test"), str(Path(sys.argv[1]).resolve())], cwd=path, check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the CPIO update worker and SDK downloader with modeled HTTP, NAND and RTOS."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import zlib

app = Path(__file__).resolve().parents[1]
sdk = Path(os.environ.get("SDK_ROOT", app.parents[2])).resolve()
headers = {
    "rtthread.h": """
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define rt_memcpy memcpy
#define rt_strncmp strncmp
typedef uint8_t u8;
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
    "boot_param.h": """
#pragma once
enum boot_device { BD_SPINAND, BD_SDMC0, BD_SDMC1 };
enum boot_device aic_get_boot_device(void);
""",
    "rtconfig.h": """
#include <boot_param.h>
#define CACHE_LINE_SIZE 64
#define aicos_malloc_align(heap, size, align) aligned_alloc(align, size)
#define aicos_free_align(heap, ptr) free(ptr)
""",
    "rtdbg.h": "#define LOG_I(...) ((void)0)\n#define LOG_E(...) ((void)0)\n",
    "firmware_security.h": "",
    "finsh.h": "",
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
char *fw_getenv(const char *);
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
#define RTCARPLAY_OTA_STAGE "stage.cpio"
#define RTCARPLAY_OTA_SOURCE_FILE "source.cfg"
#include "ota_lubanlite.c"

struct test_thread { void (*entry)(void *); void *arg; };
static rt_thread_t pending;
static unsigned char *body;
static size_t body_size;
static struct rt_mtd_nand_device nand = {{RT_Device_Class_MTD}, 2048, 64, 96};
static unsigned char flash[4096];
static struct update_archive fixture_archive;
static const struct cpio_member *flash_member;
static unsigned next_write;
static unsigned char masks, upgraded, erases, commits, resets;
static bool active, maintenance, route = true, cancel_read, readback_failure, commit_failure;
static bool available, unknown_length, lose_capacity;
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
char *fw_getenv(const char *name)
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

enum boot_device aic_get_boot_device(void) { return BD_SPINAND; }
int rt_mtd_nand_check_block(struct rt_mtd_nand_device *device, unsigned block)
{
    assert(device == &nand && block < nand.block_total);
    return lose_capacity && erases ? -RT_ERROR : RT_EOK;
}
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
    bool os = !strncmp(name, "os", 2);
    masks |= os ? 1 : 2;
    flash_member = os ? &fixture_archive.os : &fixture_archive.rodata;
    next_write = 0;
    return 0;
}
int aic_ota_erase_part(void) { ++erases; return 0; }
int aic_ota_part_write(uint32_t addr, const uint8_t *buffer, size_t size)
{
    assert(addr == next_write && size && !(size % OTA_PAGE) && size <= sizeof(flash));
    assert(addr + size <= flash_member->size && !memcmp(buffer, body + flash_member->payload + addr, size));
    next_write += size;
    memcpy(flash, buffer, size);
    return 0;
}
int aic_ota_part_read(uint32_t addr, uint8_t *buffer, size_t size)
{
    memcpy(buffer, flash, size);
    if (readback_failure && addr == 0) buffer[0] ^= 1;
    return 0;
}
void aic_reset_upgrade_status(void) { upgraded = 0; }
void aic_set_upgrade_status(char *name)
{
    assert(!strcmp(name, "d21x_os.itb") || !strcmp(name, "rodata.fatfs"));
    assert(next_write == flash_member->size);
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
    unsigned char scratch[OTA_CHUNK];
    assert(verify_archive(fixture, scratch, &fixture_archive));
    fclose(fixture);
    assert(!ota_service_init() && !status.source[0]);
    assert(carplay_ota_download() == -RT_EINVAL);
    assert(carplay_ota_source_set("http://example.test/version.txt") == -RT_EINVAL);
    assert(carplay_ota_source_set("http://example.test/fw.img") == -RT_EINVAL);
    assert(carplay_ota_source_set("http://example.test/ota.cpio#fragment") == -RT_EINVAL);
    assert(archive_url("https://example.test/ota.cpio?download=1"));
    assert(!allowed_redirect("https://github.com/any/ota.cpio", "http://github.com/ota.cpio"));
    assert(allowed_redirect("https://github.com/any/ota.cpio", "https://release-assets.githubusercontent.com/ota.cpio"));
    assert(!carplay_ota_source_set("http://192.168.1.20:8000/ota.cpio"));
    run();
    assert(!ota_service_init() && !strcmp(status.source, "http://192.168.1.20:8000/ota.cpio"));

    cancel_read = true;
    download_and_expect(CARPLAY_OTA_IDLE);
    cancel_read = false;
    --body_size;
    download_and_expect(CARPLAY_OTA_FAILED);
    ++body_size;
    unsigned char *metadata = body + fixture_archive.info.payload;
    const char board[] = "board = \"demo128-nand-carplay\";";
    while (memcmp(metadata, board, sizeof(board)))
        ++metadata;
    *metadata ^= 1;
    download_and_expect(CARPLAY_OTA_FAILED);
    *metadata ^= 1;
    unsigned char *os = body + fixture_archive.os.header;
    os[94] = 'f';
    download_and_expect(CARPLAY_OTA_FAILED);
    os[94] = '0';
    unsigned char *payload = body + fixture_archive.os.payload;
    *payload ^= 1;
    download_and_expect(CARPLAY_OTA_FAILED);
    *payload ^= 1;
    unsigned char *rodata = body + fixture_archive.rodata.header;
    rodata[CPIO_HEADER_SIZE] ^= 1;
    download_and_expect(CARPLAY_OTA_FAILED);
    rodata[CPIO_HEADER_SIZE] ^= 1;
    unsigned char *trailer = body + fixture_archive.trailer.header;
    trailer[CPIO_HEADER_SIZE] ^= 1;
    download_and_expect(CARPLAY_OTA_FAILED);
    trailer[CPIO_HEADER_SIZE] ^= 1;
    unknown_length = true;
    download_and_expect(CARPLAY_OTA_READY_TO_INSTALL);
    assert(carplay_ota_install(status.candidate - 1) == -RT_EINVAL);
    FILE *stage = fopen(RTCARPLAY_OTA_STAGE, "rb+");
    assert(stage && !fseek(stage, (long)(payload - body), SEEK_SET));
    unsigned char swapped[2] = { payload[1], payload[0] };
    assert(swapped[0] != swapped[1]);
    assert(fwrite(swapped, 1, sizeof(swapped), stage) == sizeof(swapped) && !fclose(stage));
    assert(!carplay_ota_install(status.candidate));
    run();
    assert(status.state == CARPLAY_OTA_FAILED && !erases);

    download_and_expect(CARPLAY_OTA_READY_TO_INSTALL);
    lose_capacity = true;
    assert(!carplay_ota_install(status.candidate));
    run();
    assert(status.state == CARPLAY_OTA_FAILED && erases == 1 && !next_write && !commits && !resets);
    lose_capacity = false;
    erases = masks = 0;

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
    puts("CPIO preflight, staged-package recheck, SDK inactive NAND writes and A/B commit passed.");
}
'''


def cpio_entry(name, data, mode=0o100644):
    name += b"\0"
    fields = (0, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), sum(data))
    header = b"070702" + "".join(f"{value:08x}" for value in fields).encode() + name
    return header + b"\0" * (-len(header) % 4) + data + b"\0" * (-len(data) % 4)


def cpio_fixture():
    info = (b'[image]\0size = "";\0version = "1.0.1";\0board = "demo128-nand-carplay";\0'
            b'[file]\0ota_info.bin:file;\0d21x_os.itb:os;\0rodata.fatfs:rodata;\0\0')
    info = info.ljust(508, b"\xff")
    metadata = cpio_entry(b"ota_info.bin", struct.pack("<I", zlib.crc32(info)) + info)
    archive = metadata + cpio_entry(b"d21x_os.itb", b"OS" * 3072)
    archive += cpio_entry(b"rodata.fatfs", b"RODATA!!" * 768)
    archive += cpio_entry(b"TRAILER!!!", b"", mode=0)
    archive += b"\0" * (-len(archive) % 512)
    size = str(len(archive)).encode()
    return archive.replace(b'size = ""', b'size = "' + size + b'"', 1).replace(
        b"\xff" * (16 + len(size)), b"\xff" * 16, 1)


with tempfile.TemporaryDirectory(prefix="carplay-ota-") as directory:
    path = Path(directory)
    for name, text in headers.items():
        header = path / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(text)
    (path / "test.c").write_text(source)
    fixture = path / "ota.cpio"
    fixture.write_bytes(cpio_fixture())
    includes = (path, app / "app", app / "platform", sdk / "packages/third-party/rt-carplay/include",
                sdk / "bsp/common/include", sdk / "packages/artinchip/env", sdk / "packages/artinchip/ota")
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
                    "-Wno-pointer-sign", "-fsanitize=address,undefined",
                    "-fno-pie", "-no-pie", "-g", *["-I" + str(include) for include in includes],
                    str(path / "test.c"), str(sdk / "bsp/common/crc32/crc32.c"),
                    str(sdk / "packages/artinchip/ota/ota.c"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test"), str(fixture)], cwd=path, check=True)
    if len(sys.argv) == 2:
        (path / "source.cfg").unlink()
        subprocess.run([str(path / "test"), str(Path(sys.argv[1]).resolve())], cwd=path, check=True)

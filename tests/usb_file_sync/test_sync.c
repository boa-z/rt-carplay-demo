/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the actual upstream sync service; the VFS models a mounted volume.
 * USB enumeration, DMA, real DFS and host adb compatibility need board tests. */
#include "usbd_core.h"
#include "usbd_adb.h"
#include <assert.h>

static const struct adb_service *service;
static uint8_t file_data[65536], replies[70000];
static size_t file_size, offset, reply_size;
static unsigned open_count;
static bool exists, read_only;

int usbd_adb_service_register(const struct adb_service *svc)
{
    assert(!service && svc->localid == ADB_LOCALID_SYNC);
    service = svc;
    return 0;
}
int usbd_adb_write(uint32_t id, const uint8_t *data, uint32_t length)
{
    assert(id == ADB_LOCALID_SYNC && reply_size + length <= sizeof(replies));
    memcpy(replies + reply_size, data, length);
    reply_size += length;
    return (int)length;
}
const char *vfs_get_fs_root_path(void)
{
    return "/";
}
const char *vfs_get_fs_description(void)
{
    return "test volume";
}
int vfs_stat(const char *path, struct vfs_stat *st)
{
    memset(st, 0, sizeof(*st));
    if (!strcmp(path, "/data"))
    {
        st->st_mode = VFS_S_IFDIR;
        return 0;
    }
    if (exists && !strcmp(path, "/data/check.bin"))
    {
        st->st_mode = VFS_S_IFREG;
        st->st_size = file_size;
        return 0;
    }
    return VFS_ERR_NOENT;
}
int vfs_mkdir(const char *path)
{
    (void)path;
    return VFS_ERR_ROFS;
}
int vfs_unlink(const char *path)
{
    (void)path;
    return VFS_ERR_ROFS;
}
VFS_DIR *vfs_opendir(const char *path)
{
    (void)path;
    return NULL;
}
struct vfs_dirent *vfs_readdir(VFS_DIR *dir)
{
    (void)dir;
    return NULL;
}
int vfs_closedir(VFS_DIR *dir)
{
    (void)dir;
    return 0;
}
int vfs_open(const char *path, uint8_t mode)
{
    if (strcmp(path, "/data/check.bin"))
        return VFS_ERR_NOENT;
    if (mode == VFS_O_RDONLY && !exists)
        return VFS_ERR_NOENT;
    if (mode != VFS_O_RDONLY && read_only)
        return VFS_ERR_ROFS;
    assert(open_count == 0);
    if (mode != VFS_O_RDONLY)
    {
        exists = true;
        file_size = 0;
    }
    offset = 0;
    open_count++;
    return 3;
}
int vfs_close(int fd)
{
    assert(fd == 3 && open_count == 1);
    open_count--;
    return 0;
}
int vfs_write(int fd, const void *data, size_t size)
{
    assert(fd == 3 && open_count == 1 && file_size + size <= sizeof(file_data));
    memcpy(file_data + file_size, data, size);
    file_size += size;
    return (int)size;
}
int vfs_read(int fd, void *data, size_t size)
{
    assert(fd == 3 && open_count == 1);
    if (size > file_size - offset)
        size = file_size - offset;
    memcpy(data, file_data + offset, size);
    offset += size;
    return (int)size;
}

/* Deliberately split every header and payload across arbitrary USB packets. */
static void feed(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    while (size)
    {
        size_t part = size > 37 ? 37 : size;
        service->on_write(7, bytes, (uint32_t)part);
        bytes += part;
        size -= part;
    }
}
static void request(const char id[4], const void *data, uint32_t size)
{
    uint8_t header[8];
    memcpy(header, id, 4);
    memcpy(header + 4, &size, 4);
    feed(header, 8);
    if (data)
        feed(data, size);
}
static void send_file(const uint8_t *data, uint32_t size)
{
    const char target[] = "/data/check.bin,33188";
    request("SEND", target, sizeof(target) - 1);
    request("DATA", data, size);
    request("DONE", NULL, 0);
}
int main(void)
{
    uint8_t original[65536];
    for (size_t i = 0; i < sizeof(original); i++) original[i] = (uint8_t)(i * 31);
    usbd_adb_sync_init();
    assert(service);
    service->on_open(7);
    send_file(original, sizeof(original));
    assert(reply_size == 8 && !memcmp(replies, "OKAY", 4));
    assert(file_size == sizeof(original) && !memcmp(file_data, original, file_size));
    assert(open_count == 0);

    reply_size = 0;
    request("RECV", "/data/check.bin", 15);
    size_t cursor = 0, received = 0;
    while (cursor < reply_size && !memcmp(replies + cursor, "DATA", 4))
    {
        uint32_t length;
        memcpy(&length, replies + cursor + 4, 4);
        assert(received + length <= sizeof(original));
        assert(cursor + 8 + length <= reply_size);
        assert(!memcmp(replies + cursor + 8, original + received, length));
        received += length;
        cursor += 8 + length;
    }
    assert(received == sizeof(original) && cursor + 8 == reply_size);
    assert(!memcmp(replies + cursor, "DONE", 4) && open_count == 0);

    reply_size = 0;
    request("RECV", "/missing", 8);
    assert(reply_size >= 8 && !memcmp(replies, "FAIL", 4));
    reply_size = 0;
    read_only = true;
    send_file(original, 10);
    assert(reply_size >= 8 && !memcmp(replies, "FAIL", 4) && open_count == 0);
    read_only = false;
    reply_size = 0;

    request("SEND", "/data/check.bin,33188", 21);
    request("DATA", original, 100);
    assert(open_count == 1);
    service->on_close(7);
    assert(open_count == 0);
    service->on_open(7);
    send_file(original, 1024);
    assert(file_size == 1024 && open_count == 0);
    service->on_close(7);
    puts("CherryUSB sync: fragmented 64 KiB push/pull, missing/read-only errors, interrupted service and reopen PASS");
    return 0;
}

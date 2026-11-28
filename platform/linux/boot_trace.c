#define _GNU_SOURCE
#include "linux_demo.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

void demo_boot_mark(const char *stage)
{
    const char *directory = getenv("CARPLAY_BOOT_TRACE");
    if (!directory || !*directory)
        return;
    struct timespec now;
    char path[256];
    if (clock_gettime(CLOCK_BOOTTIME, &now) != 0 ||
        snprintf(path, sizeof(path), "%s/%s", directory, stage) >= (int)sizeof(path))
        return;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return;
    char stamp[32];
    int length = snprintf(stamp, sizeof(stamp), "%lld.%03ld\n", (long long)now.tv_sec,
                          now.tv_nsec / 1000000);
    if (write(fd, stamp, (size_t)length) != length)
        unlink(path);
    close(fd);
}

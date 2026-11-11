// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_sysfs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool y2_sysfs_read_line(const char *path, char *out, size_t cap) {
    if (cap == 0) {
        return false;
    }
    out[0] = '\0';
    int fd;
    do {
        fd = open(path, O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return false;
    }
    size_t got = 0;
    bool ok = true;
    while (got < cap - 1) {
        ssize_t r = read(fd, out + got, cap - 1 - got);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (r == 0) {
            break;
        }
        got += (size_t)r;
        if (memchr(out, '\n', got) != NULL) {
            break; // a whole line is enough
        }
    }
    close(fd);
    out[got] = '\0';
    if (!ok || got == 0) {
        out[0] = '\0';
        return false;
    }
    out[strcspn(out, "\r\n")] = '\0';
    return true;
}

int y2_boot_mode_read(const char *path) {
    char v[16];
    if (!y2_sysfs_read_line(path, v, sizeof v)) {
        return -1;
    }
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v || n < 0 || n > 1000) {
        return -1;
    }
    return (int)n;
}

bool y2_boot_mode_is_charging(int mode) {
    return mode == Y2_BOOT_MODE_KERNEL_POWER_OFF_CHARGING || mode == Y2_BOOT_MODE_LOW_POWER_OFF_CHARGING;
}

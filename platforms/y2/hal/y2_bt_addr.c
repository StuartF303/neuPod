// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_bt_addr.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

y2_bt_addr_result_t y2_bt_addr_read(const char *path, uint8_t out[6]) {
    int fd;
    do {
        fd = open(path, O_RDONLY | O_CLOEXEC); // never written; the RTOS tick signal can interrupt it
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return Y2_BT_ADDR_UNREADABLE;
    }
    uint8_t buf[6];
    size_t got = 0;
    while (got < sizeof buf) {
        ssize_t n = read(fd, buf + got, sizeof buf - got);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            close(fd);
            return Y2_BT_ADDR_UNREADABLE;
        }
        if (n == 0) {
            break;
        }
        got += (size_t)n;
    }
    close(fd);
    if (got < sizeof buf) {
        return Y2_BT_ADDR_SHORT;
    }
    static const uint8_t zero[6];
    static const uint8_t ones[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (memcmp(buf, zero, 6) == 0 || memcmp(buf, ones, 6) == 0) {
        return Y2_BT_ADDR_INVALID;
    }
    memcpy(out, buf, 6);
    return Y2_BT_ADDR_OK;
}

const char *y2_bt_addr_result_text(y2_bt_addr_result_t r) {
    switch (r) {
    case Y2_BT_ADDR_OK: return "ok";
    case Y2_BT_ADDR_UNREADABLE: return "cannot be read";
    case Y2_BT_ADDR_SHORT: return "shorter than 6 bytes";
    case Y2_BT_ADDR_INVALID: return "all zero or all 0xFF";
    }
    return "?";
}

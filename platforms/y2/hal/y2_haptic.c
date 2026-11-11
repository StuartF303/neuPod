// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_haptic.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void y2_haptic_table_default(y2_haptic_table_t *t) {
    t->ms[Y2_HAPTIC_TICK] = Y2_HAPTIC_DEFAULT_TICK_MS;
    t->ms[Y2_HAPTIC_CLICK] = Y2_HAPTIC_DEFAULT_CLICK_MS;
    t->ms[Y2_HAPTIC_STRONG] = Y2_HAPTIC_DEFAULT_STRONG_MS;
}

static bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Decimal 1..Y2_HAPTIC_MAX_MS in [b, e), surrounding blanks allowed.
static bool parse_ms(const char *b, const char *e, uint16_t *out) {
    while (b < e && is_blank(*b)) {
        b++;
    }
    while (e > b && is_blank(e[-1])) {
        e--;
    }
    if (b == e) {
        return false;
    }
    unsigned v = 0;
    for (; b < e; b++) {
        if (*b < '0' || *b > '9') {
            return false;
        }
        v = v * 10u + (unsigned)(*b - '0');
        if (v > Y2_HAPTIC_MAX_MS) {
            return false;
        }
    }
    if (v == 0) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

int y2_haptic_table_parse(y2_haptic_table_t *t, const char *s) {
    if (s == NULL) {
        return 0;
    }
    int bad = 0;
    unsigned idx = 0;
    const char *p = s;
    for (;;) {
        const char *comma = strchr(p, ',');
        const char *end = comma != NULL ? comma : p + strlen(p);
        bool blank = true;
        for (const char *q = p; q < end; q++) {
            if (!is_blank(*q)) {
                blank = false;
            }
        }
        if (idx >= Y2_HAPTIC_KINDS) {
            bad++; // a fourth field: nothing to put it in
        } else if (!blank) {
            uint16_t v;
            if (parse_ms(p, end, &v)) {
                t->ms[idx] = v;
            } else {
                bad++;
            }
        }
        idx++;
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    return bad;
}

y2_haptic_kind_t y2_haptic_kind_for_effect(uint8_t id) {
    switch (id) {
    case 3: case 6: case 9: case 12: // 30 % clicks / bumps
    case 24: case 25: case 26:       // sharp ticks
        return Y2_HAPTIC_TICK;
    case 1: case 4: case 10: case 14: // 100 % clicks, double click, strong buzz
        return Y2_HAPTIC_STRONG;
    default:
        return Y2_HAPTIC_CLICK;
    }
}

bool y2_haptic_gate_pass(y2_haptic_gate_t *g, uint32_t now_ms, uint16_t ms) {
    if (g->have && (int32_t)(now_ms - g->free_at_ms) < 0) {
        return false;
    }
    g->have = true;
    g->free_at_ms = now_ms + ms + Y2_HAPTIC_REST_MS;
    return true;
}

static int open_wr(const char *path) {
    int fd;
    do {
        fd = open(path, O_WRONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR); // the POSIX port's 1 kHz tick interrupts blocking calls
    return fd;
}

bool y2_haptic_node_ok(const char *path) {
    const int fd = open_wr(path);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

bool y2_haptic_write(const char *path, unsigned ms) {
    const int fd = open_wr(path);
    if (fd < 0) {
        return false;
    }
    char buf[16];
    const int n = snprintf(buf, sizeof buf, "%u\n", ms);
    ssize_t w;
    do {
        w = write(fd, buf, (size_t)n);
    } while (w < 0 && errno == EINTR);
    close(fd);
    return w == n;
}

// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_suspend.h"

#include "y2_input.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool y2_susp_env_enabled(const char *value) { return value == NULL || strcmp(value, "0") != 0; }

bool y2_susp_state_has_mem(const char *text) {
    if (text == NULL) {
        return false;
    }
    for (const char *p = text; *p != '\0';) {
        while (*p == ' ' || *p == '\n' || *p == '\t') {
            p++;
        }
        const char *w = p;
        while (*p != '\0' && *p != ' ' && *p != '\n' && *p != '\t') {
            p++;
        }
        if (p - w == 3 && strncmp(w, "mem", 3) == 0) {
            return true;
        }
    }
    return false;
}

bool y2_susp_write(const char *dir, const char *file, const char *text) {
    char path[160];
    if (snprintf(path, sizeof path, "%s/%s", dir, file) >= (int)sizeof path) {
        return false;
    }
    int fd;
    do {
        fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return false;
    }
    const size_t n = strlen(text);
    ssize_t w;
    do {
        w = write(fd, text, n);
    } while (w < 0 && errno == EINTR);
    const bool ok = w == (ssize_t)n;
    close(fd);
    return ok;
}

bool y2_susp_has_press(const uint8_t *buf, size_t len) {
    y2_event_t ev[32];
    size_t used = 0;
    while (len >= Y2_EVENT_SIZE) {
        const size_t n = y2_evdev_decode(buf, len, ev, sizeof ev / sizeof ev[0], &used);
        for (size_t i = 0; i < n; i++) {
            if (ev[i].type == 1u && ev[i].value == 1) { /* EV_KEY, pressed */
                return true;
            }
        }
        if (used == 0) {
            break;
        }
        buf += used;
        len -= used;
    }
    return false;
}

y2_alarm_ts_t y2_alarm_after(y2_alarm_ts_t now, uint32_t add_ms) {
    int64_t sec = (int64_t)now.sec + add_ms / 1000u;
    int64_t nsec = (int64_t)now.nsec + (int64_t)(add_ms % 1000u) * 1000000;
    if (nsec >= 1000000000) {
        nsec -= 1000000000;
        sec++;
    }
    return (y2_alarm_ts_t){.sec = (int32_t)sec, .nsec = (int32_t)nsec};
}

void y2_susp_begin(y2_susp_t *s, uint32_t max_ms, int64_t mono_ms, int64_t boot_ms, int64_t rtc_ms, bool ext_online) {
    s->max_ms = max_ms;
    s->mono0_ms = mono_ms;
    s->boot0_ms = boot_ms;
    s->ext0 = ext_online;
    s->deadline_rtc_ms = rtc_ms >= 0 ? rtc_ms + (int64_t)max_ms : -1;
}

y2_susp_verdict_t y2_susp_step(const y2_susp_t *s, int64_t mono_ms, int64_t boot_ms, int64_t rtc_ms, bool input_ready, bool wheel_activity, bool ext_online) {
    const int64_t mono = mono_ms - s->mono0_ms;
    const int64_t boot = boot_ms - s->boot0_ms;
    const int64_t asleep = boot - mono; /* the boot clock ran on while the monotonic one stood still */
    y2_susp_verdict_t v = {.state = Y2_SUSP_WAIT, .why = HAL_WAKE_NONE, .slept = asleep >= (int64_t)Y2_SUSP_GAP_MS, .slept_ms = asleep > 0 ? (uint32_t)asleep : 0u};

    const bool rtc_due = rtc_ms >= 0 && s->deadline_rtc_ms >= 0 && rtc_ms >= s->deadline_rtc_ms - (int64_t)Y2_SUSP_TIMER_SLACK_MS;
    if ((boot >= (int64_t)s->max_ms - (int64_t)Y2_SUSP_TIMER_SLACK_MS && v.slept) || rtc_due) {
        v.state = Y2_SUSP_WOKE;
        v.why = HAL_WAKE_TIMER; /* the alarm: by the boot clock, or (if that did not run on) by the real-time clock reaching its time */
        v.slept = v.slept || rtc_due;
    } else if (input_ready || (wheel_activity && v.slept)) {
        v.state = Y2_SUSP_WOKE; /* a key, the wheel or the panel: also while it was still going down (the key is real) */
        v.why = HAL_WAKE_KEY;
    } else if (ext_online && !s->ext0) {
        v.state = Y2_SUSP_WOKE;
        v.why = HAL_WAKE_EXTERNAL_POWER;
    } else if (v.slept) {
        v.state = Y2_SUSP_WOKE; /* it slept and nothing says why: a wake source we do not watch */
        v.why = HAL_WAKE_OTHER;
    } else if (mono >= (int64_t)Y2_SUSP_REFUSE_MS) {
        v.state = Y2_SUSP_REFUSED;
    }
    return v;
}

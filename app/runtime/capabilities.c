// SPDX-License-Identifier: MPL-2.0
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "capabilities.h"
#include "os.h"

static capabilities_t g_caps;
static os_mutex_t *g_lock;
static bool g_timed_out[CAP_COUNT]; // DEGRADED only because the deadline passed: may still turn OK
static bool g_rearmed[CAP_COUNT];   // DEGRADED, and a check that is run again on purpose may overwrite it (OK or a new reason)

static const char *const k_names[CAP_COUNT] = {"display", "storage",   "battery", "haptics",
                                               "wheel",   "audio",     "bluetooth", "wifi"};

const char *cap_name(cap_id_t id) { return id < CAP_COUNT ? k_names[id] : "?"; }

void caps_init(void) {
    memset(&g_caps, 0, sizeof g_caps);
    memset(g_timed_out, 0, sizeof g_timed_out);
    memset(g_rearmed, 0, sizeof g_rearmed);
    if (g_lock == NULL) {
        g_lock = os_mutex_create();
    }
}

static bool resolve_v(cap_id_t id, bool ok, bool timeout, const char *brief, const char *fmt, va_list ap) {
    if (id >= CAP_COUNT) {
        return false;
    }
    char text[CAP_TEXT_LEN];
    vsnprintf(text, sizeof text, fmt, ap);

    bool changed = false, late = false;
    if (os_mutex_lock(g_lock, OS_WAIT_FOREVER)) {
        bool pending = g_caps.state[id] == CAP_PENDING;
        bool degraded = g_caps.state[id] == CAP_DEGRADED;
        late = !pending && degraded && ((ok && g_timed_out[id]) || g_rearmed[id]);
        if (pending || late) {
            g_rearmed[id] = false;
            g_caps.state[id] = ok ? CAP_OK : CAP_DEGRADED;
            memcpy(g_caps.text[id], text, sizeof text);
            snprintf(g_caps.brief[id], sizeof g_caps.brief[id], "%s", brief != NULL ? brief : "");
            g_timed_out[id] = timeout && !ok;
            changed = true;
        }
        os_mutex_unlock(g_lock);
    }
    if (changed) {
        printf("boot: %-9s %s: %s%s\n", cap_name(id), ok ? "ok      " : "DEGRADED", late && ok ? "(late result after timeout) " : "", text);
    }
    return changed;
}

bool caps_resolve(cap_id_t id, bool ok, const char *brief, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    bool r = resolve_v(id, ok, false, brief, fmt, ap);
    va_end(ap);
    return r;
}

bool caps_resolve_timeout(cap_id_t id, const char *brief, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    bool r = resolve_v(id, false, true, brief, fmt, ap);
    va_end(ap);
    return r;
}

void caps_allow_late_ok(cap_id_t id) {
    if (id < CAP_COUNT && os_mutex_lock(g_lock, OS_WAIT_FOREVER)) {
        if (g_caps.state[id] == CAP_DEGRADED) {
            g_rearmed[id] = true;
        }
        os_mutex_unlock(g_lock);
    }
}

void caps_force_degraded(cap_id_t id, const char *brief, const char *fmt, ...) {
    if (id >= CAP_COUNT) {
        return;
    }
    char text[CAP_TEXT_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    bool changed = false;
    if (os_mutex_lock(g_lock, OS_WAIT_FOREVER)) {
        if (g_caps.state[id] == CAP_OK) {
            g_caps.state[id] = CAP_DEGRADED;
            memcpy(g_caps.text[id], text, sizeof text);
            snprintf(g_caps.brief[id], sizeof g_caps.brief[id], "%s", brief != NULL ? brief : "");
            g_timed_out[id] = false;
            g_rearmed[id] = true;
            changed = true;
        }
        os_mutex_unlock(g_lock);
    }
    if (changed) {
        printf("boot: %-9s DEGRADED: %s\n", cap_name(id), text);
    }
}

void caps_get(capabilities_t *out) {
    if (os_mutex_lock(g_lock, OS_WAIT_FOREVER)) {
        *out = g_caps;
        os_mutex_unlock(g_lock);
    } else {
        memset(out, 0, sizeof *out);
    }
}

bool caps_available(cap_id_t id) {
    capabilities_t c;
    caps_get(&c);
    return id < CAP_COUNT && c.state[id] == CAP_OK;
}

bool caps_all_resolved(void) {
    capabilities_t c;
    caps_get(&c);
    for (int i = 0; i < CAP_COUNT; i++) {
        if (c.state[i] == CAP_PENDING) {
            return false;
        }
    }
    return true;
}

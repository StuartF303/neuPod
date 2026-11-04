// SPDX-License-Identifier: MPL-2.0
// Resume record in the settings store. See resume.h. Pure logic over settings_get / settings_put: host-tested.
#include "resume.h"

#include <string.h>

#include "play_queue.h"
#include "settings_store.h"

#define HDR_BYTES 13u
#define VER 1u
#define FIRST_PART (SETTINGS_VALUE_MAX - HDR_BYTES) /* path bytes that fit the header item */

static uint32_t g_last_ms;
static bool g_have_last;

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

void resume_clear(void) {
    settings_delete(SETTING_RESUME0);
    settings_delete(SETTING_RESUME1);
}

bool resume_periodic_due(uint32_t now_ms) { return !g_have_last || now_ms - g_last_ms >= RESUME_PERIOD_MS; }

bool resume_save(const resume_state_t *s, resume_why_t why, uint32_t now_ms) {
    size_t n = strnlen(s->path, RESUME_PATH_MAX);
    if (n == 0 || n > RESUME_PATH_MAX_STORED || s->path[0] != '/') {
        resume_clear();
        return false;
    }
    if (why == RESUME_PERIODIC && !resume_periodic_due(now_ms)) {
        return false;
    }
    uint8_t h[SETTINGS_VALUE_MAX];
    h[0] = VER;
    h[1] = s->playing ? 1u : 0u;
    h[2] = (uint8_t)s->index;
    h[3] = (uint8_t)(s->index >> 8);
    put32(h + 4, s->frame);
    put32(h + 8, s->size);
    h[12] = (uint8_t)n;
    uint32_t first = n < FIRST_PART ? (uint32_t)n : FIRST_PART;
    memcpy(h + HDR_BYTES, s->path, first);
    bool ok = settings_put(SETTING_RESUME0, h, HDR_BYTES + first);
    if (n > FIRST_PART) {
        ok = settings_put(SETTING_RESUME1, s->path + FIRST_PART, (uint32_t)(n - FIRST_PART)) && ok;
    } else {
        settings_delete(SETTING_RESUME1);
    }
    if (!ok) {
        resume_clear(); // store full: better no record than half of one
        return false;
    }
    g_last_ms = now_ms;
    g_have_last = true;
    return true;
}

bool resume_load(resume_state_t *out) {
    uint8_t h[SETTINGS_VALUE_MAX];
    uint32_t len = settings_get(SETTING_RESUME0, h, sizeof h);
    if (len <= HDR_BYTES || len > sizeof h || h[0] != VER) {
        return false;
    }
    uint32_t n = h[12];
    uint32_t first = len - HDR_BYTES;
    if (n == 0 || n > RESUME_PATH_MAX_STORED || first != (n < FIRST_PART ? n : FIRST_PART)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    memcpy(out->path, h + HDR_BYTES, first);
    if (n > FIRST_PART) {
        uint32_t l2 = settings_get(SETTING_RESUME1, out->path + FIRST_PART, sizeof out->path - FIRST_PART - 1u);
        if (l2 != n - FIRST_PART) {
            return false;
        }
    }
    out->path[n] = '\0';
    if (out->path[0] != '/' || strlen(out->path) != n) {
        return false;
    }
    out->playing = (h[1] & 1u) != 0;
    out->index = (uint16_t)(h[2] | ((uint32_t)h[3] << 8));
    out->frame = get32(h + 4);
    out->size = get32(h + 8);
    return true;
}

bool resume_split(const char *path, char *dir, size_t dir_n, const char **name) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash[1] == '\0') {
        return false;
    }
    size_t dl = (size_t)(slash - path);
    if (dl == 0) {
        dl = 1; // the root folder keeps its slash
    }
    if (dl + 1u > dir_n) {
        return false;
    }
    memcpy(dir, path, dl);
    dir[dl] = '\0';
    *name = slash + 1;
    return true;
}

resume_plan_t resume_plan(const resume_state_t *r, const char *name, const file_entry_t *e, uint32_t count, uint32_t *idx,
                          uint32_t *frame) {
    for (uint32_t i = 0; i < count; i++) {
        if (!e[i].is_dir && strncmp(e[i].name, name, FILES_NAME_LEN - 1u) == 0 && pq_is_audio(e[i].name)) {
            *idx = i;
            if (r->size != 0 && e[i].size != r->size) {
                *frame = 0;
                return RESUME_PLAN_CHANGED;
            }
            *frame = r->frame;
            return RESUME_PLAN_EXACT;
        }
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        n += !e[i].is_dir && pq_is_audio(e[i].name);
    }
    if (n == 0) {
        return RESUME_PLAN_NOTHING;
    }
    uint32_t want = r->index < n ? r->index : n - 1u, seen = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!e[i].is_dir && pq_is_audio(e[i].name) && seen++ == want) {
            *idx = i;
            break;
        }
    }
    *frame = 0;
    return RESUME_PLAN_GONE;
}

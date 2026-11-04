// SPDX-License-Identifier: MPL-2.0
#include "play_queue.h"

#include <stdio.h>
#include <string.h>

static bool ends_with(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    if (n < e) {
        return false;
    }
    for (size_t i = 0; i < e; i++) {
        char c = name[n - e + i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + 'a' - 'A');
        }
        if (c != ext[i]) {
            return false;
        }
    }
    return true;
}

bool pq_is_audio(const char *name) {
    return ends_with(name, ".wav") || ends_with(name, ".flac") || ends_with(name, ".mp3") || ends_with(name, ".m4a") ||
           ends_with(name, ".mp4");
}

void pq_clear(play_queue_t *q) {
    q->dir[0] = '\0';
    q->count = 0;
    q->pos = 0;
}

static bool playable(const file_entry_t *e) { return !e->is_dir && e->name[0] != '\0' && pq_is_audio(e->name); }

bool pq_build(play_queue_t *q, const char *dir, const file_entry_t *entries, uint32_t count, uint32_t start_index) {
    pq_clear(q);
    if (entries == NULL || start_index >= count || !playable(&entries[start_index]) || strlen(dir) >= sizeof q->dir) {
        return false;
    }
    // Choose the window [lo, hi) of entries to keep: from the chosen file forward as far as it fits, then backwards while room remains.
    uint32_t used = 0, tracks = 0, hi = start_index;
    for (uint32_t i = start_index; i < count && tracks < PLAY_QUEUE_MAX_TRACKS; i++) {
        if (!playable(&entries[i])) {
            continue;
        }
        uint32_t need = (uint32_t)strlen(entries[i].name) + 1u;
        if (used + need > PLAY_QUEUE_POOL_BYTES) {
            break;
        }
        used += need;
        tracks++;
        hi = i + 1;
    }
    if (tracks == 0) {
        return false; // a single name that does not fit the pool (cannot happen: names are < 64 bytes)
    }
    uint32_t lo = start_index;
    for (uint32_t i = start_index; i-- > 0;) {
        if (!playable(&entries[i])) {
            continue;
        }
        uint32_t need = (uint32_t)strlen(entries[i].name) + 1u;
        if (used + need > PLAY_QUEUE_POOL_BYTES || tracks >= PLAY_QUEUE_MAX_TRACKS) {
            break;
        }
        used += need;
        tracks++;
        lo = i;
    }
    snprintf(q->dir, sizeof q->dir, "%s", dir);
    uint32_t w = 0, n = 0;
    for (uint32_t i = lo; i < hi; i++) {
        if (!playable(&entries[i])) {
            continue;
        }
        if (i == start_index) {
            q->pos = (uint16_t)n;
        }
        size_t len = strlen(entries[i].name) + 1u;
        memcpy(q->pool + w, entries[i].name, len);
        q->off[n++] = (uint16_t)w;
        w += (uint32_t)len;
    }
    q->count = (uint16_t)n;
    return true;
}

const char *pq_name(const play_queue_t *q, uint32_t i) { return i < q->count ? q->pool + q->off[i] : NULL; }

bool pq_path(const play_queue_t *q, uint32_t i, char *out, size_t n) {
    const char *name = pq_name(q, i);
    if (name == NULL) {
        return false;
    }
    int w = strcmp(q->dir, "/") == 0 ? snprintf(out, n, "/%s", name) : snprintf(out, n, "%s/%s", q->dir, name);
    return w > 0 && (size_t)w < n;
}

bool pq_has_next(const play_queue_t *q) { return q->count != 0 && (uint32_t)q->pos + 1u < q->count; }

pq_move_t pq_next(play_queue_t *q) {
    if (!pq_has_next(q)) {
        return PQ_END;
    }
    q->pos++;
    return PQ_MOVED;
}

pq_move_t pq_prev(play_queue_t *q, uint32_t elapsed_s) {
    if (q->pos == 0 || elapsed_s > PLAY_QUEUE_RESTART_S) {
        return PQ_RESTART;
    }
    q->pos--;
    return PQ_MOVED;
}

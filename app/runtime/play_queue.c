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
    q->wrap_first = PLAY_QUEUE_NO_ENTRY;
}

// xorshift32: enough for a shuffle (not security relevant), 4 bytes of state.
static uint32_t rnd(play_queue_t *q) {
    uint32_t x = q->rng != 0 ? q->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    q->rng = x;
    return x;
}

// Uniform-enough value in [0, n) (multiply-shift; the bias is at most n / 2^32 for n <= 128).
static uint32_t rnd_below(play_queue_t *q, uint32_t n) { return (uint32_t)(((uint64_t)rnd(q) * n) >> 32); }

// Small seeds (a tick count) start xorshift in a weak patch: discard a few outputs.
void pq_seed(play_queue_t *q, uint32_t seed) {
    q->rng = seed != 0 ? seed : 0x9E3779B9u;
    for (int i = 0; i < 3; i++) {
        (void)rnd(q);
    }
}

static void identity(play_queue_t *q) {
    for (uint32_t i = 0; i < q->count; i++) {
        q->order[i] = (uint16_t)i;
    }
}

// New play order: entry @p first at slot 0, all the others after it in a Fisher-Yates permutation. pos goes to 0.
static void shuffle_from(play_queue_t *q, uint16_t first) {
    uint32_t n = q->count, w = 1;
    q->order[0] = first;
    for (uint32_t e = 0; e < n; e++) {
        if (e != first) {
            q->order[w++] = (uint16_t)e;
        }
    }
    for (uint32_t i = n; i-- > 2;) { // slots 1..n-1
        uint32_t j = 1u + rnd_below(q, i); // 1..i
        uint16_t t = q->order[i];
        q->order[i] = q->order[j];
        q->order[j] = t;
    }
    q->pos = 0;
    q->wrap_first = PLAY_QUEUE_NO_ENTRY;
}

// The entry that opens the next round of a shuffled repeat-all: anything but the track that just ended, if there is a choice.
static uint16_t pick_wrap_first(play_queue_t *q) {
    uint16_t last = q->order[q->pos];
    if (q->count < 2) {
        return last;
    }
    uint32_t r = rnd_below(q, (uint32_t)q->count - 1u);
    return (uint16_t)(r >= last ? r + 1u : r);
}

void pq_set_shuffle(play_queue_t *q, pq_shuffle_t mode) {
    bool was = q->shuffle != 0;
    q->shuffle = mode == PQ_SHUFFLE_SONGS ? 1u : 0u;
    q->wrap_first = PLAY_QUEUE_NO_ENTRY;
    if (q->count == 0 || was == (q->shuffle != 0)) {
        return;
    }
    uint16_t cur = q->order[q->pos];
    if (q->shuffle) {
        shuffle_from(q, cur);
    } else {
        identity(q);
        q->pos = cur;
    }
}

void pq_set_repeat(play_queue_t *q, pq_repeat_t mode) { q->repeat = (uint8_t)mode; }

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
    identity(q);
    if (q->shuffle) {
        shuffle_from(q, q->pos); // identity: the slot is the entry
    }
    return true;
}

static const char *entry_name(const play_queue_t *q, uint32_t e) { return e < q->count ? q->pool + q->off[e] : NULL; }
const char *pq_name(const play_queue_t *q, uint32_t i) { return i < q->count ? entry_name(q, q->order[i]) : NULL; }
uint16_t pq_entry(const play_queue_t *q, uint32_t i) { return i < q->count ? q->order[i] : PLAY_QUEUE_NO_ENTRY; }

bool pq_entry_path(const play_queue_t *q, uint16_t e, char *out, size_t n) {
    const char *name = entry_name(q, e);
    if (name == NULL) {
        return false;
    }
    int w = strcmp(q->dir, "/") == 0 ? snprintf(out, n, "/%s", name) : snprintf(out, n, "%s/%s", q->dir, name);
    return w > 0 && (size_t)w < n;
}

bool pq_path(const play_queue_t *q, uint32_t i, char *out, size_t n) { return pq_entry_path(q, pq_entry(q, i), out, n); }

bool pq_has_next(const play_queue_t *q) { return q->count != 0 && (uint32_t)q->pos + 1u < q->count; }

// A new round: shuffled, @p forced (or a pick that is not the track that just ended) opens it; unshuffled it is the folder order again.
static pq_move_t wrap(play_queue_t *q, uint16_t forced) {
    if (q->shuffle) {
        shuffle_from(q, forced != PLAY_QUEUE_NO_ENTRY ? forced : pick_wrap_first(q));
    } else {
        q->pos = 0;
        q->wrap_first = PLAY_QUEUE_NO_ENTRY;
    }
    return PQ_WRAPPED;
}

void pq_snapshot(const play_queue_t *q, pq_snapshot_t *s) {
    memcpy(s->order, q->order, q->count * sizeof q->order[0]);
    s->pos = q->pos;
    s->wrap_first = q->wrap_first;
    s->rng = q->rng;
}

void pq_restore(play_queue_t *q, const pq_snapshot_t *s) {
    memcpy(q->order, s->order, q->count * sizeof q->order[0]);
    q->pos = s->pos;
    q->wrap_first = s->wrap_first;
    q->rng = s->rng;
}

pq_move_t pq_next(play_queue_t *q) {
    if (pq_has_next(q)) {
        q->pos++;
        return PQ_MOVED;
    }
    if (q->count != 0 && q->repeat == PQ_REPEAT_ALL) {
        return wrap(q, q->wrap_first);
    }
    return PQ_END;
}

pq_move_t pq_auto_next(play_queue_t *q) {
    if (q->count != 0 && q->repeat == PQ_REPEAT_ONE) {
        return PQ_RESTART;
    }
    return pq_next(q);
}

pq_move_t pq_prev(play_queue_t *q, uint32_t elapsed_s) {
    if (q->pos == 0 || elapsed_s > PLAY_QUEUE_RESTART_S) {
        return PQ_RESTART;
    }
    q->pos--;
    q->wrap_first = PLAY_QUEUE_NO_ENTRY;
    return PQ_MOVED;
}

bool pq_peek_auto(play_queue_t *q, uint16_t *entry) {
    if (q->count == 0) {
        return false;
    }
    if (q->repeat == PQ_REPEAT_ONE) {
        *entry = q->order[q->pos];
    } else if (pq_has_next(q)) {
        *entry = q->order[q->pos + 1u];
    } else if (q->repeat == PQ_REPEAT_ALL) {
        if (q->shuffle) {
            if (q->wrap_first == PLAY_QUEUE_NO_ENTRY) {
                q->wrap_first = pick_wrap_first(q);
            }
            *entry = q->wrap_first;
        } else {
            *entry = q->order[0];
        }
    } else {
        return false;
    }
    return true;
}

pq_move_t pq_commit(play_queue_t *q, uint16_t e) {
    if (q->count == 0 || e >= q->count) {
        return PQ_END;
    }
    if (q->order[q->pos] == e) {
        return PQ_RESTART;
    }
    if (pq_has_next(q)) {
        if (q->shuffle) {
            uint32_t s = 0;
            while (q->order[s] != e) {
                s++;
            }
            q->order[s] = q->order[q->pos + 1u]; // pull it in as the next one, keep the rest of the permutation
            q->order[q->pos + 1u] = e;
            q->pos++;
        } else {
            q->pos = e;
        }
        q->wrap_first = PLAY_QUEUE_NO_ENTRY;
        return PQ_MOVED;
    }
    if (q->shuffle) {
        return wrap(q, e);
    }
    q->pos = e;
    q->wrap_first = PLAY_QUEUE_NO_ENTRY;
    return PQ_WRAPPED;
}

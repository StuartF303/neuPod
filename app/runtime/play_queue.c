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
    q->lib = 0;
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

// Uniform-enough value in [0, n) (multiply-shift; the bias is at most n / 2^32).
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

static uint32_t lib_entry_of_slot(const play_queue_t *q, uint32_t slot);

// The entry that opens the next round of a shuffled repeat-all: anything but the track that just ended, if there is a choice.
static uint32_t pick_wrap_first(play_queue_t *q) {
    uint32_t last = q->lib ? lib_entry_of_slot(q, q->pos) : q->order[q->pos];
    if (q->count < 2) {
        return last;
    }
    uint32_t r = rnd_below(q, q->count - 1u);
    return r >= last ? r + 1u : r;
}

/* ---- library range queues ----------------------------------------------------------------------------------------- */

// A keyed permutation of [0, n): a balanced 4-round Feistel network over the smallest even number of bits that holds n, cycle-walked (applied
// again while the result is outside [0, n)). The domain is at most 4n, so a walk takes a handful of steps. No state beyond the key.
static uint32_t feistel_round(uint32_t r, uint32_t key, uint32_t round) {
    uint32_t v = r * 0x9E3779B1u + key + round * 0x85EBCA6Bu;
    v ^= v >> 15;
    v *= 0x2C1B3C6Du;
    v ^= v >> 12;
    v *= 0x297A2D39u;
    v ^= v >> 15;
    return v;
}

static uint32_t half_bits(uint32_t n) { // bits per half: 2 * half >= bits needed for n - 1
    uint32_t bits = 1;
    while (bits < 32 && (n - 1u) >> bits != 0) {
        bits++;
    }
    return (bits + 1u) / 2u;
}

static uint32_t feistel_once(uint32_t x, uint32_t h, uint32_t key, bool inverse) {
    uint32_t mask = (1u << h) - 1u; /* h <= 16: half_bits() of a 32-bit n */
    uint32_t l = (x >> h) & mask, r = x & mask;
    if (!inverse) {
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t t = l ^ (feistel_round(r, key, i) & mask);
            l = r;
            r = t;
        }
    } else {
        for (uint32_t i = 4; i-- > 0;) {
            uint32_t t = r ^ (feistel_round(l, key, i) & mask);
            r = l;
            l = t;
        }
    }
    return (l << h) | r;
}

static uint32_t perm(uint32_t x, uint32_t n, uint32_t key, bool inverse) {
    if (n < 2) {
        return 0;
    }
    uint32_t h = half_bits(n);
    do {
        x = feistel_once(x, h, key, inverse);
    } while (x >= n);
    return x;
}

static uint32_t lib_entry_of_slot_as(const play_queue_t *q, uint32_t slot, bool shuffled) {
    if (!shuffled) {
        return slot;
    }
    uint64_t s = (uint64_t)slot + q->lib_rot;
    return perm((uint32_t)(s >= q->count ? s - q->count : s), q->count, q->lib_key, false);
}
static uint32_t lib_entry_of_slot(const play_queue_t *q, uint32_t slot) { return lib_entry_of_slot_as(q, slot, q->shuffle != 0); }

// New permutation with @p first at slot 0 (pos 0).
static void lib_shuffle_from(play_queue_t *q, uint32_t first) {
    q->lib_key = rnd(q);
    q->lib_rot = perm(first, q->count, q->lib_key, true); // perm(rot) = first
    q->pos = 0;
    q->wrap_first = PLAY_QUEUE_NO_ENTRY;
}

bool pq_is_handle(const char *path) { return path != NULL && strncmp(path, "@lib/", 5) == 0; }

bool pq_format_handle(char *out, size_t n, uint8_t order, uint32_t pos) {
    int w = snprintf(out, n, "@lib/%u/%u", (unsigned)order, (unsigned)pos);
    return w > 0 && (size_t)w < n;
}

bool pq_parse_handle(const char *path, uint8_t *order, uint32_t *pos) {
    if (!pq_is_handle(path)) {
        return false;
    }
    const char *p = path + 5;
    uint32_t o = 0, v = 0;
    if (*p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9' && o < 256) {
        o = o * 10u + (uint32_t)(*p++ - '0');
    }
    if (*p++ != '/' || o > 255 || *p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9') {
        uint64_t nv = (uint64_t)v * 10u + (uint32_t)(*p++ - '0');
        if (nv > 0xFFFFFFFFu) {
            return false;
        }
        v = (uint32_t)nv;
    }
    if (*p != '\0') {
        return false;
    }
    *order = (uint8_t)o;
    *pos = v;
    return true;
}

bool pq_build_range(play_queue_t *q, const pq_range_t *range, uint32_t start) {
    pq_clear(q);
    if (range == NULL || range->count == 0 || range->count > PLAY_QUEUE_LIB_MAX || range->first > 0xFFFFFFFFu - range->count ||
        (start != PLAY_QUEUE_NO_ENTRY && start >= range->count)) {
        return false;
    }
    q->lib = 1;
    q->lib_order = range->order;
    q->lib_first = range->first;
    q->count = range->count;
    if (start == PLAY_QUEUE_NO_ENTRY) {
        start = rnd_below(q, range->count);
    }
    q->pos = start;
    if (q->shuffle) {
        lib_shuffle_from(q, start);
    }
    return true;
}

void pq_set_shuffle(play_queue_t *q, pq_shuffle_t mode) {
    bool was = q->shuffle != 0;
    q->shuffle = mode == PQ_SHUFFLE_SONGS ? 1u : 0u;
    q->wrap_first = PLAY_QUEUE_NO_ENTRY;
    if (q->count == 0 || was == (q->shuffle != 0)) {
        return;
    }
    if (q->lib) {
        uint32_t cur = lib_entry_of_slot_as(q, q->pos, was);
        if (q->shuffle) {
            lib_shuffle_from(q, cur);
        } else {
            q->pos = cur;
        }
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

bool pq_build_paths(play_queue_t *q, const char *const *paths, uint32_t count, uint32_t start) {
    pq_clear(q);
    if (paths == NULL || start >= count || paths[start] == NULL || paths[start][0] != '/' || !pq_is_audio(paths[start])) {
        return false;
    }
    // the same window rule as pq_build(): from the chosen entry forward as far as it fits, then backwards while room remains
    uint32_t used = 0, tracks = 0, hi = start, lo = start;
    for (uint32_t i = start; i < count && tracks < PLAY_QUEUE_MAX_TRACKS; i++) {
        if (paths[i] == NULL || paths[i][0] != '/' || !pq_is_audio(paths[i])) {
            continue;
        }
        size_t need = strlen(paths[i]) + 1u;
        if (need > PLAY_QUEUE_PATH_MAX || used + need > PLAY_QUEUE_POOL_BYTES) {
            if (i == start) {
                return false;
            }
            break;
        }
        used += (uint32_t)need;
        tracks++;
        hi = i + 1u;
    }
    for (uint32_t i = start; i-- > 0;) {
        if (paths[i] == NULL || paths[i][0] != '/' || !pq_is_audio(paths[i])) {
            continue;
        }
        size_t need = strlen(paths[i]) + 1u;
        if (need > PLAY_QUEUE_PATH_MAX || used + need > PLAY_QUEUE_POOL_BYTES || tracks >= PLAY_QUEUE_MAX_TRACKS) {
            break;
        }
        used += (uint32_t)need;
        tracks++;
        lo = i;
    }
    q->dir[0] = '\0';
    uint32_t w = 0, n = 0;
    for (uint32_t i = lo; i < hi; i++) {
        if (paths[i] == NULL || paths[i][0] != '/' || !pq_is_audio(paths[i])) {
            continue;
        }
        size_t len = strlen(paths[i]) + 1u;
        if (len > PLAY_QUEUE_PATH_MAX || w + len > PLAY_QUEUE_POOL_BYTES || n >= PLAY_QUEUE_MAX_TRACKS) {
            continue; // cannot happen after the windowing above; never overrun the pool
        }
        if (i == start) {
            q->pos = n;
        }
        memcpy(q->pool + w, paths[i], len);
        q->off[n++] = (uint16_t)w;
        w += (uint32_t)len;
    }
    q->count = n;
    identity(q);
    if (q->shuffle) {
        shuffle_from(q, (uint16_t)q->pos);
    }
    return true;
}

static const char *entry_name(const play_queue_t *q, uint32_t e) { return e < q->count ? q->pool + q->off[e] : NULL; }
const char *pq_name(const play_queue_t *q, uint32_t i) {
    if (q->lib) {
        return i < q->count ? "" : NULL; // the name is not known until the storage task has looked the track up
    }
    const char *name = i < q->count ? entry_name(q, q->order[i]) : NULL;
    if (name != NULL && q->dir[0] == '\0') { // a playlist queue holds whole paths: the name is the file's own
        const char *slash = strrchr(name, '/');
        name = slash != NULL ? slash + 1 : name;
    }
    return name;
}
uint32_t pq_entry(const play_queue_t *q, uint32_t i) {
    if (i >= q->count) {
        return PLAY_QUEUE_NO_ENTRY;
    }
    return q->lib ? lib_entry_of_slot(q, i) : q->order[i];
}

bool pq_entry_path(const play_queue_t *q, uint32_t e, char *out, size_t n) {
    if (q->lib) {
        return e < q->count && pq_format_handle(out, n, q->lib_order, q->lib_first + e);
    }
    const char *name = entry_name(q, (uint16_t)e);
    if (name == NULL || e >= PLAY_QUEUE_MAX_TRACKS) {
        return false;
    }
    if (q->dir[0] == '\0') { // a playlist queue: the name is the whole path
        size_t l = strlen(name);
        if (l + 1u > n) {
            return false;
        }
        memcpy(out, name, l + 1u);
        return true;
    }
    int w = strcmp(q->dir, "/") == 0 ? snprintf(out, n, "/%s", name) : snprintf(out, n, "%s/%s", q->dir, name);
    return w > 0 && (size_t)w < n;
}

bool pq_path(const play_queue_t *q, uint32_t i, char *out, size_t n) { return pq_entry_path(q, pq_entry(q, i), out, n); }

bool pq_has_next(const play_queue_t *q) { return q->count != 0 && q->pos + 1u < q->count; }

// A new round: shuffled, @p forced (or a pick that is not the track that just ended) opens it; unshuffled it is the folder order again.
static pq_move_t wrap(play_queue_t *q, uint32_t forced) {
    if (q->shuffle) {
        uint32_t first = forced != PLAY_QUEUE_NO_ENTRY ? forced : pick_wrap_first(q);
        if (q->lib) {
            lib_shuffle_from(q, first);
        } else {
            shuffle_from(q, (uint16_t)first);
        }
    } else {
        q->pos = 0;
        q->wrap_first = PLAY_QUEUE_NO_ENTRY;
    }
    return PQ_WRAPPED;
}

void pq_snapshot(const play_queue_t *q, pq_snapshot_t *s) {
    if (!q->lib) {
        memcpy(s->order, q->order, q->count * sizeof q->order[0]);
    }
    s->pos = q->pos;
    s->wrap_first = q->wrap_first;
    s->rng = q->rng;
    s->lib_key = q->lib_key;
    s->lib_rot = q->lib_rot;
}

void pq_restore(play_queue_t *q, const pq_snapshot_t *s) {
    if (!q->lib) {
        memcpy(q->order, s->order, q->count * sizeof q->order[0]);
    }
    q->pos = s->pos;
    q->wrap_first = s->wrap_first;
    q->rng = s->rng;
    q->lib_key = s->lib_key;
    q->lib_rot = s->lib_rot;
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

bool pq_peek_auto(play_queue_t *q, uint32_t *entry) {
    if (q->count == 0) {
        return false;
    }
    if (q->repeat == PQ_REPEAT_ONE) {
        *entry = pq_entry(q, q->pos);
    } else if (pq_has_next(q)) {
        *entry = pq_entry(q, q->pos + 1u);
    } else if (q->repeat == PQ_REPEAT_ALL) {
        if (q->shuffle) {
            if (q->wrap_first == PLAY_QUEUE_NO_ENTRY) {
                q->wrap_first = pick_wrap_first(q);
            }
            *entry = q->wrap_first;
        } else {
            *entry = pq_entry(q, 0);
        }
    } else {
        return false;
    }
    return true;
}

pq_move_t pq_commit(play_queue_t *q, uint32_t e) {
    if (q->count == 0 || e >= q->count) {
        return PQ_END;
    }
    if (pq_entry(q, q->pos) == e) {
        return PQ_RESTART;
    }
    if (q->lib) { // the permutation is stateless: a successor that is not the planned one re-bases it on that song
        if (pq_has_next(q) && pq_entry(q, q->pos + 1u) == e) {
            q->pos++;
            q->wrap_first = PLAY_QUEUE_NO_ENTRY;
            return PQ_MOVED;
        }
        bool last = !pq_has_next(q);
        if (q->shuffle) {
            lib_shuffle_from(q, e);
        } else {
            q->pos = e;
            q->wrap_first = PLAY_QUEUE_NO_ENTRY;
        }
        return last ? PQ_WRAPPED : PQ_MOVED;
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

// SPDX-License-Identifier: MPL-2.0
#include <string.h>

#include "arena.h"

#define HDR 8u

static size_t align8(size_t n) { return (n + 7u) & ~(size_t)7u; }

void arena_init(arena_t *a, void *storage, size_t bytes) {
    a->base = storage;
    a->cap = bytes;
    a->used = 0;
    a->high = 0;
    a->failures = 0;
}

void arena_reset(arena_t *a) { a->used = 0; }

void *arena_alloc(arena_t *a, size_t n) {
    size_t need = HDR + align8(n ? n : 1u);
    if (need < n || need > a->cap - a->used) { // overflow-safe
        a->failures++;
        return NULL;
    }
    uint8_t *blk = a->base + a->used;
    *(uint32_t *)blk = (uint32_t)(need - HDR);
    a->used += need;
    if (a->used > a->high) {
        a->high = a->used;
    }
    return blk + HDR;
}

// A pointer we handed out: 8-aligned (header-sized) and inside the used part. Anything else is ignored and counted.
static int owned(arena_t *a, const uint8_t *p) {
    if (p == NULL || ((uintptr_t)p & 7u) != 0 || p < a->base + HDR || p > a->base + a->used) {
        a->failures++;
        return 0;
    }
    return 1;
}

static int is_last(const arena_t *a, const uint8_t *p) {
    return p + *(const uint32_t *)(p - HDR) == a->base + a->used;
}

void arena_free(arena_t *a, void *p) {
    if (p == NULL || !owned(a, p)) {
        return;
    }
    if (is_last(a, p)) {
        a->used = (size_t)((uint8_t *)p - HDR - a->base);
    }
}

void *arena_realloc(arena_t *a, void *p, size_t n) {
    if (p == NULL) {
        return arena_alloc(a, n);
    }
    if (!owned(a, p)) {
        return NULL;
    }
    uint8_t *b = p;
    uint32_t old = *(uint32_t *)(b - HDR);
    size_t want = align8(n ? n : 1u);
    if (want <= old) {
        return p;
    }
    if (is_last(a, b)) { // grow in place
        size_t extra = want - old;
        if (extra > a->cap - a->used) {
            a->failures++;
            return NULL;
        }
        *(uint32_t *)(b - HDR) = (uint32_t)want;
        a->used += extra;
        if (a->used > a->high) {
            a->high = a->used;
        }
        return p;
    }
    void *q = arena_alloc(a, n);
    if (q != NULL) {
        memcpy(q, p, old);
    }
    return q;
}

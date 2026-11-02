// SPDX-License-Identifier: MPL-2.0
/**
 * @file seqlock.h
 * @brief Single-writer sequence lock: lets a time-critical writer publish a small struct that a
 *        slow reader can copy, with no lock the writer could ever wait on.
 *
 * Writer: seqlock_write_begin(); store fields; seqlock_write_end().
 * Reader: do { q = seqlock_read_begin(); copy fields; } while (seqlock_read_retry(q)).
 * The copied fields are plain data (a race the retry loop discards); keep the payload to a few
 * words so the reader's retry window stays tiny. Only one writer may exist.
 */
#ifndef NEUPOD_SEQLOCK_H
#define NEUPOD_SEQLOCK_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    _Atomic uint32_t seq; /**< odd while a write is in progress */
} seqlock_t;

static inline void seqlock_write_begin(seqlock_t *s) {
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
}

static inline void seqlock_write_end(seqlock_t *s) {
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1u, memory_order_release);
}

static inline uint32_t seqlock_read_begin(const seqlock_t *s) {
    uint32_t q;
    do {
        q = atomic_load_explicit(&s->seq, memory_order_acquire);
    } while ((q & 1u) != 0u);
    return q;
}

/** Non-spinning begin: false if a write is in progress right now (the caller bounds its retries). */
static inline bool seqlock_read_try_begin(const seqlock_t *s, uint32_t *q) {
    *q = atomic_load_explicit(&s->seq, memory_order_acquire);
    return (*q & 1u) == 0u;
}

static inline bool seqlock_read_retry(const seqlock_t *s, uint32_t q) {
    atomic_thread_fence(memory_order_acquire);
    return atomic_load_explicit(&s->seq, memory_order_relaxed) != q;
}

#endif // NEUPOD_SEQLOCK_H

// SPDX-License-Identifier: MPL-2.0
#include <string.h>

#include "ram_func.h"
#include "spsc_ring.h"

bool spsc_ring_init(spsc_ring_t *r, void *storage, uint32_t frame_bytes, uint32_t capacity_frames) {
    if (r == NULL || storage == NULL || frame_bytes == 0 || capacity_frames < 2 ||
        (capacity_frames & (capacity_frames - 1u)) != 0) {
        return false;
    }
    r->buf = storage;
    r->mask = capacity_frames - 1u;
    r->frame_bytes = frame_bytes;
    atomic_store_explicit(&r->head, 0, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0, memory_order_relaxed);
    return true;
}

uint32_t NEUPOD_RAM_FUNC(spsc_ring_available)(const spsc_ring_t *r) {
    uint32_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    return head - tail;
}

uint32_t NEUPOD_RAM_FUNC(spsc_ring_space)(const spsc_ring_t *r) {
    return r->mask + 1u - spsc_ring_available(r);
}

// Copy @p frames frames starting at free-running index @p index, in at most two pieces.
static void NEUPOD_RAM_FUNC(ring_copy_in)(spsc_ring_t *r, uint32_t index, const uint8_t *src, uint32_t frames) {
    uint32_t pos = index & r->mask;
    uint32_t first = r->mask + 1u - pos;
    if (first > frames) {
        first = frames;
    }
    if (src != NULL) {
        memcpy(r->buf + (size_t)pos * r->frame_bytes, src, (size_t)first * r->frame_bytes);
        memcpy(r->buf, src + (size_t)first * r->frame_bytes, (size_t)(frames - first) * r->frame_bytes);
    } else {
        memset(r->buf + (size_t)pos * r->frame_bytes, 0, (size_t)first * r->frame_bytes);
        memset(r->buf, 0, (size_t)(frames - first) * r->frame_bytes);
    }
}

uint32_t NEUPOD_RAM_FUNC(spsc_ring_write)(spsc_ring_t *r, const void *src, uint32_t frames) {
    uint32_t head = atomic_load_explicit(&r->head, memory_order_relaxed); // we own head
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    uint32_t space = r->mask + 1u - (head - tail);
    if (frames > space) {
        frames = space;
    }
    if (frames == 0) {
        return 0;
    }
    ring_copy_in(r, head, src, frames);
    // Release: the frame data above is visible to the consumer before it sees the new head.
    atomic_store_explicit(&r->head, head + frames, memory_order_release);
    return frames;
}

uint32_t NEUPOD_RAM_FUNC(spsc_ring_read)(spsc_ring_t *r, void *dst, uint32_t frames) {
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed); // we own tail
    uint32_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    uint32_t avail = head - tail;
    if (frames > avail) {
        frames = avail;
    }
    if (frames == 0) {
        return 0;
    }
    uint32_t pos = tail & r->mask;
    uint32_t first = r->mask + 1u - pos;
    if (first > frames) {
        first = frames;
    }
    uint8_t *out = dst;
    memcpy(out, r->buf + (size_t)pos * r->frame_bytes, (size_t)first * r->frame_bytes);
    memcpy(out + (size_t)first * r->frame_bytes, r->buf, (size_t)(frames - first) * r->frame_bytes);
    // Release: the slots are free for the producer only after we have finished copying them out.
    atomic_store_explicit(&r->tail, tail + frames, memory_order_release);
    return frames;
}

uint32_t NEUPOD_RAM_FUNC(spsc_ring_discard)(spsc_ring_t *r, uint32_t frames) {
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t avail = atomic_load_explicit(&r->head, memory_order_acquire) - tail;
    if (frames > avail) {
        frames = avail;
    }
    atomic_store_explicit(&r->tail, tail + frames, memory_order_release);
    return frames;
}

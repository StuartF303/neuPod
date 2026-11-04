// SPDX-License-Identifier: MPL-2.0
// Core-1 byte source for the decoders (see stream_io.h). CORE-1 RULES apply.
#include "stream_io.h"

#include <stdatomic.h>

#include "ram_func.h"

size_t sio_read(void *ctx, void *dst, size_t n) {
    sio_t *s = ctx;
    uint8_t *d = dst;
    size_t got = 0;
    if (s->aborted) {
        return 0; // sticky: once a command interrupted us, every further read fails until the owner has dealt with it
    }
    while (got < n) {
        bool eof = *s->eof != 0; // read before the ring: data written before the flag is then visible
        uint32_t avail = spsc_ring_available(s->ring);
        uint32_t want = (uint32_t)(n - got);
        if (s->end != NULL) { // after the ring: if the next stream's bytes are visible, the boundary before them is too
            uint32_t end = *s->end;
            if (end != SIO_END_UNKNOWN) {
                uint64_t at = s->pos + got;
                uint32_t rem = end > at ? (uint32_t)(end - at) : 0u;
                if (rem == 0) {
                    break; // the stream is over; whatever follows in the ring belongs to the next one
                }
                if (want > rem) {
                    want = rem;
                }
            }
        }
        if (avail != 0) {
            got += spsc_ring_read(s->ring, d + got, want);
            continue;
        }
        if (eof) {
            break;
        }
        if (s->poll(s->ctx)) {
            s->aborted = true;
            break;
        }
        s->idle();
    }
    s->pos += got;
    return got;
}

static bool discard(sio_t *s, uint64_t n) {
    uint8_t tmp[64];
    while (n != 0) {
        size_t k = n < sizeof tmp ? (size_t)n : sizeof tmp;
        if (sio_read(s, tmp, k) != k) {
            return false;
        }
        n -= k;
    }
    return true;
}

bool sio_drain_end(sio_t *s) {
    uint8_t tmp[128];
    for (;;) {
        if (sio_read(s, tmp, sizeof tmp) < sizeof tmp) {
            return !s->aborted;
        }
    }
}

bool sio_aborted(void *ctx) { return ((sio_t *)ctx)->aborted; }

bool sio_seek(void *ctx, int64_t off, int origin) {
    sio_t *s = ctx;
    if (s->aborted) {
        return false;
    }
    if (origin == 1) {
        return off >= 0 && discard(s, (uint64_t)off);
    }
    if (off < 0) {
        return false;
    }
    if ((uint64_t)off >= s->pos) {
        return discard(s, (uint64_t)off - s->pos); // forward: just skip the bytes
    }
    // Backwards (or a jump we prefer not to stream through): ask core 0 to reposition.
    uint32_t req = ++s->seek_seq;
    *s->seek_off = (uint64_t)off;
    atomic_thread_fence(memory_order_release); // the offset must be visible before the request counter moves
    *s->seek_req = req;
    while (*s->seek_ack != req) {
        if (s->poll(s->ctx)) {
            s->aborted = true;
            return false;
        }
        s->idle();
    }
    atomic_thread_fence(memory_order_acquire); // core 0 stopped writing before it acknowledged
    (void)spsc_ring_discard(s->ring, 0xFFFFFFFFu); // core 0 is idle now: nothing newer than the ack can be in the ring
    *s->eof = 0;
    s->pos = (uint64_t)off;
    atomic_thread_fence(memory_order_release); // the flush is complete before core 0 may write again
    *s->seek_done = req;
    return true;
}

// SPDX-License-Identifier: MPL-2.0
/**
 * @file spsc_ring.h
 * @brief Lock-free single-producer / single-consumer ring buffer of fixed-size frames.
 *
 * Exactly one thread/task/core may write and exactly one may read, concurrently, with no lock:
 * the producer owns @c head, the consumer owns @c tail, and each publishes with a release store
 * and observes the other's index with an acquire load. Safe across the two RP2350 cores (SRAM is
 * coherent; only plain loads and stores are used, no read-modify-write). This is the decoder ->
 * decoder -> sink path inside the audio core, and the core 0 <-> core 1 command, data and event paths:
 * the audio core must never wait on a lock (ADR-0011).
 *
 * Capacity is a power of two (frames); indices run freely and wrap by masking, so full and empty
 * are distinguished without wasting a slot.
 */
#ifndef NEUPOD_SPSC_RING_H
#define NEUPOD_SPSC_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;
    uint32_t mask;        /**< capacity_frames - 1 */
    uint32_t frame_bytes;
    _Atomic uint32_t head; /**< producer index (frames written, free-running) */
    _Atomic uint32_t tail; /**< consumer index (frames read, free-running) */
} spsc_ring_t;

/**
 * @p storage must hold @p capacity_frames * @p frame_bytes bytes. @p capacity_frames must be a
 * power of two (2..2^31). Returns false on bad arguments.
 */
bool spsc_ring_init(spsc_ring_t *r, void *storage, uint32_t frame_bytes, uint32_t capacity_frames);

/** Frames currently readable. Exact when called by the consumer; a lower bound otherwise. */
uint32_t spsc_ring_available(const spsc_ring_t *r);

/** Frames currently writable. Exact when called by the producer; a lower bound otherwise. */
uint32_t spsc_ring_space(const spsc_ring_t *r);

/**
 * Producer: copy up to @p frames frames in (all-zero frames if @p src is NULL). Returns the
 * number written (less than requested when the ring fills; never blocks).
 */
uint32_t spsc_ring_write(spsc_ring_t *r, const void *src, uint32_t frames);

/** Consumer: copy up to @p frames frames out. Returns the number read (never blocks). */
uint32_t spsc_ring_read(spsc_ring_t *r, void *dst, uint32_t frames);

/** Consumer: drop up to @p frames frames without copying. Returns the number dropped. */
uint32_t spsc_ring_discard(spsc_ring_t *r, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_SPSC_RING_H

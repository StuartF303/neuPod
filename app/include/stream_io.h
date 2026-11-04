// SPDX-License-Identifier: MPL-2.0
/**
 * @file stream_io.h
 * @brief Core-1 byte source for the decoders: reads the core-0 data ring, waits for data without deadlocking
 *        (the caller's poll hook keeps handling commands) and does the seek handshake with core 0.
 *
 * Core-1 rules apply (no RTOS, no malloc). Host-tested with a thread playing core 0.
 *
 * Seek handshake (absolute seeks only; forward-relative seeks just discard bytes):
 *   core 1: sets seek_off, increments seek_req, waits for seek_ack == seek_req
 *   core 0: sees the request, repositions the file and its feed (nothing more is written to the ring), sets
 *           seek_ack = seek_req, then waits for seek_done == seek_req before writing again
 *   core 1: sees the ack, discards everything in the ring (it is the consumer), clears eof, sets seek_done = seek_req
 */
#ifndef NEUPOD_STREAM_IO_H
#define NEUPOD_STREAM_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "spsc_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Value of the end-of-stream byte count while core 0 has not yet written the stream's last byte. */
#define SIO_END_UNKNOWN 0xFFFFFFFFu

typedef struct {
    spsc_ring_t *ring;           /**< byte ring: core 0 produces, we consume */
    volatile uint32_t *eof;      /**< set once core 0 has written the last byte (legacy flag; may point at a constant 0 when @c end is used) */
    /** Optional (NULL = unused): the stream's total length in bytes, published by core 0 (release) once it has written the stream's
     *  last byte, SIO_END_UNKNOWN until then. The ring may already hold the NEXT stream's bytes behind it (gapless handoff): reads
     *  never cross this boundary, and at it the stream reads as ended. Core 0 publishes it BEFORE writing the first byte of the next
     *  stream, so a reader that has seen those bytes (it reads the ring first, then this) always sees the boundary. */
    const volatile uint32_t *end;
    bool (*poll)(void *ctx);     /**< called while waiting; true = abandon the read (a command needs attention) */
    void (*idle)(void);          /**< sleep until the next interrupt (or yield on the host) */
    void *ctx;
    volatile uint32_t *seek_req;   /**< written by core 1 */
    volatile uint64_t *seek_off;
    volatile uint32_t *seek_ack;   /**< written by core 0 */
    volatile uint32_t *seek_done;  /**< written by core 1 */
    uint64_t pos;                /**< bytes consumed (stream offset) */
    uint32_t seek_seq;
    bool aborted;                /**< a read or seek was abandoned because poll() returned true; sticky until the owner clears it */
} sio_t;

/** codec_io_t.read: n bytes, waiting as needed; fewer only at the end of the stream or when aborted. */
size_t sio_read(void *ctx, void *dst, size_t n);
/** Consume what is left of the stream up to its end (waits for core 0 to publish it). False if abandoned (poll() returned true). */
bool sio_drain_end(sio_t *s);
/** codec_io_t.aborted */
bool sio_aborted(void *ctx);
/** codec_io_t.seek: origin 0 = absolute, 1 = relative. */
bool sio_seek(void *ctx, int64_t off, int origin);

#ifdef __cplusplus
}
#endif

#endif

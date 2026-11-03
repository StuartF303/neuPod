// SPDX-License-Identifier: MPL-2.0
/**
 * @file codec.h
 * @brief Decoders on the bare-metal audio core: FLAC and MP3 (dr_libs) and ALAC (Apple), all on one static arena.
 *
 * Core-1 rules apply (ADR-0011): no malloc, no stdio, no RTOS. Everything the decoders need comes from the arena,
 * which is reset at every codec_open(). Output is interleaved stereo int32, left-justified: exact for the integer
 * codecs (FLAC, ALAC), rounded and clipped for MP3's floating point output. Mono is duplicated to both channels.
 */
#ifndef NEUPOD_CODEC_H
#define NEUPOD_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "neupod_md5.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Arena size: the largest of FLAC (8192-sample blocks, stereo), MP3 and ALAC (4096 frames, 24-bit stereo; 32-bit ALAC
 *  is rejected). The binding case is ALAC 24-bit (measured high water 98,456 B) plus a 2 KB margin, rounded up to 1 KB.
 *  tests/test_codecs.c arena_worst_cases_fit checks it. */
#define CODEC_ARENA_BYTES (99u * 1024u)

typedef enum { CODEC_NONE = 0, CODEC_FLAC, CODEC_MP3, CODEC_ALAC } codec_kind_t;

typedef enum {
    CODEC_OK = 0,
    CODEC_ERR_OPEN = 1,   /**< decoder refused the stream */
    CODEC_ERR_MEM = 2,    /**< arena too small for this stream */
    CODEC_ERR_FORMAT = 3, /**< stream differs from what core 0 announced, or unsupported */
    CODEC_ERR_DATA = 4,   /**< damaged data */
    CODEC_ERR_TRUNCATED = 5, /**< the stream ended before the length the file announced */
} codec_err_t;

typedef struct {
    size_t (*read)(void *ctx, void *dst, size_t n);   /**< blocks until n bytes or end of stream; fewer only at the end / on abort */
    bool (*seek)(void *ctx, int64_t offset, int origin); /**< origin 0 = from start, 1 = from current */
    bool (*aborted)(void *ctx);                          /**< optional: the last read was abandoned (end of data is then not an end of stream) */
    void *ctx;
} codec_io_t;

typedef struct {
    uint32_t kind;
    uint32_t rate;
    uint32_t channels, bits; /**< bits: source bit depth (FLAC/ALAC); ignored for MP3 */
    bool verify;             /**< FLAC: MD5 of the decoded samples */
    uint64_t total_frames;   /**< length announced by the container (0 = unknown): ending short is then a decode error */
    uint8_t cookie[64];      /**< ALAC magic cookie */
    uint32_t cookie_len;
} codec_params_t;

typedef struct codec codec_t;
struct codec {
    uint32_t kind;
    codec_io_t io;
    uint32_t channels;  /**< native channels (1 or 2) */
    uint32_t bits;
    bool verify;
    uint64_t total_frames, frames_out;
    md5_t md5;
    void *impl;         /**< drflac* / drmp3* (arena) */
    /* ALAC */
    void *alac;
    uint8_t *frame_buf, *out_buf;
    uint32_t frame_cap, out_frames, out_pos, out_bps;
};

codec_err_t codec_open(codec_t *c, const codec_params_t *p, const codec_io_t *io);
/**
 * Produce up to @p max_frames (<= 512) stereo frames into @p out. Returns frames produced; 0 = end of stream (or
 * the input was aborted: ask the io); negative = decode error (codec_err_t negated). A stream that ends short of its announced length is an error, not an end.
 */
int32_t codec_read(codec_t *c, int32_t *out, uint32_t max_frames);
/** FLAC verify mode: finish the MD5. */
void codec_md5_final(codec_t *c, uint8_t out[16]);
void codec_close(codec_t *c);
size_t codec_arena_high_water(void);
size_t codec_arena_failures(void);

/** Hash @p frames native-channel frames the way FLAC defines its MD5: each sample as ceil(bits/8) little-endian bytes of its right-justified value. */
void codec_md5_pcm(md5_t *m, const int32_t *s32, uint32_t frames, uint32_t channels, uint32_t bits);

#ifdef __cplusplus
}
#endif

#endif

// SPDX-License-Identifier: MPL-2.0
/**
 * @file meta.h
 * @brief File-header parsing for the codecs, run on core 0: FLAC (STREAMINFO + Vorbis comments), MP3 (ID3v2,
 *        first frame header, Xing/Info) and MP4/M4A (moov: sample tables, ALAC magic cookie, ilst tags).
 *        Portable, no heap, reads through a callback so it works on a file or in memory. Host-tested.
 */
#ifndef NEUPOD_META_H
#define NEUPOD_META_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wav.h" /* wav_read_fn */

#ifdef __cplusplus
extern "C" {
#endif

typedef wav_read_fn meta_read_fn;

typedef struct {
    char title[64], artist[64], album[64]; /**< UTF-8, cut at a character boundary; empty if absent */
    uint16_t track;                        /**< 0 = unknown */
} audio_tags_t;

typedef enum {
    META_OK = 0,
    META_E_READ,
    META_E_FORMAT,      /**< not this kind of file, or damaged */
    META_E_CHANNELS,    /**< more than 2 channels */
    META_E_RATE,        /**< not 44.1/48/88.2/96 kHz */
    META_E_BITS,        /**< unsupported bit depth */
    META_E_BLOCKSIZE,   /**< FLAC block size above what the decoder memory allows */
    META_E_AAC,         /**< MP4 audio that is AAC: not supported yet */
    META_E_CODEC,       /**< MP4 audio in another codec */
    META_E_NO_AUDIO,
    META_E_ALAC32,      /**< ALAC at 32 bits: its buffers do not fit the codec arena */
} meta_err_t;

const char *meta_err_str(meta_err_t e);

/** Scratch space for header parsing (FLAC comment blocks up to its size are read; larger ones are skipped). Without one, tags are skipped. */
void meta_set_scratch(void *buf, size_t bytes);

/**
 * Largest FLAC block size (samples) the core-1 arena is sized for. dr_flac keeps 4 bytes per sample and channel, but 8 for
 * 32-bit streams (side-channel reconstruction needs 33 bits), so 32-bit FLAC is limited to half the block size.
 */
#define META_FLAC_MAX_BLOCK 8192u
#define META_FLAC_MAX_BLOCK_32BIT 4096u

/* ---- FLAC ---- */
typedef struct {
    uint32_t rate;
    uint8_t channels, bits;
    uint64_t total_frames; /**< 0 if the encoder did not record it */
    uint8_t md5[16];       /**< all zero = the encoder did not compute one */
    uint32_t min_block, max_block;
    uint64_t audio_offset; /**< file offset of the first audio frame (after the last metadata block) */
    audio_tags_t tags;
} flac_info_t;
meta_err_t flac_parse(meta_read_fn rd, void *ctx, uint64_t file_size, flac_info_t *out);

/**
 * Build the 42-byte header the core-1 decoder is fed in place of the file's metadata: "fLaC" plus one STREAMINFO block
 * flagged last. Core 0 skips the file's other metadata blocks (pictures, comments) instead of making the decoder
 * stream through them. Returns 42.
 */
#define FLAC_SYNTH_HEADER_BYTES 42u
uint32_t flac_make_header(const flac_info_t *f, uint8_t out[FLAC_SYNTH_HEADER_BYTES]);

/* ---- MP3 ---- */
typedef struct {
    uint32_t rate;
    uint8_t channels;
    uint32_t bitrate_kbps; /**< first frame's bitrate (CBR) or the average (VBR) */
    bool vbr;
    uint64_t total_frames; /**< PCM frames: from the Xing/Info frame count, else estimated from the size */
    bool estimated;
    uint64_t audio_offset; /**< file offset of the first frame header (after ID3v2) */
    audio_tags_t tags;
} mp3_info_t;
meta_err_t mp3_parse(meta_read_fn rd, void *ctx, uint64_t file_size, mp3_info_t *out);

/* ---- MP4 / M4A ---- */
#define MP4_COOKIE_MAX 64u
typedef struct {
    bool is_alac;
    uint32_t rate;
    uint8_t channels, bits;
    uint32_t frame_length;               /**< ALAC frames per packet */
    uint64_t total_frames;               /**< from mdhd duration, converted to the sample rate */
    uint8_t cookie[MP4_COOKIE_MAX];      /**< ALACSpecificConfig (+ optional layout) */
    uint32_t cookie_len;
    uint32_t sample_count;               /**< packets */
    uint64_t file_size;                  /**< packets must lie inside it */
    uint32_t max_packet;                 /**< largest packet the ALAC decoder accepts */
    uint64_t stsz_off, stsc_off, stco_off;
    uint32_t stsz_fixed, stsc_count, stco_count;
    bool co64;
    audio_tags_t tags;
} mp4_info_t;
meta_err_t mp4_parse(meta_read_fn rd, void *ctx, uint64_t file_size, mp4_info_t *out);

/** Walks the packets of an MP4 track in order using the sample tables (stsz, stsc, stco/co64), with small read caches. */
typedef struct {
    const mp4_info_t *info;
    uint32_t sample;          /**< next packet index */
    uint32_t chunk;           /**< current chunk (0-based) */
    uint32_t in_chunk;        /**< packets already taken from the current chunk */
    uint32_t chunk_samples;   /**< packets in the current chunk */
    uint32_t run;             /**< stsc entry in force */
    uint32_t next_run_chunk;  /**< first chunk (0-based) of the next stsc entry */
    uint64_t pos;             /**< byte offset of the next packet inside the chunk */
    uint32_t szbuf[64];
    uint32_t sz_first;        /**< index of szbuf[0] */
    uint32_t sz_n;
} mp4_iter_t;
void mp4_iter_init(mp4_iter_t *it, const mp4_info_t *info);
/** Next packet: file offset and size. False at the end or on a read error / inconsistent tables. */
bool mp4_iter_next(mp4_iter_t *it, meta_read_fn rd, void *ctx, uint64_t *offset, uint32_t *size);

/* ---- shared helpers ---- */
/** Copy @p n bytes of ISO-8859-1 / UTF-8 / UTF-16 text (ID3 encoding 0..3) to UTF-8 in @p dst (cap bytes incl. NUL). */
void meta_text_to_utf8(char *dst, size_t cap, const uint8_t *src, size_t n, int id3_encoding);

#ifdef __cplusplus
}
#endif

#endif

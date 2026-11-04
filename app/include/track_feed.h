// SPDX-License-Identifier: MPL-2.0
/**
 * @file track_feed.h
 * @brief Core-0 side of one track: parse the file's header (WAV, FLAC, MP3, M4A/ALAC), describe the stream to the audio core
 *        and feed its bytes into the audio data ring. Portable: the file is read through a callback, so the storage task
 *        uses the SD card and the host tests use a plain file or memory.
 *
 * What goes into the data ring is what the core-1 decoder expects (docs/audio.md): WAV: the raw PCM bytes; FLAC: a synthesised
 * 42-byte header and then the frames; MP3: the file from its first frame; ALAC: the packets, each behind a 4-byte big-endian
 * length (the MP4 container is demuxed here).
 */
#ifndef NEUPOD_TRACK_FEED_H
#define NEUPOD_TRACK_FEED_H

#include <stdbool.h>
#include <stdint.h>

#include "audio_engine.h"
#include "meta.h"
#include "wav.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRACK_FEED_CHUNK 2048u /**< scratch bytes track_feed_step() needs; also one file read */

/** What the player shows about a track (filled by track_feed_open). */
typedef struct {
    audio_stream_desc_t desc;
    char format[32];       /**< "FLAC 24/96", "MP3 VBR ~245k", ... */
    uint32_t rate;
    uint64_t total_frames; /**< playing length, after any encoder delay / padding trim */
    audio_tags_t tags;
    bool title_is_name;
    bool estimated;
    bool verify;
    uint32_t codec;
    uint8_t md5[16];
} track_info_t;

typedef enum { TRACK_FEED_BYTES, TRACK_FEED_M4A } track_feed_kind_t;

typedef struct {
    meta_read_fn rd; /**< random access to the file */
    void *rctx;
    track_feed_kind_t kind;
    /* TRACK_FEED_BYTES: [hdr][file bytes from base, data_len of them] */
    uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
    uint32_t hdr_len, hdr_pos;
    uint64_t base, data_len, remaining;
    /* TRACK_FEED_M4A */
    mp4_info_t mp4;
    mp4_iter_t it;
    uint64_t cur_off;
    uint32_t cur_left, cur_size;
    bool len_pending;
} track_feed_t;

/**
 * Parse the file. @p path decides the type by its extension and supplies the fallback title. Header parsing borrows the scratch
 * set with meta_set_scratch(). Returns NULL on success, else a short reason for the user ("Not a playable file", ...).
 * @p verify: FLAC only, the stream decodes flat out without sound and the MD5 is compared.
 */
const char *track_feed_open(track_feed_t *t, const char *path, uint64_t size, meta_read_fn rd, void *rctx, bool verify,
                            track_info_t *info);

typedef enum { TRACK_FEED_MORE, TRACK_FEED_DONE, TRACK_FEED_ERROR } track_feed_status_t;
/**
 * Write as much as fits into the audio data ring (a few KB per call). TRACK_FEED_DONE: the last byte is written; the caller then
 * calls audio_stream_end(). @p buf is TRACK_FEED_CHUNK bytes of scratch.
 */
track_feed_status_t track_feed_step(track_feed_t *t, uint8_t *buf);

/** The track_feed_t was copied to another address (the successor becoming the current track): re-point its internal references. */
void track_feed_relocated(track_feed_t *t);

/** The decoder asked for stream byte offset @p off again (stream_io.h seek): restart the feed there. */
void track_feed_rewind(track_feed_t *t, uint64_t off);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_TRACK_FEED_H

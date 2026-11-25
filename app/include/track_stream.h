// SPDX-License-Identifier: MPL-2.0
/**
 * @file track_stream.h
 * @brief Core-0 stream controller: plays one track at a time into the audio core and hands over to the next one gaplessly.
 *
 * Owned by the storage task (the only caller). It sits between the file system / player (injected through track_stream_ops_t, so
 * the host tests can drive it with plain files) and the audio core's API (audio_engine.h). Gapless handoff, in short (docs/audio.md):
 *   1. the current track plays; once the audio core has its decoder open the player is asked which track follows;
 *   2. that file is opened and parsed (rate and channel count must match), announced with audio_stream_next(), and kept open;
 *   3. when the current track is completely fed, audio_stream_end() publishes its length and the successor's bytes follow at once;
 *   4. the audio core switches decoders at the boundary and reports the track change. Anything that does not fit just does not announce
 *      a successor: the track ends normally and the player starts the next one the ordinary way.
 */
#ifndef NEUPOD_TRACK_STREAM_H
#define NEUPOD_TRACK_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "track_feed.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Open @p path for reading: set the random-access read, its context and the file size. NULL on success, else the reason (in @p msg or static). */
    const char *(*open)(const char *path, meta_read_fn *rd, void **rctx, uint64_t *size, char *msg, size_t msg_n);
    /** Close what open() returned. */
    void (*close)(void *rctx);
    /** Gapless: path of the track after the current one, if the audio core may be told about it now. */
    bool (*next_track)(char *path, size_t n);
    /** The audio core was told to continue with this track by itself. */
    void (*next_announced)(const track_info_t *info);
    /** The track passed to track_stream_start() is playing. */
    void (*started)(const char *path, const track_info_t *info);
    /** Gapless: the audio core has just started playing the announced successor (its first sample is being played). */
    void (*track_changed)(const char *path, const track_info_t *info);
    /** It could not be started or fed (reason for the user). */
    void (*failed)(const char *msg);
    /** The stream that was playing broke (card read error mid-track). Not the answer to a start request, so the owner must not count it as one.
     *  Optional: failed() is used when it is NULL. */
    void (*stream_failed)(const char *msg);
    uint32_t (*now_ms)(void);
    void (*delay_ms)(uint32_t ms);
} track_stream_ops_t;

void track_stream_init(const track_stream_ops_t *ops);
/** Replace what plays with @p path (hard start). @p verify: FLAC MD5 check without sound. Reports through ops->started / ops->failed. */
void track_stream_start(const char *path, bool verify);
/** As track_stream_start(), beginning at PCM frame @p frame of the track (resume; track_feed_seek() says how exact that is). ops->started reports the real start in info->start_frame. */
void track_stream_start_at(const char *path, bool verify, uint64_t frame);
/** Stop feeding (the caller stops the audio core itself); cancels an announced successor. */
void track_stream_stop(void);
/** One feeding step; call every millisecond while active. */
void track_stream_pump(void);
bool track_stream_active(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_TRACK_STREAM_H

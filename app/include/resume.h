// SPDX-License-Identifier: MPL-2.0
/**
 * @file resume.h
 * @brief Resume: remember what was playing (folder track, position, playing/paused) in the settings store and give it back at boot.
 *
 * Stored as two settings items (SETTING_RESUME0 / SETTING_RESUME1: a small header plus the track's full path, which does not fit one
 * 96-byte item). Writing only changes the RAM image and marks it dirty; the SD write is the settings store's own (settings_flush on the
 * storage task, with the write lock, debounced 1.5 s). So this module never touches the card and may be called from any task but core 1.
 *
 * Rate limiting: events (pause, resume, track change, stop) are stored at once; the periodic update while playing is stored at most every
 * RESUME_PERIOD_MS. The path is limited to RESUME_PATH_MAX_STORED bytes; a longer one is not stored (the old record is dropped).
 */
#ifndef NEUPOD_RESUME_H
#define NEUPOD_RESUME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "storage_service.h" /* file_entry_t */

#ifdef __cplusplus
extern "C" {
#endif

#define RESUME_PATH_MAX 256u         /**< buffer size for a path (same as PLAYER_PATH_MAX / NFS_PATH_MAX) */
#define RESUME_PATH_MAX_STORED 177u  /**< longest path that fits the two items */
#define RESUME_PERIOD_MS 30000u      /**< periodic update while playing */

typedef struct {
    char path[RESUME_PATH_MAX]; /**< full path of the current track, e.g. "/Music/Album/03 Song.flac" */
    uint16_t index;             /**< 0-based position in the folder queue when saved (fallback if the file is gone) */
    uint32_t frame;             /**< playback position in PCM frames of the track */
    uint32_t size;              /**< file size in bytes when saved: a different size means the file was replaced */
    bool playing;               /**< was playing (true) or paused; boot restores paused either way */
} resume_state_t;

typedef enum { RESUME_EVENT, RESUME_PERIODIC } resume_why_t;

/** A RESUME_PERIODIC update would be stored at @p now_ms (cheap check before building one). */
bool resume_periodic_due(uint32_t now_ms);
/**
 * Store @p s. A RESUME_PERIODIC update is dropped (false) if the last stored one is younger than RESUME_PERIOD_MS at @p now_ms.
 * False also if the path is empty, not absolute or too long (the old record is cleared then).
 */
bool resume_save(const resume_state_t *s, resume_why_t why, uint32_t now_ms);
/** Forget the saved context (queue ended, folder gone). */
void resume_clear(void);
/** Read the saved context. False if there is none or it does not validate. */
bool resume_load(resume_state_t *out);
/** Split @p path at its last '/': folder into @p dir ("/" for the root) and the file name returned through @p name. False if it does not fit. */
bool resume_split(const char *path, char *dir, size_t dir_n, const char **name);

#define RESUME_MIN_BUFFERED_MS 300u /**< audio that must be buffered before a save may run during playback (when enabled at all) */
/** Milliseconds of audio held by the data ring (@p data_bytes, assumed @p bytes_per_frame each when decoded: pass 8 = 32-bit stereo when unsure) plus @p pcm_frames. */
uint32_t resume_buffered_ms(uint32_t rate, uint32_t data_bytes, uint32_t bytes_per_frame, uint32_t pcm_frames);
bool resume_write_while_playing_ok(uint32_t buffered_ms);

typedef enum {
    RESUME_PLAN_EXACT,   /**< the saved file is there, same size: resume at the saved frame */
    RESUME_PLAN_CHANGED, /**< it is there but its size differs (replaced): the same track from its beginning */
    RESUME_PLAN_GONE,    /**< it is gone: the playable entry at the saved queue position, from its beginning */
    RESUME_PLAN_NOTHING  /**< the folder holds nothing playable: discard */
} resume_plan_t;
/**
 * Decide what to resume, given the listing @p entries of the saved folder and the saved file's @p name. Sets @p idx (entry to play) and
 * @p frame. A saved size of 0 (unknown) is not compared.
 */
resume_plan_t resume_plan(const resume_state_t *r, const char *name, const file_entry_t *entries, uint32_t count, uint32_t *idx,
                          uint32_t *frame);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_RESUME_H

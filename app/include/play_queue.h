// SPDX-License-Identifier: MPL-2.0
/**
 * @file play_queue.h
 * @brief The play queue: the playable files of one folder, in the order the Files screen shows them, and where we are in them.
 *
 * "Folder = album" until there is a library database: playing a file from Files queues the rest of its folder, starting at that
 * file (the files before it stay queued for Previous). Pure data and rules, no OS: host-tested. Names are packed into a small pool
 * (names are at most FILES_NAME_LEN - 1 bytes); a folder that does not fit keeps a window around the chosen file.
 */
#ifndef NEUPOD_PLAY_QUEUE_H
#define NEUPOD_PLAY_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "storage_service.h" /* file_entry_t */

#ifdef __cplusplus
extern "C" {
#endif

#define PLAY_QUEUE_POOL_BYTES 3072u /**< packed names (NUL included): about 100 tracks of typical names */
#define PLAY_QUEUE_MAX_TRACKS 128u
#define PLAY_QUEUE_DIR_MAX 256u
#define PLAY_QUEUE_RESTART_S 3u /**< Previous after more than this many seconds into a track restarts it (iPod behaviour) */

typedef struct {
    char dir[PLAY_QUEUE_DIR_MAX];             /**< folder, "/" or "/a/b" */
    uint16_t off[PLAY_QUEUE_MAX_TRACKS];      /**< start of each name in pool */
    char pool[PLAY_QUEUE_POOL_BYTES];
    uint16_t count;                           /**< tracks queued (0 = no queue) */
    uint16_t pos;                             /**< current track, 0-based */
} play_queue_t;

typedef enum { PQ_MOVED, PQ_RESTART, PQ_END } pq_move_t;

/** A name the player can play: .wav .flac .mp3 .m4a .mp4 (case-insensitive). Everything else (cover.jpg, .csv, folders) is skipped. */
bool pq_is_audio(const char *name);

/** Empty the queue. */
void pq_clear(play_queue_t *q);

/**
 * Queue the playable files among @p entries (a folder listing: directories and other files are skipped) and make the entry at
 * @p start_index current. False (queue left empty) if that entry is not a playable file. When the folder holds more than fits,
 * the queue is the window of playable files around the chosen one (from it onwards first).
 */
bool pq_build(play_queue_t *q, const char *dir, const file_entry_t *entries, uint32_t count, uint32_t start_index);

/** Name of queue entry @p i (NULL when out of range). */
const char *pq_name(const play_queue_t *q, uint32_t i);
/** Full path of entry @p i into @p out. False if out of range or it does not fit. */
bool pq_path(const play_queue_t *q, uint32_t i, char *out, size_t n);

/** Next track: PQ_MOVED (pos advanced) or PQ_END (it was the last; position unchanged). */
pq_move_t pq_next(play_queue_t *q);
/** Previous track: more than PLAY_QUEUE_RESTART_S into the track, or on the first, PQ_RESTART (position unchanged); else PQ_MOVED. */
pq_move_t pq_prev(play_queue_t *q, uint32_t elapsed_s);
/** True if a track follows the current one. */
bool pq_has_next(const play_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PLAY_QUEUE_H

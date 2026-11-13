// SPDX-License-Identifier: MPL-2.0
/**
 * @file play_queue.h
 * @brief The play queue: the playable files of one folder, in the order the Files screen shows them, and where we are in them.
 *
 * "Folder = album" until there is a library database: playing a file from Files queues the rest of its folder, starting at that
 * file (the files before it stay queued for Previous). Pure data and rules, no OS: host-tested. Names are packed into a small pool
 * (names are at most FILES_NAME_LEN - 1 bytes); a folder that does not fit keeps a window around the chosen file.
 *
 * Library queues (docs/adr/0017, L2): a queue can instead run over a range of one of the library's order tables (the songs of an album, an
 * artist, a genre, or all songs). It then holds no names at all, only (order, first, count) and where it is in it, so it costs the same for
 * 100,000 songs as for 10; an entry is a position in the range and its path is looked up by the storage task when the track starts (the
 * "path" the queue hands out is a handle, "@lib/<order>/<position>", see pq_is_handle). Shuffle there is a stateless keyed permutation of the
 * range (a 4-round Feistel network, cycle-walked into range), so it needs no array and visits every song exactly once.
 *
 * Play order: entries are stored in folder order; order[slot] says which entry plays at each slot, and pos is a SLOT. Without
 * shuffle the order is the identity. Shuffle is a seeded Fisher-Yates permutation of the slots (no allocation: the array is part of
 * the struct), made with the current track at slot 0, so it stays current. Repeat-all wraps to slot 0 (re-shuffling, and never starting
 * the new round with the track that just ended when there is a choice); repeat-one is only applied to automatic advances, a press of
 * Next still moves on.
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

#ifdef NEUPOD_ART_HIRES
#define PLAY_QUEUE_POOL_BYTES 8192u /**< packed names (NUL included): a folder's names, or the whole paths of a playlist (about 130 tracks of typical paths) */
#else
#define PLAY_QUEUE_POOL_BYTES 3072u /**< packed names (NUL included): about 100 tracks of typical names */
#endif
#define PLAY_QUEUE_MAX_TRACKS 128u
#define PLAY_QUEUE_DIR_MAX 256u
#define PLAY_QUEUE_PATH_MAX 256u /**< longest whole path a playlist queue entry may have (NUL included) */
#define PLAY_QUEUE_NO_ENTRY 0xFFFFFFFFu
#define PLAY_QUEUE_LIB_MAX 0x7FFFFFFFu /**< most songs a library queue can hold */
#define PLAY_QUEUE_RESTART_S 3u /**< Previous after more than this many seconds into a track restarts it (iPod behaviour) */

typedef enum { PQ_SHUFFLE_OFF = 0, PQ_SHUFFLE_SONGS = 1 } pq_shuffle_t;
typedef enum { PQ_REPEAT_OFF = 0, PQ_REPEAT_ONE = 1, PQ_REPEAT_ALL = 2 } pq_repeat_t;

typedef struct {
    char dir[PLAY_QUEUE_DIR_MAX];             /**< folder, "/" or "/a/b" */
    uint16_t off[PLAY_QUEUE_MAX_TRACKS];      /**< start of each name in pool */
    char pool[PLAY_QUEUE_POOL_BYTES];
    uint32_t count;                           /**< tracks queued (0 = no queue) */
    uint32_t pos;                             /**< current slot in the play order, 0-based */
    uint16_t order[PLAY_QUEUE_MAX_TRACKS];    /**< slot -> entry (index into off[]); identity unless shuffled */
    uint32_t wrap_first;                      /**< entry chosen to open the next round (repeat-all + shuffle), PLAY_QUEUE_NO_ENTRY if not yet */
    uint32_t rng;                             /**< xorshift32 state; never 0 */
    uint8_t shuffle;                          /**< pq_shuffle_t; survives pq_clear()/pq_build() */
    uint8_t repeat;                           /**< pq_repeat_t; survives pq_clear()/pq_build() */
    uint8_t lib;                              /**< 1: a library range queue (pq_build_range): names, order[] and off[] are unused; count is the range length */
    uint8_t lib_order;                        /**< lib: which order table (lib_order_t) */
    uint32_t lib_first;                       /**< lib: position of the range's first song in that table */
    uint32_t lib_key, lib_rot;                /**< lib + shuffle: entry(slot) = perm((slot + lib_rot) mod count) under lib_key */
} play_queue_t;

/** A range of one of the library's order tables: @p order is a lib_order_t (library.h), positions first .. first + count - 1. */
typedef struct {
    uint8_t order;
    uint32_t first, count;
} pq_range_t;

/** PQ_MOVED: pos advanced. PQ_RESTART: same track again. PQ_WRAPPED: pos went back to the start of a new round. PQ_END: nothing follows. */
typedef enum { PQ_MOVED, PQ_RESTART, PQ_END, PQ_WRAPPED } pq_move_t;

/** A name the player can play: .wav .flac .mp3 .m4a .mp4 (case-insensitive). Everything else (cover.jpg, .csv, folders) is skipped. */
bool pq_is_audio(const char *name);

/** Empty the queue (the shuffle and repeat modes are kept). */
void pq_clear(play_queue_t *q);

/** The handle a library queue hands out as a "path": "@lib/<order>/<position in the table>". Never a real path (those start with '/'). */
bool pq_is_handle(const char *path);
bool pq_parse_handle(const char *path, uint8_t *order, uint32_t *pos);
/** Format the handle of (@p order, @p pos) into @p out. False if it does not fit. */
bool pq_format_handle(char *out, size_t n, uint8_t order, uint32_t pos);

/**
 * Queue a library range and make song @p start (an index into the range) current; PLAY_QUEUE_NO_ENTRY picks a random one (Shuffle Songs).
 * The queue's shuffle flag decides the order: set it first (q->shuffle) for a shuffled queue. False (queue left empty) for an empty or
 * oversized range or a start outside it.
 */
bool pq_build_range(play_queue_t *q, const pq_range_t *range, uint32_t start);

/**
 * Queue the playable files among @p entries (a folder listing: directories and other files are skipped) and make the entry at
 * @p start_index current. False (queue left empty) if that entry is not a playable file. When the folder holds more than fits,
 * the queue is the window of playable files around the chosen one (from it onwards first).
 */
bool pq_build(play_queue_t *q, const char *dir, const file_entry_t *entries, uint32_t count, uint32_t start_index);

/**
 * Queue a playlist: the files at @p paths (absolute paths; @p count of them) in that order, entry @p start current. The queue is then a folder queue with no
 * folder (dir ""): each name is a whole path, and pq_name() gives the file's own name. Entries that are not audio files by name are skipped. When the
 * paths do not fit the pool (PLAY_QUEUE_POOL_BYTES, PLAY_QUEUE_MAX_TRACKS) the window around @p start is kept (from it onwards first). False (queue left
 * empty) when @p start is not a playable entry or a path is 256 bytes or longer.
 */
bool pq_build_paths(play_queue_t *q, const char *const *paths, uint32_t count, uint32_t start);

/** Seed the shuffle generator (0 is replaced by a constant). Call before pq_set_shuffle() / pq_build() with something unpredictable. */
void pq_seed(play_queue_t *q, uint32_t seed);
/** Switch shuffle. On: the current track becomes slot 0 and the rest is permuted; off: folder order, the current track keeps playing. */
void pq_set_shuffle(play_queue_t *q, pq_shuffle_t mode);
void pq_set_repeat(play_queue_t *q, pq_repeat_t mode);

/** Name of the track at play-order slot @p i (NULL when out of range). */
const char *pq_name(const play_queue_t *q, uint32_t i);
/** Full path of the track at slot @p i into @p out. False if out of range or it does not fit. */
bool pq_path(const play_queue_t *q, uint32_t i, char *out, size_t n);
/** Entry (folder-order index) playing at slot @p i, PLAY_QUEUE_NO_ENTRY when out of range. */
uint32_t pq_entry(const play_queue_t *q, uint32_t i);
/** Full path of entry @p e (folder order, not play order). */
bool pq_entry_path(const play_queue_t *q, uint32_t e, char *out, size_t n);

/** Everything a move can change besides the track names: used to put the queue back exactly as it was. */
typedef struct {
    uint16_t order[PLAY_QUEUE_MAX_TRACKS];
    uint32_t pos, wrap_first;
    uint32_t rng;
    uint32_t lib_key, lib_rot; /* library queues: the permutation (order[] is not used) */
} pq_snapshot_t;
void pq_snapshot(const play_queue_t *q, pq_snapshot_t *s);
void pq_restore(play_queue_t *q, const pq_snapshot_t *s);

/** A press of Next: PQ_MOVED, PQ_WRAPPED (repeat-all at the last slot) or PQ_END (last slot, no repeat-all; position unchanged). */
pq_move_t pq_next(play_queue_t *q);
/** The track ended by itself: as pq_next(), except repeat-one answers PQ_RESTART (same track again). */
pq_move_t pq_auto_next(play_queue_t *q);
/** Previous track: more than PLAY_QUEUE_RESTART_S into the track, or on the first slot, PQ_RESTART (position unchanged); else PQ_MOVED. */
pq_move_t pq_prev(play_queue_t *q, uint32_t elapsed_s);
/** True if a later slot follows the current one (ignores repeat: used for skipping tracks that cannot be played). */
bool pq_has_next(const play_queue_t *q);
/**
 * Which entry pq_auto_next() would play next (gapless look-ahead): false if it would stop. At the end of a repeat-all + shuffle
 * round this fixes the opening track of the next round, so the answer stays the same until the move is made.
 */
bool pq_peek_auto(play_queue_t *q, uint32_t *entry);
/**
 * The audio core went on to entry @p e by itself (it was offered earlier; the modes may have changed since). Makes that the
 * current track: the planned move when it matches, otherwise the entry is pulled in as the next one. Returns what happened.
 */
pq_move_t pq_commit(play_queue_t *q, uint32_t e);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PLAY_QUEUE_H

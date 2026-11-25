// SPDX-License-Identifier: MPL-2.0
/**
 * @file plays_store.h
 * @brief What was played, and how often: the numbers behind the Music > Playlists smart lists (Most played, Unplayed, Top rated), kept on the card in
 *        /.neupod/plays.bin (design review LB-7).
 *
 * An entry is keyed like a rating, by the file's path AND size (ratings_key), so a rebuilt library keeps it. It also keeps the path and the title /
 * artist the track had when it was played or rated: a smart list has to open the file and draw a row without the library (the key is a hash, it cannot
 * be turned back into a path). A "play" is counted once per track start, when half of it has been heard or the track ends (play_tracker.h); an entry is
 * also made, with zero plays, when a track is rated or added to On-The-Go, so Top rated can list it.
 *
 * "Last played" is a serial number that counts up with every recorded play (the player has no real clock on every board): a bigger number is more recent.
 *
 * The table (PLAYS_MAX entries) lives in RAM and is always usable; when it is full the entry with the fewest plays goes (never a rated one while an
 * unrated one is left; ties: the least recently touched). plays_poll() (storage task only) loads the file once the settings store is attached to the card and
 * writes it after a change, under the same nfs write scope and write-lock discipline as ratings_store.h (confined to /.neupod), through a temp file that is
 * synced and then renamed over the old one, so a power cut keeps the previous file or the complete new one. On a write-protected card the numbers stay in RAM
 * and are written as soon as the card allows (plays_persistent() says which).
 *
 * File: "NPPL", version 1, serial u32, count u32, then per entry key u32, plays u16, last u32, stamp u32, path / title / artist as u8 length + bytes,
 * then CRC-32 of everything before it. Only the builds with the playlists (NEUPOD_ART_HIRES: hosted, Y2, PSRAM boards) keep a table; the Pico 2 W (frozen)
 * compiles it down to nothing (PLAYS_MAX 0: every call is a no-op).
 */
#ifndef NEUPOD_PLAYS_STORE_H
#define NEUPOD_PLAYS_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NEUPOD_ART_HIRES
#define PLAYS_MAX 128u
#else
#define PLAYS_MAX 0u
#endif
#define PLAYS_PATH_MAX 96u  /**< bytes, NUL included: a longer path is not tracked */
#define PLAYS_TITLE_MAX 40u
#define PLAYS_ARTIST_MAX 32u
#define PLAYS_FILE "/.neupod/plays.bin"
#define PLAYS_TMP "/.neupod/plays.tmp" /**< a save is written here in full, then renamed over PLAYS_FILE */
#define PLAYS_RETRY_MS 30000u

typedef struct {
    uint32_t key;   /**< ratings_key(path, size); 0 = free slot */
    uint16_t plays; /**< counted plays (saturates) */
    uint32_t last;  /**< serial of the last counted play, 0 = never played */
    uint32_t stamp; /**< serial of the last touch (play, rating, On-The-Go): eviction order */
    char path[PLAYS_PATH_MAX], title[PLAYS_TITLE_MAX], artist[PLAYS_ARTIST_MAX];
} plays_entry_t;

/**
 * Note the track at @p path of @p size bytes: makes the entry if there is none (title and artist are what the lists show) and, with @p played, counts a play and
 * stamps it as the latest. A path of PLAYS_PATH_MAX bytes or more is ignored. Any task; the card is written later by plays_poll(). True when recorded.
 */
bool plays_record(const char *path, uint64_t size, const char *title, const char *artist, bool played);
/** Counted plays of the entry with @p key, 0 when unknown. Any task. */
uint16_t plays_get(uint32_t key);
/** Slots in the table (PLAYS_MAX); plays_at() reads slot @p i (copied; false for a free slot). Any task. */
uint32_t plays_slots(void);
bool plays_at(uint32_t i, plays_entry_t *out);
/** True while the table is backed by the card (loaded, and the last save worked); false: RAM only. */
bool plays_persistent(void);

/** Storage task, from its idle loop: load once the card is usable, save when changed (retries every PLAYS_RETRY_MS after a failure). */
void plays_poll(uint32_t now_ms);

/* file access, for tests */
bool plays_save_file(const char *path);  /**< write the whole table to @p path (no temp file, no scope) */
bool plays_load_file(const char *path);  /**< replace the table from @p path; false (table untouched) when missing or not valid */
void plays_reset(void);                  /**< forget everything (tests) */

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PLAYS_STORE_H

// SPDX-License-Identifier: MPL-2.0
/**
 * @file ratings_store.h
 * @brief Star ratings of tracks (Now Playing page 2), kept on the card in /.neupod/ratings.bin.
 *
 * A rating is keyed by the file's path AND size (ratings_key), not by a library id, so a rebuilt library index keeps every rating. The table
 * (RATINGS_MAX tracks, the least recently rated is dropped when full) lives in RAM and is always usable; ratings_poll() (storage task only) loads the
 * file once the settings store is attached to the card and writes it after a change, under the same nfs write scope and write-lock discipline as the
 * settings store (confined to /.neupod). On a write-protected card, or while the card is not usable, ratings stay in RAM only
 * (ratings_persistent() says so) and are written as soon as the card allows. File: "NPRT", version, count, entries (key u32, stars u8), CRC-32;
 * saved in place, so a power cut during a save can lose the ratings (a bad CRC loads as "none"), never anything else.
 */
#ifndef NEUPOD_RATINGS_STORE_H
#define NEUPOD_RATINGS_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RATINGS_MAX 128u
#define RATINGS_FILE "/.neupod/ratings.bin"

/** Key of the file at @p path of @p size bytes (never 0). */
uint32_t ratings_key(const char *path, uint64_t size);
/** Stars 0..5 (0 = not rated). Any task. */
uint8_t ratings_get(uint32_t key);
/** Set the stars (0 clears). Any task; the card is written later by ratings_poll(). */
void ratings_set(uint32_t key, uint8_t stars);
/** True while the table is backed by the card (loaded, and the last save worked); false: RAM only. */
bool ratings_persistent(void);

/** Storage task, from its idle loop: load once the card is usable, save when changed (retries every RATINGS_RETRY_MS after a failure). */
#define RATINGS_RETRY_MS 30000u
void ratings_poll(uint32_t now_ms);

/* the file image, for tests */
size_t ratings_serialise(uint8_t *out, size_t cap);
bool ratings_parse(const uint8_t *in, size_t n); /**< replaces the table; false (table untouched) when the image is not valid */
void ratings_reset(void);                        /**< forget everything (tests) */

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_RATINGS_STORE_H

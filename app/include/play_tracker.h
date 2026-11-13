// SPDX-License-Identifier: MPL-2.0
/**
 * @file play_tracker.h
 * @brief When does a track count as played (design review LB-7: the numbers behind Most played, Unplayed, Recently played).
 *
 * A pure rule, no clock of its own and no card access (host-tested): fed the player's state at every service pass, it adds up the time the track was
 * actually PLAYING (a seek or a scan does not move it forward) and says "count it" once per track start, when
 *  - at least half of the track's length has been heard, or
 *  - the track's length is not known and 30 s have been heard, or
 *  - the track ended by itself after at least 3 s.
 * A new track id starts a new count; pausing stops the clock; a gap between two passes longer than a second (the task was held up) adds only a second.
 */
#ifndef NEUPOD_PLAY_TRACKER_H
#define NEUPOD_PLAY_TRACKER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PTRK_UNKNOWN_LEN_MS 30000u /**< heard time that counts a track whose length is not known */
#define PTRK_MIN_END_MS 3000u      /**< a track that ended by itself counts after this much */
#define PTRK_MAX_STEP_MS 1000u     /**< most one pass adds */

typedef struct {
    bool have;          /**< a track is being followed */
    uint32_t track_id;
    uint32_t played_ms; /**< time heard so far */
    uint32_t last_ms;   /**< time of the previous pass */
    bool counted;
} ptrk_t;

void ptrk_init(ptrk_t *t);
/**
 * One pass at @p now_ms. @p playing: the player is PLAYING a file (not paused, not a test tone); @p ended: the player says the file ran to its end;
 * @p track_id identifies the track start (player_info_t.track_id); @p duration_ms its length (0 = unknown). True exactly once per track start, at the
 * pass where it becomes a counted play.
 */
bool ptrk_update(ptrk_t *t, uint32_t now_ms, bool playing, bool ended, uint32_t track_id, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PLAY_TRACKER_H

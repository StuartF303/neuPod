// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_control.h
 * @brief What the headset adds to the player (stage 2c, AVRCP): its volume, its buttons, and the track information it shows (ADR-0019, owner decision 4).
 *
 * Volume. With a headset that has AVRCP absolute volume (hal_bt_volume_supported()) on the Bluetooth output, the volume of the player IS the headset's
 * volume: the wheel sets it with hal_bt_volume_set(), the headset's own changes (a swipe on the stem) move the bar, and the software volume is held at
 * 0 dB so that nothing is attenuated twice. Without absolute volume, or on the wired output, the software volume is the volume, as before. One scale
 * ties the two together (bt_vol_to_db / bt_vol_from_db): linear in dB between 0 dB (127) and the software "off" level (0), so a wheel step is 2 dB in
 * either mode and the bar means the same thing. Nothing is remembered per headset: the headset keeps its own volume and is read on connect, and a
 * volume is never pushed to a headset behind the user's back.
 *
 * Keys. A headset button (HAL_BT_EVT_KEY) does what the matching button of the device does on every screen: Play / Pause, Right (next track), Left
 * (previous track); the held FF / RW buttons scrub the track.
 *
 * Track information. The title, artist, album, length, position and play status are handed to the stack (hal_bt_track_info()) while a headset is connected.
 *
 * The functions marked "pure" have no state and no OS; they are what test_bt_control checks.
 */
#ifndef NEUPOD_BT_CONTROL_H
#define NEUPOD_BT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_radio.h"
#include "player.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the volume scale (pure) ---- */

#define BT_VOL_STEP 4u /**< one wheel step on a headset, in 0..127 units (about 3 %, 2 dB on the scale below: 32 steps from silent to full) */

/** Headset volume 0..127 for a software attenuation in dB: 0 dB = 127, AUDIO_VOL_OFF_DB and above = 0, linear in dB in between (rounded to nearest). */
uint8_t bt_vol_from_db(uint32_t att_db);
/** The attenuation in dB for a headset volume (the inverse; 127 = 0 dB, 0 = AUDIO_VOL_OFF_DB). */
uint32_t bt_vol_to_db(uint8_t volume);
/** @p steps wheel steps (positive = louder) from @p volume, clamped to 0..127. */
uint8_t bt_vol_step(uint8_t volume, int32_t steps);
/** 0..100 for the screen. */
unsigned bt_vol_percent(uint8_t volume);

/* ---- the headset's buttons (pure) ---- */

typedef enum {
    BT_KEY_NONE = 0,
    BT_KEY_TOGGLE, /**< the Play / Pause button */
    BT_KEY_NEXT,   /**< Right: next track of the queue */
    BT_KEY_PREV,   /**< Left: previous track (or the start of this one) */
    BT_KEY_FWD,    /**< fast forward: scrub forward */
    BT_KEY_BACK,   /**< rewind: scrub back */
} bt_key_action_t;

/**
 * The player action for a headset button in player state @p st. PLAY only acts when the player is not playing, PAUSE and STOP (which pauses: the place
 * is kept, a later PLAY resumes) only when it is playing, so a button that arrives twice or in the wrong order does no harm; PLAY_PAUSE toggles.
 * NEXT / PREV need a folder queue (@p queue). FF / RW need a track that is playing or paused.
 */
bt_key_action_t bt_key_action(hal_bt_key_t key, player_state_t st, bool queue);
/** A folder queue is playing, opening or paused (the same test the Left / Right keys use). */
bool bt_queue_active(const player_info_t *pi);
/** Seconds one FF / RW press moves (the Now Playing scrub step for a track of @p total_s seconds). */
uint32_t bt_scrub_step_s(uint64_t total_s);

/* ---- track information (pure) ---- */

/** Fill @p out from the player: false when there is nothing to tell (no file: idle, a test tone). */
bool bt_meta_from_player(const player_info_t *pi, hal_bt_track_t *out);
/**
 * Must the headset be told again? Yes when the track or its play status changed, when the position is away from where the headset's own clock has
 * it by more than 2 s (a seek), or after 10 s (so a clock that drifted is put right). @p sent is the last information given, @p age_ms how long ago.
 */
bool bt_meta_changed(const hal_bt_track_t *sent, uint32_t age_ms, const hal_bt_track_t *now);

/* ---- the app side ---- */

/** The headset's volume is the player's volume: Bluetooth is the output and the headset has absolute volume. */
bool bt_volume_absolute(void);
/** 0..127 while bt_volume_absolute(), else -1. */
int bt_volume_get(void);
/** The wheel: @p steps (positive = louder) on the headset's volume, or on the software volume when there is no absolute volume. */
void bt_volume_step(int32_t steps);

/** A radio event (HAL_BT_EVT_KEY is queued for bt_control_service()). Any task; never blocks; called by bt_service. */
void bt_control_on_event(const hal_bt_event_t *ev);
/** Every 100 ms from the monitor task: hold the software volume at 0 dB while the headset's volume is the volume, act on headset buttons, keep the headset's track information current. */
void bt_control_service(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_CONTROL_H

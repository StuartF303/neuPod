// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_reconnect.h
 * @brief Automatic reconnection to the remembered headset (ADR-0019 stage 3): after a lost link, and once at power-up.
 *
 * Pure policy, no OS and no radio: bt_service.c feeds it the facts and makes the Connect call it asks for. An attempt is ended by the HAL's own
 * connect timer (HAL_BT_CONNECT_TIMEOUT_MS), so the policy only schedules the next one. Schedule after a loss: 2 s, 5 s, 10 s, then every 30 s,
 * for 5 minutes in all. At power-up the first attempt is at once, then the same schedule. Any user action (Disconnect, Forget, Pair, Connect,
 * Cancel) ends it for good; a scan or a Pair / Connect of the user only holds it back. Playback is NOT resumed by a reconnect: the pause stays.
 *
 * Reconnect-on-play: when the user starts playback on the Bluetooth output with the headset away, bt_rc_play() runs a short schedule of its own: the first
 * attempt at once, at most BT_RC_PLAY_ATTEMPTS attempts (the second 2 s after the first has failed), then it ends like any other give-up. If an automatic
 * schedule (loss, power-up) is already running the play request joins it and changes nothing. No background retry follows: the user asks again with Play.
 */
#ifndef NEUPOD_BT_RECONNECT_H
#define NEUPOD_BT_RECONNECT_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_RC_GIVE_UP_MS 300000u /**< no more attempts this long after the loss (or the power-up) */
#define BT_RC_PLAY_ATTEMPTS 2u   /**< attempts of a reconnect-on-play schedule (each is bounded by the HAL's own connect timer) */

/** Wait before attempt number @p failed + 1 after @p failed attempts have failed (0 -> 2 s, 1 -> 5 s, 2 -> 10 s, then 30 s). */
uint32_t bt_rc_delay_ms(uint32_t failed);

typedef struct {
    bool armed;      /**< reconnecting: waiting for the next attempt, or one is running */
    bool attempting; /**< we made a Connect call and its outcome is not in yet */
    uint8_t max_attempts; /**< 0: no cap (the 5 minute schedules); else the attempts after which it gives up (reconnect-on-play) */
    uint32_t failed;
    uint32_t start_ms, due_ms;
} bt_rc_t;

typedef struct {
    bool bt_on;      /**< hal_bt_state() is ON (not scanning) */
    bool have_saved; /**< a headset is remembered */
    hal_bt_link_state_t link;
} bt_rc_in_t;

void bt_rc_init(bt_rc_t *rc);
/** The link to the remembered headset was lost (HAL_BT_FAIL_LINK_LOST): start with attempt 1 after 2 s. */
void bt_rc_lost(bt_rc_t *rc, uint32_t now_ms);
/** Power-up with a remembered headset: first attempt now. */
void bt_rc_powerup(bt_rc_t *rc, uint32_t now_ms);
/** Play was pressed with the headset away: first attempt now, BT_RC_PLAY_ATTEMPTS at most. Does nothing while a schedule is already armed (it joins that one). */
void bt_rc_play(bt_rc_t *rc, uint32_t now_ms);
/** The user took over (Disconnect, Forget, Pair, Connect, Cancel): stop for good. */
void bt_rc_user_action(bt_rc_t *rc);
/** Drive it (every tick). true: make the Connect call now (then bt_rc_attempt_made(), or bt_rc_attempt_refused() if the call failed). */
bool bt_rc_step(bt_rc_t *rc, uint32_t now_ms, const bt_rc_in_t *in);
void bt_rc_attempt_made(bt_rc_t *rc);
void bt_rc_attempt_refused(bt_rc_t *rc, uint32_t now_ms);
/** "Reconnecting..." is on screen: armed (also while an attempt runs). */
bool bt_rc_active(const bt_rc_t *rc);

/** What bt_rc_service() saw end. */
typedef enum {
    BT_RC_ENDED_NONE = 0,
    BT_RC_ENDED_AWAY, /**< a schedule ended with the headset still away (gave up, attempts used up) */
    BT_RC_ENDED_BACK, /**< a schedule ended because the link is up again */
} bt_rc_end_t;

/** The Connect call of bt_rc_service(): true when the radio took the request. */
typedef bool (*bt_rc_connect_fn)(void *ctx);

/**
 * One tick of the policy, for bt_service.c (and its test): takes in a Play's request (@p req_seq against *@p seen: a running schedule is joined, else bt_rc_play() starts
 * one), ends a play schedule (rc->max_attempts != 0) once nothing is waiting any more (@p holding false; a loss / power-up schedule, max_attempts 0, is never ended
 * that way, also when it replaced a play schedule), steps the policy, makes the Connect call through @p connect, and reports how a schedule ended.
 */
bt_rc_end_t bt_rc_service(bt_rc_t *rc, uint32_t now_ms, const bt_rc_in_t *in, bool holding, uint32_t req_seq, uint32_t *seen, bt_rc_connect_fn connect, void *ctx);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_RECONNECT_H

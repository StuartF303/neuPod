// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_link_policy.h
 * @brief Link robustness policy of the A2DP encoder (docs/bluetooth.md, "Link robustness"): pure functions and a few bytes of state, no BTstack,
 *        no RTOS, no I/O, so it is unit-tested on the host (tests/test_bt_link_policy.c) and costs nothing but its code on the RP2350.
 *
 * Two things live here. The catch-up budget: how much audio the encoder may owe the headset after the link stalled (it is sent as fast as the
 * link takes it, so the headset's buffer fills again; only what is beyond the budget is dropped). And the bitpool ladder (53 -> 45 -> 35, within what the
 * headset allows; SBC frames carry their own bitpool, so the headset needs no reconfiguration). The ladder steps DOWN FAST and UP SLOWLY: a stall that
 * has outrun the catch-up budget has already lost audio, so waiting to be sure costs more than a lower bitrate for a while does.
 *   Down, at once: a severe stall (owed >= 90 ms or one packet waiting >= 120 ms) goes straight to the last level; the first tick of any stall
 *   episode (owed crossing 40 ms) steps one level; one bad 1 s window (>= 25 % pressured ticks) steps one level. Single steps are at least one settle
 *   time apart, and the first second of a stream is exempt from the tick-level triggers (the start-up burst is owed on purpose).
 *   Up, one level at a time, only after a long clean spell (no stall, windows under 5 % pressured): 30 s, doubling to 240 s for every step down soon
 *   after a step up, so a bad patch does not make the bitpool oscillate.
 *
 * Every constant of the behaviour is in this header.
 */
#ifndef NEUPOD_BT_LINK_POLICY_H
#define NEUPOD_BT_LINK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Catch-up ---- */
#define BT_LINK_CATCHUP_MS 160u /**< the most audio the encoder may owe after a stall: made up at the link's speed, the rest dropped (counted) */
#define BT_LINK_STALL_MS 40u    /**< owing more than this is a stall (the old hard cap; counted once per episode, and the start-up burst bound) */

/* ---- Backpressure and the bitpool ladder ---- */
#define BT_LINK_PRESSURE_WAIT_MS 30u  /**< a finished packet that has waited this long for the link is pressure ... */
#define BT_LINK_PRESSURE_OWED_MS 50u  /**< ... and so is owing this much audio */
#define BT_LINK_WINDOW_MS 1000u       /**< the policy judges the link in windows of this length */
#define BT_LINK_BAD_PERCENT 25u       /**< a window with at least this share of pressured ticks is bad: one bad window steps the bitpool down */
#define BT_LINK_CLEAN_PERCENT 5u      /**< a window with less than this share is clean (in between: neither; it restarts the clean spell) */
#define BT_LINK_SEVERE_OWED_MS 90u    /**< owing this much goes straight to the last level: the Bluetooth ring holds 93 ms, so the stall has outrun catch-up */
#define BT_LINK_SEVERE_WAIT_MS 120u   /**< ... and so does one packet waiting this long for the link (a stall of the same size) */
#define BT_LINK_DOWN_SETTLE_MS 1000u  /**< single steps down (stall start, bad window) are at least this far apart; a severe stall ignores it */
#define BT_LINK_START_GRACE_MS 1000u  /**< after init the tick-level triggers (severe, stall start) are ignored: the start-up burst is owed on purpose */
#define BT_LINK_UP_HOLD_MS 30000u     /**< clean time before stepping back up (each level needs its own full hold) */
#define BT_LINK_UP_HOLD_MAX_MS 240000u /**< a step down soon after a step up doubles the hold (up to this): no flapping in a bad patch */
#define BT_LINK_QUICK_DOWN_MS 120000u /**< "soon after" */
#define BT_LINK_BITPOOL_STEP1 45u
#define BT_LINK_BITPOOL_STEP2 35u
#define BT_LINK_LEVELS 3u             /**< at most this many bitpools (the ladder: ceiling, STEP1, STEP2) */

/** Audio owed after adding the elapsed time, bounded by the catch-up budget: @p owed (samples per channel) is clamped to BT_LINK_CATCHUP_MS at
 * @p rate Hz; the part beyond it goes to *dropped. Returns the clamped value. */
uint32_t bt_link_owed_clamp(uint32_t owed, uint32_t rate, uint32_t *dropped);

/** One tick's verdict: the packet waiting for the link for @p wait_ms (0 if none) or @p owed_ms of audio owed. */
bool bt_link_pressured(uint32_t wait_ms, uint32_t owed_ms);

/** The bitpool of ladder @p level (0 = best) for a headset allowing @p min_bp .. and a ceiling of @p ceiling (the bitpool a stream starts at). The ladder is
 * the ceiling, then 45, then 35, each at most the ceiling and at least @p min_bp; steps that coincide are one. Levels past the last repeat it. */
uint8_t bt_link_bitpool(uint8_t ceiling, uint8_t min_bp, uint8_t level);
/** Number of distinct levels (1 .. BT_LINK_LEVELS). */
uint8_t bt_link_levels(uint8_t ceiling, uint8_t min_bp);

typedef struct {
    uint8_t level;          /**< current ladder level */
    bool in_stall;          /**< the last tick owed >= BT_LINK_STALL_MS (an episode is in progress) */
    bool has_up;            /**< last_up is valid */
    uint16_t ticks;         /**< this window */
    uint16_t pressured;     /**< this window */
    uint32_t window_start;  /**< ms */
    uint32_t start_ms;      /**< ms of init (the start grace) */
    uint32_t clean_since;   /**< ms: when the current clean spell began */
    uint32_t last_up;       /**< ms of the last step up (valid if has_up) */
    uint32_t last_down;     /**< ms of the last step down (init: a settle time before start, so the first is allowed) */
    uint32_t hold_ms;       /**< the current hold before stepping up */
} bt_link_policy_t;

void bt_link_policy_init(bt_link_policy_t *p, uint32_t now_ms);

/** Feed one pacing tick: @p wait_ms how long the finished packet has waited for can-send-now (0 if none), @p owed_ms the audio owed after the catch-up
 * clamp. Returns the bitpool to switch to, or 0 for no change. @p ceiling / @p min_bp as bt_link_bitpool(). */
uint8_t bt_link_policy_tick(bt_link_policy_t *p, uint32_t now_ms, uint32_t wait_ms, uint32_t owed_ms, uint8_t ceiling, uint8_t min_bp);

/* ---- The MT6582's phantom link ---- */

/** After a Create_Connection that timed out (and was cancelled) the MT6582 refuses every later one to that address with 0x0B (ACL connection
 * already exists) until it is reset, with no link there. True if a failed set-up step (status @p st, with @p have_acl: we hold a connection handle)
 * is that and the controller has not been reset for this attempt yet (@p already_reset): reset it and try the same step once more. */
static inline bool bt_link_phantom_acl(uint8_t st, bool have_acl, bool already_reset) {
    return st == 0x0Bu /* ERROR_CODE_ACL_CONNECTION_ALREADY_EXISTS */ && !have_acl && !already_reset;
}

/** Where a controller reset for a phantom link is. */
enum { PH_NONE = 0, PH_OFF /* power-off not issued yet */, PH_DOWN /* power-off issued, waiting for HCI_STATE_OFF */, PH_ON /* power-on issued or pending */ };
/** The attempt ended (cancel, Forget, timeout) while a reset was at @p phase. Before the power-off was issued nothing has happened to the controller, so the reset is
 * simply dropped. Afterwards it is never cancelled half-way (HCI would be left off, or the state wrong): it runs to the end and only its retry is abandoned. */
static inline bool bt_link_phantom_abandon_only(uint8_t phase) { return phase == PH_DOWN || phase == PH_ON; }

#define BT_LINK_PHANTOM_REPEAT_MS 60000u /**< at most one controller reset per peer in this time (unless a link to it came up) */
#define BT_LINK_PHANTOM_BOUND_MS 15000u  /**< a reset that has not brought the controller back by now ends the attempt as a failed start */

/** True if the last reset (at @p last_ms) is far enough back for another (wrap-safe). */
static inline bool bt_link_phantom_due(uint32_t now_ms, uint32_t last_ms) { return (uint32_t)(now_ms - last_ms) >= BT_LINK_PHANTOM_REPEAT_MS; }

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_LINK_POLICY_H

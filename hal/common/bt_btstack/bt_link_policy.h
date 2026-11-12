// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_link_policy.h
 * @brief Link robustness policy of the A2DP encoder (docs/bluetooth.md, "Link robustness"): pure functions and a few bytes of state, no BTstack,
 *        no RTOS, no I/O, so it is unit-tested on the host (tests/test_bt_link_policy.c) and costs nothing but its code on the RP2350.
 *
 * Two things live here. The catch-up budget: how much audio the encoder may owe the headset after the link stalled (it is sent as fast as the
 * link takes it, so the headset's buffer fills again; only what is beyond the budget is dropped). And the bitpool ladder: on sustained backpressure
 * the SBC bitpool steps down (53 -> 45 -> 35, within what the headset allows) and back up after a clean spell. SBC frames carry their own bitpool, so
 * the headset needs no reconfiguration.
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
#define BT_LINK_BAD_PERCENT 25u       /**< a window with at least this share of pressured ticks is bad */
#define BT_LINK_CLEAN_PERCENT 5u      /**< a window with less than this share is clean (in between: neither) */
#define BT_LINK_DOWN_WINDOWS 2u       /**< consecutive bad windows that step the bitpool down: pressure must be sustained, not one stall */
#define BT_LINK_UP_HOLD_MS 15000u     /**< clean time before stepping back up */
#define BT_LINK_UP_HOLD_MAX_MS 60000u /**< a step down soon after a step up doubles the hold (up to this): no flapping in a bad patch */
#define BT_LINK_QUICK_DOWN_MS 60000u  /**< "soon after" */
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
    uint8_t bad_windows;    /**< consecutive bad windows */
    uint16_t ticks;         /**< this window */
    uint16_t pressured;     /**< this window */
    uint32_t window_start;  /**< ms */
    uint32_t clean_since;   /**< ms: when the current clean spell began */
    uint32_t last_up;       /**< ms of the last step up (valid if has_up) */
    uint32_t hold_ms;       /**< the current hold before stepping up */
    bool has_up;
} bt_link_policy_t;

void bt_link_policy_init(bt_link_policy_t *p, uint32_t now_ms);

/** Feed one pacing tick. Returns the bitpool to switch to, or 0 for no change. @p ceiling / @p min_bp as bt_link_bitpool(). */
uint8_t bt_link_policy_tick(bt_link_policy_t *p, uint32_t now_ms, bool pressured, uint8_t ceiling, uint8_t min_bp);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_LINK_POLICY_H

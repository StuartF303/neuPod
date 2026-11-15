// SPDX-License-Identifier: MPL-2.0
/**
 * @file power_policy.h
 * @brief When the player sleeps, and when it switches itself off because the battery is empty (ADR-0021). Pure: no OS, no HAL, no allocation.
 *
 * The owner's rules (15 Nov 2026): the player may sit on a shelf for a week and still power up, and nobody has to remember to switch it off.
 *   - After the "Sleep after" time with nothing happening it goes to SLEEP (suspend to RAM where the platform can, else a power-off).
 *   - "Nothing happening" means: no key, wheel turn or touch (the one that wakes the screen counts), no audio playing (a paused player is idle), and
 *     nothing busy (the music library building, a Bluetooth pairing or connect, a file or settings write). While audio plays the idle clock keeps
 *     being restarted, so the time is counted from the moment it stopped.
 *   - Never on external power (USB, charger): the clock is held at zero and the battery rule is off.
 *   - Off external power a battery at or below POWER_CRIT_PCT (or below POWER_CRIT_MV), read as valid, for POWER_CRIT_HOLD_MS in a row is
 *     POWER_ACT_POWEROFF_CRITICAL: the caller shows "Battery empty" and takes the normal power-off path.
 * The caller owns what the actions mean (the sleep sequence, the notice); this only decides. It must be called regularly (the UI tick, about every 30 ms).
 *
 * Clocks: all times are 32-bit millisecond counters that wrap (about 49 days); every comparison is a signed difference. An activity stamp a little
 * ahead of now (up to POWER_FUTURE_MS: the host tour spaces its keys ahead of the tick clock) is not idle yet; a clock that jumped further back restarts
 * the count instead of sleeping on the spot or never.
 */
#ifndef NEUPOD_POWER_POLICY_H
#define NEUPOD_POWER_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_power.h" /* NEUPOD_POWER_MGMT */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the "Sleep after" choice (Settings > Display) ---- */

#define POWER_SLEEP_STEPS 4u       /**< entries of the choice: Never, 10 min, 30 min, 60 min */
#define POWER_SLEEP_DEFAULT_IDX 2u /**< 30 min (owner decision, 15 Nov 2026) */

/** Seconds of the "Sleep after" choice @p idx (0 = Never); 0 if out of range. */
uint32_t power_sleep_seconds(uint8_t idx);
/** Label of the choice: "Never", "10 min", "30 min", "60 min"; "-" if out of range. */
const char *power_sleep_name(uint8_t idx);

/* ---- thresholds ---- */

#define POWER_OFF_AFTER_SLEEP_MS (24u * 60u * 60u * 1000u) /**< asleep this long: switch off fully (the resume position is saved across boots) */
#define POWER_CRIT_PCT 3u                                  /**< battery at or below this is empty */
#ifndef POWER_CRIT_MV
#define POWER_CRIT_MV 3300u                                /**< ... or a cell below this (0 = not checked), for a gauge whose percentage lags */
#endif
#define POWER_CRIT_HOLD_MS 30000u                          /**< ... for this long without a break */
#define POWER_FUTURE_MS 120000u                            /**< how far ahead of now an activity stamp may be and still count as "just now" */

/* ---- the policy ---- */

typedef enum {
    POWER_ACT_NONE = 0,
    POWER_ACT_SLEEP,             /**< idle for the whole "Sleep after" time: go to sleep now */
    POWER_ACT_POWEROFF_CRITICAL, /**< battery empty for 30 s off external power: show the notice, then power off */
} power_action_t;

typedef struct {
    uint32_t now_ms;           /**< the tick clock */
    uint32_t last_activity_ms; /**< the last key, wheel turn or touch (the stamp of the input that woke the screen counts) */
    uint32_t sleep_after_s;    /**< the setting in seconds, 0 = Never */
    bool audio_playing;        /**< audio is playing (not paused, not stopped) */
    bool busy;                 /**< something that must not be cut off: library build or scan, Bluetooth pair / connect, a file or settings write */
    bool external_power;       /**< USB or a charger is attached */
    bool battery_valid;        /**< the gauge was read and a battery is present: false never counts as empty */
    uint8_t battery_pct;       /**< 0..100 */
    uint16_t battery_mv;       /**< cell voltage, 0 = unknown */
} power_inputs_t;

typedef struct {
    uint32_t ref_ms;        /**< idle is counted from here: the latest of the last activity and the last time something held the player awake */
    uint32_t crit_since_ms; /**< the battery rule's streak began here (valid while crit) */
    bool crit;              /**< the battery has been empty since crit_since_ms */
} power_policy_t;

/** Start the idle clock at @p now_ms (not at 0: a long boot must not count as idle). */
void power_policy_init(power_policy_t *p, uint32_t now_ms);
/** Start counting again from @p now_ms (after a wake, or after a sleep that could not be done); the battery streak is kept. */
void power_policy_restart(power_policy_t *p, uint32_t now_ms);
/** Decide. POWEROFF_CRITICAL wins over SLEEP. SLEEP / POWEROFF_CRITICAL repeat on every call until the caller restarts the policy or the cause ends. */
power_action_t power_policy_tick(power_policy_t *p, const power_inputs_t *in);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_POWER_POLICY_H

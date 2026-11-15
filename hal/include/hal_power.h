// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_power.h
 * @brief Low-power control: external power and system sleep (ADR-0021). (Battery state lives in hal_battery.h; switching off in hal_system.h.)
 *
 * Sleep here means the whole system is suspended with its memory kept (Y2: Linux suspend-to-RAM), so nothing of the firmware needs to start again
 * on wake: the call blocks, and returns when something woke the system. Everything that must be safe before the system stops (audio, Bluetooth,
 * saved settings, the display) is the CORE's job and is done before the call; the HAL only suspends, waits and reports what woke it.
 * Platforms that cannot sleep answer hal_power_can_sleep() false; the core then switches the player off after the idle time where the platform
 * can (hal_system_can_poweroff()), and does nothing otherwise.
 */
#ifndef NEUPOD_HAL_POWER_H
#define NEUPOD_HAL_POWER_H

#include "hal_types.h"

/**
 * 1: power management is built in (the sleep setting, the policy tick, the sleep sequence). 0: compiled out, for the Pico 2 W bench rig, which is frozen
 * with about 100 B of static RAM to spare and can neither sleep nor switch itself off (set by the top-level CMakeLists.txt for that board only).
 */
#ifndef NEUPOD_POWER_MGMT
#define NEUPOD_POWER_MGMT 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Battery state / fuel gauge: see hal_battery.h (MAX17048). */

/** Why hal_power_sleep() returned. */
typedef enum {
    HAL_WAKE_NONE = 0,       /**< did not sleep (the call failed) */
    HAL_WAKE_KEY,            /**< a key, the wheel or the touch panel: the input that woke it is still queued for the input task to read */
    HAL_WAKE_TIMER,          /**< @p max_ms went by: the core switches the player off */
    HAL_WAKE_EXTERNAL_POWER, /**< a charger or USB was plugged in */
    HAL_WAKE_OTHER,          /**< anything else (a radio, a spurious wake-up) */
} hal_wake_reason_t;

/** Initialise power management. Call once at start-up, before the first hal_power_can_sleep() / hal_power_sleep(); safe to call again. */
hal_status_t hal_power_init(void);

/**
 * Can this platform sleep right now? false where there is no suspend, where it is switched off (Y2: NEUPOD_SUSPEND=0 in /etc/neupod-fw.env, or the
 * kernel offers no way to suspend), or where it failed before. Cheap; the core asks before it prepares anything.
 */
bool hal_power_can_sleep(void);

/**
 * Suspend the system and block until it wakes, or until @p max_ms of sleep have passed (counted by a clock that keeps running while the system
 * sleeps). @p why (may be NULL) is set to the reason. Returns:
 *   HAL_OK       it slept and woke; *why says why (HAL_WAKE_TIMER when @p max_ms went by).
 *   HAL_ENOTSUP  the platform cannot sleep (hal_power_can_sleep() is false); nothing happened.
 *   HAL_EBUSY    the system would not go to sleep (something held it awake) or a wake came before it did; it is awake and nothing was lost.
 *   HAL_ERR      the suspend machinery failed; it is awake.
 * The callers' clocks: hal_millis() / hal_micros() may or may not count the time asleep; the core never relies on either, and tolerates a jump.
 * Called from one task only (the UI task); not from the audio core or an interrupt.
 */
hal_status_t hal_power_sleep(uint32_t max_ms, hal_wake_reason_t *why);

/** Is external power (USB, a charger) attached? false where the platform cannot tell. The core never sleeps while this is true. */
bool hal_power_external(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_POWER_H

// SPDX-License-Identifier: MPL-2.0
/**
 * @file power_service.h
 * @brief System sleep and the empty-battery power-off for the app (ADR-0021): gathers what the policy (power_policy.h) needs, and does the parts of the
 *        sleep sequence that are not the screen's. The UI task owns the whole thing (it owns the idle clock, Hold and the display): it calls
 *        power_service_poll() every tick and, on SLEEP, runs  prepare -> screen off -> block -> wake  (ui_frame.c).
 *
 * The sleep sequence, in order, all on the UI task (the only task that waits; the others keep running until the system suspends):
 *   1. power_service_sleep_prepare(): re-check that nothing plays; stop the Bluetooth stream and power the Bluetooth controller off (remembering whether a
 *      headset was linked); write the settings and the saved resume position to the card and sync it, waiting (bounded) for the storage task to be idle.
 *      false = something started meanwhile or the card stayed busy: nothing is left half done that needs undoing, the screen has not been touched.
 *   2. the UI turns the screen off and puts the panel to sleep.
 *   3. power_service_sleep_block(): hal_power_sleep(POWER_OFF_AFTER_SLEEP_MS).
 *   4. power_service_wake(): the Bluetooth controller back on if it was, the policy restarted. The UI restores the display and decides what the waking
 *      input does (it is not delivered).
 * Wired audio needs nothing here: the output stages power themselves down a few seconds after the last sound (docs/audio.md), long before the shortest
 * "Sleep after" (10 min), and the policy never sleeps while anything plays.
 */
#ifndef NEUPOD_POWER_SERVICE_H
#define NEUPOD_POWER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_power.h"
#include "power_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#if NEUPOD_POWER_MGMT

/** Can the player do something about being idle: sleep (hal_power_can_sleep()) or switch itself off (hal_system_can_poweroff())? */
bool power_service_can_act(void);

/**
 * Gather the inputs and run the policy (at most every 500 ms; between times it repeats POWER_ACT_POWEROFF_CRITICAL while that holds and otherwise returns POWER_ACT_NONE, so a sleep is asked for once). @p last_activity_ms is the UI's last input;
 * @p sleep_after_s the setting (0 = Never). SLEEP is returned only where power_service_can_act(); where it cannot act the idle count simply restarts.
 */
power_action_t power_service_poll(uint32_t now_ms, uint32_t last_activity_ms, uint32_t sleep_after_s);

/** Start the idle count again from @p now_ms (a sleep was refused, or the system woke). */
void power_service_restart(uint32_t now_ms);

/** Step 1 of the sleep sequence, see above. */
bool power_service_sleep_prepare(void);

/** Step 3: suspend and block (hal_power_sleep(POWER_OFF_AFTER_SLEEP_MS)). Same return codes; @p why is set on HAL_OK. */
hal_status_t power_service_sleep_block(hal_wake_reason_t *why);

/** Step 4 (non-UI part). */
void power_service_wake(uint32_t now_ms);

/** The normal power-off path: write the settings out (bounded wait), then hal_system_poweroff(). False if the platform cannot switch off. Returns at once. */
bool power_service_poweroff(void);

#endif // NEUPOD_POWER_MGMT

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_POWER_SERVICE_H

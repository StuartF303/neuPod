// SPDX-License-Identifier: MPL-2.0
/**
 * @file boot.h
 * @brief Boot-time hardware self-checks, run concurrently while the splash screen is up.
 *
 * Every check runs in its own short-lived task (core 0, below the UI) and reports through
 * caps_resolve(). Nothing here may hang the boot: each check has a deadline, and boot_poll()
 * resolves any check that missed it as DEGRADED. Checks are READ-ONLY: the microSD card is only
 * identified and mounted (writes stay locked in hal_storage), the haptic driver is left in
 * standby, and the click wheel and radio are only probed.
 */
#ifndef NEUPOD_BOOT_H
#define NEUPOD_BOOT_H

#include <stdbool.h>

#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the I2C bus (400 kHz) and spawn the check tasks. Call once, from a task, after caps_init(). */
void boot_start(void);

/**
 * Apply the deadlines (marks overdue checks DEGRADED). Call regularly from the UI task.
 * Returns true once every capability is resolved.
 */
bool boot_poll(void);

/**
 * Y2 only, and only with rt_config()->storage_hotplug. Call regularly (the monitor does, every 100 ms; it rate-limits itself): when storage is
 * unavailable and hal_storage_card_present() says a card has appeared, re-runs the storage check in its own task, which upgrades
 * the capability to OK and loads the settings. A card that fails the check is not retried until it has been removed and inserted again.
 */
#if NEUPOD_RT_HOTPLUG
void boot_storage_hotplug_poll(void);
#else
static inline void boot_storage_hotplug_poll(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BOOT_H

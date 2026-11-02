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

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BOOT_H

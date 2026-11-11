// SPDX-License-Identifier: MPL-2.0
/**
 * @file charging.h
 * @brief Charging-only mode of the Y2 firmware: entered from main() when the bootloader reports a power-off-charging boot.
 */
#ifndef NEUPOD_Y2_CHARGING_H
#define NEUPOD_Y2_CHARGING_H

#ifdef __cplusplus
extern "C" {
#endif

/** Exit status that tells the S91neupod-fw wrapper not to respawn (reboot/poweroff was requested). Same as poweroff. */
#define Y2_EXIT_REQUESTED 77

/**
 * Run charging-only mode until Power is held 2 s (spawns /sbin/reboot, returns 77), the charger has been gone for 3 s
 * (spawns /sbin/poweroff, returns 77) or SIGTERM arrives (returns 0). Starts no UI, storage, audio or RTOS. Keeps the
 * backlight off and the framebuffer black, polls every 250 ms. If a command cannot be started it logs and keeps waiting.
 */
int y2_charging_main(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_CHARGING_H

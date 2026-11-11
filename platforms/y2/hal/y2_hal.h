// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_hal.h
 * @brief Y2-only extras that are not part of the portable HAL (used by platforms/y2/main.c).
 */
#ifndef NEUPOD_Y2_HAL_H
#define NEUPOD_Y2_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Route HAL sleeps through the scheduler (the runtime installs os_delay_ms), as on the host build. */
void y2_set_delay_hook(void (*fn)(uint32_t ms));

/**
 * Receive the side-key volume steps (+ louder). Called from the input task, which is the only caller of
 * hal_input_poll(). main.c installs a function that calls player_volume_step().
 */
void y2_input_set_volume_handler(void (*fn)(int32_t steps));

/**
 * Called once from the input task when the power key has been held long enough (Y2_POWER_HOLD_MS). main.c installs a
 * function that starts the same orderly stop as SIGTERM and arranges the power-off.
 */
void y2_input_set_power_handler(void (*fn)(void));

/** Device paths. */
#define Y2_SYS_BLOCK "/sys/block"
#define Y2_DEV_DIR "/dev"
#define Y2_INPUT_CLASS "/sys/class/input"
#define Y2_DEV_INPUT "/dev/input"
#define Y2_POWER_SUPPLY "/sys/class/power_supply"
#define Y2_BACKLIGHT "/sys/class/leds/lcd-backlight/brightness"
#define Y2_FB_DEVICE "/dev/fb0"
#define Y2_KEYS_FIFO "/run/neupod.keys"

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_HAL_H

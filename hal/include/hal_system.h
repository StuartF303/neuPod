// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_system.h
 * @brief Platform bring-up, time base and status LED.
 */
#ifndef NEUPOD_HAL_SYSTEM_H
#define NEUPOD_HAL_SYSTEM_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise clocks, the debug console (stdout) and anything else every other HAL depends on.
 * Must be called once before any other HAL function.
 */
hal_status_t hal_system_init(void);

/** Milliseconds since hal_system_init(); wraps after ~49 days. */
uint32_t hal_millis(void);

/** Microseconds since hal_system_init() (64-bit, does not wrap in practice). */
uint64_t hal_micros(void);

/**
 * Smallest amount of stack (bytes) that was ever free since hal_system_init(), measured by
 * painting the unused stack at start-up; 0 where unknown (host, ESP32-S3).
 */
size_t hal_stack_free_min(void);

/** Block the calling task for at least @p ms milliseconds. */
void hal_delay_ms(uint32_t ms);

/** Drive the board status LED (no-op where the board has none). */
void hal_status_led(bool on);

/** Short platform name for logs, e.g. "rp2350", "esp32s3", "host". */
const char *hal_platform_name(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_SYSTEM_H

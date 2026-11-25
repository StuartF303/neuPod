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

/**
 * Second-stage bring-up for parts that need the RTOS scheduler running (RP2350: the Wi-Fi/BT
 * chip, which also carries the status LED, runs a worker task). Call once from a task pinned to
 * core 0 after the scheduler has started, before hal_status_led(). hal_system_init() does it
 * itself in bare-metal builds (the test executables), where this is a no-op.
 */
hal_status_t hal_system_late_init(void);

/**
 * True when the caller is in ordinary task context, where blocking RTOS calls still work (so the
 * panic path may freeze the other tasks first); false inside interrupts or kernel critical sections.
 */
bool hal_panic_can_block(void);

/**
 * Fatal-error path, never returns: write @p msg to the debug console WITHOUT any lock, heap or
 * scheduler help (it may run from an interrupt or with the kernel in an undefined state), then
 * stop the audio core (RP2350), then halt forever, flashing the status LED @p blink_code times per cycle where the platform can,
 * repeating @p msg every few seconds. Host: prints and abort()s so tests fail loudly.
 */
void hal_panic(uint8_t blink_code, const char *msg) __attribute__((noreturn));

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

/**
 * The chip's unique 64-bit ID (RP2350: the OTP chip ID read by the boot ROM before main; no flash access). false if unavailable.
 * NOT secret (docs/wifi.md): used only to bind the secrets file to one board.
 */
bool hal_chip_id(uint8_t id[8]);

/** Fill @p buf with @p n random bytes (RP2350: the SDK's get_rand_64(), which uses the chip's hardware TRNG; host: /dev/urandom). */
void hal_random(void *buf, size_t n);

/**
 * Can this platform switch itself off from software (Y2: ask init to power off; host: simulated, only logs)? The UI shows the
 * "powering off" countdown of a long Power hold only where this is true.
 */
bool hal_system_can_poweroff(void);

/**
 * Ask for an orderly power-off (Y2: stop the firmware cleanly, then run the system poweroff; exit status 77 keeps the init wrapper
 * from respawning it). Returns at once; true if the request was accepted. False where hal_system_can_poweroff() is false.
 */
bool hal_system_poweroff(void);

/** Short platform name for logs, e.g. "rp2350", "esp32s3", "host". */
const char *hal_platform_name(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_SYSTEM_H

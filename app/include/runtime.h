// SPDX-License-Identifier: MPL-2.0
/**
 * @file runtime.h
 * @brief The neuPod runtime: creates every task, runs the boot self-checks and the UI.
 *
 * Replaces the LED-blink app. Entered from each platform's main() after hal_system_init().
 */
#ifndef NEUPOD_RUNTIME_H
#define NEUPOD_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "os.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Optional hooks and limits for tests and the host simulator; all-zero for a normal run. */
typedef struct {
    /** If non-zero the monitor calls on_done() once this many milliseconds have passed. */
    uint32_t run_ms;
    /** Splash hold when a check failed, ms; 0 = RT_SPLASH_HOLD_MS. Tests shorten it. */
    uint32_t splash_hold_ms;
    /**
     * Host tour (tests): space-separated tokens, each either keys (e.g. "s", "\r") fed to the UI like a key
     * press, or "snap:NAME" which reports screen NAME through on_screen. One token per 300 ms after the home
     * screen is up; on_done is called when the script ends. NULL = interactive.
     */
    const char *ui_script;
    /** True where console keys are an input source (the UART testbed); the host uses its window. */
    bool console_keys;
    /** Called from the UI task when a named screen is fully drawn ("splash", "home"). */
    void (*on_screen)(const char *name, void *ctx);
    /** Called from the monitor task when run_ms has elapsed. */
    void (*on_done)(void *ctx);
    void *ctx;
} neupod_runtime_config_t;

/** Bring up the tasks and start the scheduler. @p cfg may be NULL. Does not return. */
void neupod_runtime_run(const neupod_runtime_config_t *cfg);

/** Hooks given to neupod_runtime_run (never NULL; zeroed if none were given). */
const neupod_runtime_config_t *rt_config(void);

/* rt_haptic_click(): see haptic_service.h. */

/**
 * The I2C bus has one user at a time: hold this around every battery/haptic transaction
 * (hal_i2c is not re-entrant). Lock with a timeout; never hold across a delay.
 */
os_mutex_t *rt_i2c_mutex(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_RUNTIME_H

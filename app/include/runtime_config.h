// SPDX-License-Identifier: MPL-2.0
/**
 * @file runtime_config.h
 * @brief The task table in one place: priorities, cores and stacks (docs/runtime.md mirrors it).
 *
 * The RTOS (FreeRTOS, single-core) owns core 0 and every task below. Core 1 is NOT an RTOS core: it
 * runs the bare-metal audio engine (app/audio/audio_core.c, ADR-0011), so there is no audio or
 * decoder task. Changing the arrangement is an architecture change, not a tuning knob.
 */
#ifndef NEUPOD_RUNTIME_CONFIG_H
#define NEUPOD_RUNTIME_CONFIG_H

#include "os.h"

/* Priorities (higher runs first; 0 is the kernel idle task; the timer service task is 6). */
#define RT_PRIO_STORAGE 5u
#define RT_PRIO_INPUT 4u
#define RT_PRIO_RADIO 3u /* the SDK's CYW43 worker; set via CYW43_TASK_PRIORITY on RP2350 */
#define RT_PRIO_UI 2u
#define RT_PRIO_MONITOR 1u
#define RT_PRIO_I2C 4u   /* I2C users run at input priority while on the bus: hal_i2c timeouts are
                            wall-clock (2 ms floor), and a UI frame at priority 2 outlasts that */
#define RT_PRIO_CHECK 1u /* boot self-check tasks: transient, below the UI so the splash animates */

/* Every RTOS task runs on core 0 (the only core the kernel owns). */
#define RT_CORE_OTHER OS_CORE_0

/* Boot splash: shown at least this long, and, if any check ends not fully correct, held this long
 * after the checks finish (any input event skips the hold). */
#define RT_SPLASH_MIN_MS 1500u
#define RT_SPLASH_HOLD_MS 8000u

/* Stacks, bytes. */
#define RT_STACK_STORAGE 4096u
#define RT_STACK_INPUT 2048u
#define RT_STACK_UI 8192u
#define RT_STACK_MONITOR 3072u
#define RT_STACK_CHECK 3072u
#define RT_STACK_CHECK_STORAGE 5120u /* FatFs mount */

#endif // NEUPOD_RUNTIME_CONFIG_H

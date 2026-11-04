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
#define RT_PRIO_RADIO 3u /* the SDK's CYW43 worker, which also runs BTstack; set via CYW43_TASK_PRIORITY on RP2350 */
#define RT_PRIO_UI 2u
#define RT_PRIO_MONITOR 1u
#define RT_PRIO_WIFI 1u  /* the Wi-Fi policy task: joins, retries, NTP; the radio worker (3) does the real work */
#define RT_PRIO_I2C 4u   /* I2C users run at input priority while on the bus: hal_i2c timeouts are
                            wall-clock (2 ms floor), and a UI frame at priority 2 outlasts that */
#define RT_PRIO_CHECK 1u /* boot self-check tasks: transient, below the UI so the splash animates */

/* Every RTOS task runs on core 0 (the only core the kernel owns). */
#define RT_CORE_OTHER OS_CORE_0

/* Boot splash: shown at least this long, and, if any check ends not fully correct, held this long
 * after the checks finish (any input event skips the hold). */
#define RT_SPLASH_MIN_MS 1500u
#define RT_SPLASH_HOLD_MS 8000u

/* UI scrolling: Rockbox-style 4 wheel counts per list step, with velocity acceleration. The wheel
 * reports counts (positions); testbed keys send +/-4 counts, i.e. one step per press. Counts seen in the
 * last UI_ACCEL_WINDOW_MS (decaying) above a threshold multiply the step rate. */
#define UI_COUNTS_PER_STEP 4
#define UI_ACCEL_WINDOW_MS 150u
#define UI_ACCEL_T1 12u /* counts in the window: 2x */
#define UI_ACCEL_T2 24u /* 4x */
#define UI_ACCEL_T3 40u /* 8x */

/* Signal meter (ui_signal_bars): 4 bars at or above the first threshold, 3 above the second, 2 above the third, else 1.
 * 4 and 3 bars are drawn green, 2 yellow, 1 red. Bluetooth inquiry RSSI reads weaker than Wi-Fi at the same distance
 * and quality, so its thresholds sit 5 dB stronger. The status-bar icon changes bars only after the signal has moved
 * UI_SIG_HYST_DB past a threshold, so it does not flicker around one. */
#define UI_SIG_WIFI_4_DBM (-60)
#define UI_SIG_WIFI_3_DBM (-70)
#define UI_SIG_WIFI_2_DBM (-80)
#define UI_SIG_BT_4_DBM (-55)
#define UI_SIG_BT_3_DBM (-65)
#define UI_SIG_BT_2_DBM (-75)
#define UI_SIG_HYST_DB 3
/* Busy indicator (ui_list item .busy): animation frame period, and how long a row action may stay busy before the screen gives up. */
#define UI_BUSY_FRAME_MS 140u
#define UI_BUSY_TIMEOUT_MS RT_WIFI_JOIN_TIMEOUT_MS

/* Click feedback: ROM effect played on every list step (3 = Strong Click 30 %, deliberately gentle). */
#define RT_HAPTIC_CLICK_EFFECT 3u
#define RT_HAPTIC_MIN_GAP_MS 35u  /* requests closer than this are dropped */
#define RT_HAPTIC_IDLE_MS 2000u   /* back to standby after this long without a click */
#define RT_PRIO_HAPTIC RT_PRIO_I2C
#define RT_STACK_HAPTIC 2048u

/* Stacks, bytes. */
#define RT_STACK_STORAGE 8192u /* file headers and tags are parsed here (about 1 KB of locals), on top of FatFs */
#define RT_STACK_INPUT 2048u
#define RT_STACK_UI 12288u
#define RT_STACK_MONITOR 3072u
#define RT_WIFI_JOIN_TIMEOUT_MS 60000u /* a join (associate + DHCP) not finished by then is abandoned and retried with backoff */
#define RT_STACK_WIFI 4096u /* sealing a PSK (AES-GCM, HKDF) on save left 664 B of 3 KB on the bench */
#define RT_STACK_CHECK 3072u
#define RT_STACK_CHECK_STORAGE 5120u /* FatFs mount */
#define RT_STACK_CHECK_RADIO 4096u   /* radio driver start-up and the Bluetooth hand-over (caps_resolve with a format) */
#define RT_STACK_RADIO 6144u         /* the SDK's CYW43 worker task, which is also BTstack's thread (CYW43_TASK_STACK_SIZE is in words: 1536) */

/* The CYW43 worker's stack is set by CYW43_TASK_STACK_SIZE (WORDS, platforms/rp2350/freertos/CMakeLists.txt); keep the two in step. */
#ifdef CYW43_TASK_STACK_SIZE
_Static_assert(RT_STACK_RADIO == CYW43_TASK_STACK_SIZE * 4u, "RT_STACK_RADIO must equal CYW43_TASK_STACK_SIZE words in bytes");
#endif

#endif // NEUPOD_RUNTIME_CONFIG_H

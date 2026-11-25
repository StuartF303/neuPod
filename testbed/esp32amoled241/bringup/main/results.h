// SPDX-License-Identifier: MPL-2.0
// Per-step PASS/FAIL table, periodic summary, and the single-key serial console.
#pragma once

#include <stdint.h>

typedef enum { ST_PENDING, ST_PASS, ST_FAIL, ST_INFO } res_status_t;

typedef enum {
    R_BOOT, R_PSRAM, R_I2C, R_TCA, R_DISP, R_FPS, R_BRIGHT, R_TOUCH_ID, R_TOUCH_PT,
    R_IMU, R_RTC, R_BATT, R_KEY, R_SD, R_WIFI, R_BLE, R_COUNT
} res_id_t;

void res_init(void);
void res_set(res_id_t id, res_status_t st, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void res_print_summary(void);

// Starts the console task: 's' reprints the summary, the summary also reprints every 20 s.
// Other keys are queued for the main loop (console_take_key, 0 when none).
void console_start(void);
char console_take_key(void);

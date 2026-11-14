// SPDX-License-Identifier: MPL-2.0
// I2C peripherals: bus scan, TCA9554 expander, FT6336 touch, QMI8658 IMU, PCF85063 RTC.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t x, y;      // raw panel coordinates (portrait, x 0..449, y 0..599)
    uint8_t event;      // 0 down, 1 up, 2 contact
} touch_pt_t;

esp_err_t periph_i2c_init(void);
void periph_i2c_scan(void);                 // sets R_I2C
void periph_tca_init(void);                 // sets R_TCA; EXIO1 (PWR_EN) high, EXIO0/2/3/4 inputs
int periph_tca_inputs(void);                // input register, or -1
void periph_touch_init(void);               // reset pulse, chip ID; sets R_TOUCH_ID
int periph_touch_read(touch_pt_t pts[2]);   // number of points, or -1 on bus error
void periph_imu_probe(void);                // sets R_IMU
void periph_rtc_probe(void);                // sets R_RTC (read only, never writes the clock)

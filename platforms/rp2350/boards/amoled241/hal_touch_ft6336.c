// SPDX-License-Identifier: MPL-2.0
/*
 * FT6336 capacitive touch (Waveshare RP2350-Touch-AMOLED-2.41): I2C1 address 0x38, reset GPIO5, interrupt GPIO4 (not used: the
 * input task polls). Our own code; the register numbers are from the controller's data sheet.
 *
 * Only the first contact is reported. The controller's raw coordinates are in the panel's native portrait frame (x 0..449 across,
 * y 0..599 down); they are turned into the landscape frame the display HAL reports (see board.h, NEUPOD_AMOLED_LANDSCAPE), which
 * is the same transform the display's MADCTL applies to pixels.
 */
#include <stdio.h>

#include "board.h"
#include "hal_i2c.h"
#include "hal_touch.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// Registers.
#define FT_REG_TD_STATUS 0x02 // bits 3:0 = number of contacts; 0x03.. = first contact (XH, XL, YH, YL)
#define FT_REG_LIB_VER_H 0xA1
#define FT_REG_CHIP_ID 0xA3 // 0x64 on the FT6336U
#define FT_REG_FIRMWARE_ID 0xA6
#define FT_REG_VENDOR_ID 0xA8 // 0x11 = FocalTech

#define I2C_HZ 400000u
#define POLL_MIN_US 8000u // the controller refreshes at about 60 Hz in active mode: no point reading faster

#ifndef NEUPOD_TOUCH_LOG
#define NEUPOD_TOUCH_LOG 0
#endif

static bool g_present;
static hal_touch_point_t g_last;
static uint64_t g_last_read_us;
static bool g_last_ok = true;
static uint32_t g_fails;

bool hal_touch_present(void) { return true; }

hal_status_t hal_touch_init(void) {
    // The bus is normally brought up by the boot sequence; make sure it is when this is called earlier.
    if (hal_i2c_get_baud() == 0) {
        if (hal_i2c_init(I2C_HZ) != HAL_I2C_OK) {
            printf("touch: I2C bus did not initialise\n");
            return HAL_ENODEV;
        }
    }
    gpio_init(TOUCH_PIN_INT);
    gpio_set_dir(TOUCH_PIN_INT, GPIO_IN);
    gpio_pull_up(TOUCH_PIN_INT);
    gpio_init(TOUCH_PIN_RST);
    gpio_put(TOUCH_PIN_RST, 1);
    gpio_set_dir(TOUCH_PIN_RST, GPIO_OUT);
    sleep_ms(10);
    gpio_put(TOUCH_PIN_RST, 0);
    sleep_ms(10);
    gpio_put(TOUCH_PIN_RST, 1);
    sleep_ms(300); // the controller needs about 300 ms after reset before it answers (the probe's figure)

    uint8_t chip = 0, vendor = 0, fw = 0;
    hal_i2c_err_t e = hal_i2c_reg_read8(TOUCH_I2C_ADDR, FT_REG_CHIP_ID, &chip);
    if (e != HAL_I2C_OK) {
        printf("touch: FT6336 at 0x%02X did not answer (%s)\n", TOUCH_I2C_ADDR, hal_i2c_strerror(e));
        g_present = false;
        return HAL_ENODEV;
    }
    (void)hal_i2c_reg_read8(TOUCH_I2C_ADDR, FT_REG_VENDOR_ID, &vendor);
    (void)hal_i2c_reg_read8(TOUCH_I2C_ADDR, FT_REG_FIRMWARE_ID, &fw);
    printf("touch: FT6336 chip ID 0x%02X (FT6336U = 0x64), vendor 0x%02X (FocalTech = 0x11), firmware 0x%02X\n", chip, vendor, fw);
    g_present = true;
    g_last = (hal_touch_point_t){0};
    g_last_ok = true;
    g_last_read_us = 0;
    g_fails = 0;
    return HAL_OK;
}

#if NEUPOD_TOUCH_LOG
static void log_point(const char *what, unsigned rx, unsigned ry, const hal_touch_point_t *p) {
    printf("touch: %s raw x=%u y=%u -> x=%u y=%u\n", what, rx, ry, (unsigned)p->x, (unsigned)p->y);
}
#endif

bool hal_touch_read(hal_touch_point_t *out) {
    if (out == NULL) {
        return false;
    }
    if (!g_present) {
        *out = (hal_touch_point_t){0};
        return false;
    }
    uint64_t now = time_us_64();
    if (g_last_read_us != 0 && now - g_last_read_us < POLL_MIN_US) {
        *out = g_last; // cached: the caller polls faster than the controller reports
        return g_last_ok;
    }
    g_last_read_us = now;

    uint8_t b[5];
    if (hal_i2c_reg_read_burst(TOUCH_I2C_ADDR, FT_REG_TD_STATUS, b, sizeof b) != HAL_I2C_OK) {
        if (++g_fails == 20) {
            printf("touch: I2C read failing (20 in a row)\n");
        }
        g_last = (hal_touch_point_t){0};
        g_last_ok = false;
        *out = g_last;
        return false;
    }
    g_fails = 0;
    g_last_ok = true;

    unsigned n = b[0] & 0x0Fu;
    unsigned event = (unsigned)b[1] >> 6; // 0 = press, 1 = lift, 2 = contact
    bool down = n >= 1u && n <= 2u && event != 1u;
    unsigned rx = (((unsigned)b[1] & 0x0Fu) << 8) | b[2];
    unsigned ry = (((unsigned)b[3] & 0x0Fu) << 8) | b[4];
    if (rx >= AMOLED_NATIVE_W) {
        rx = AMOLED_NATIVE_W - 1u;
    }
    if (ry >= AMOLED_NATIVE_H) {
        ry = AMOLED_NATIVE_H - 1u;
    }
    hal_touch_point_t p = g_last;
    p.down = down;
    if (down) {
#if NEUPOD_AMOLED_LANDSCAPE == 3
        p.x = (uint16_t)(AMOLED_NATIVE_H - 1u - ry);
        p.y = (uint16_t)rx;
#else
        p.x = (uint16_t)ry;
        p.y = (uint16_t)(AMOLED_NATIVE_W - 1u - rx);
#endif
    }
#if NEUPOD_TOUCH_LOG
    static uint64_t last_move_log_us;
    if (down && !g_last.down) {
        log_point("down", rx, ry, &p);
        last_move_log_us = now;
    } else if (down && (p.x != g_last.x || p.y != g_last.y) && now - last_move_log_us > 200000u) {
        log_point("move", rx, ry, &p);
        last_move_log_us = now;
    } else if (!down && g_last.down) {
        printf("touch: up\n");
    }
#endif
    g_last = p;
    *out = p;
    return true;
}

// SPDX-License-Identifier: MPL-2.0
/*
 * FT6336 capacitive touch controller (I2C 0x38 on the shared bus, reset on GPIO3). Polled: the interrupt line is behind the
 * TCA9554 expander (EXIO2) and the controller reports at ~60 Hz anyway, so the input task's 2 ms loop reads at most every
 * TOUCH_POLL_MS and otherwise returns the last answer.
 *
 * Coordinates: the controller reports portrait panel coordinates (x 0..449, y 0..599). The display runs landscape (MADCTL 0x30,
 * hal_display.c), so the point is rotated: landscape x = y, landscape y = 449 - x (swap and mirror x). Bench-confirmed
 * 2026-11-15: the factory demo's mapping (599 - y, x) came out 180 degrees rotated with the factory init table's MADCTL 0x30.
 * The three TOUCH_* switches below are the one place to change it.
 */
#include <stdio.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_display.h"
#include "hal_touch.h"

#define TOUCH_SWAP_XY 1
#define TOUCH_MIRROR_X 1 /* applied to the portrait x before the swap */
#define TOUCH_MIRROR_Y 0 /* applied to the portrait y before the swap */
#define TOUCH_POLL_MS 8
#define TOUCH_TIMEOUT_MS 20

#define REG_STATUS 0x02 /* TD_STATUS, then 6 bytes per point */
#define REG_CHIP_ID 0xA3
#define REG_FW 0xA6
#define REG_VENDOR 0xA8

static i2c_master_dev_handle_t s_dev;
static bool s_up;
static hal_touch_point_t s_last;
static uint32_t s_last_ms;
static bool s_last_ok;

bool hal_touch_present(void) { return true; }

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, TOUCH_TIMEOUT_MS);
}

hal_status_t hal_touch_init(void) {
    if (s_up) {
        return HAL_OK;
    }
    if (board_i2c_bus() == NULL) {
        return HAL_ENODEV;
    }
    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_FT6336, .scl_speed_hz = 400000};
    if (s_dev == NULL && i2c_master_bus_add_device(board_i2c_bus(), &dc, &s_dev) != ESP_OK) {
        return HAL_ERR;
    }
    gpio_config_t g = {.pin_bit_mask = 1ULL << PIN_TOUCH_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&g);
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    uint8_t chip = 0, fw = 0, vendor = 0;
    esp_err_t e = rd(REG_CHIP_ID, &chip, 1);
    if (e == ESP_OK) {
        e = rd(REG_FW, &fw, 1);
    }
    if (e == ESP_OK) {
        e = rd(REG_VENDOR, &vendor, 1);
    }
    if (e != ESP_OK || chip == 0x00 || chip == 0xFF) {
        printf("touch: FT6336 not found (%s, chip id 0x%02X)\n", esp_err_to_name(e), chip);
        return HAL_ENODEV;
    }
    printf("touch: FT6336 chip id 0x%02X (0x64 = FT6336) firmware 0x%02X vendor 0x%02X\n", chip, fw, vendor);
    s_up = true;
    return HAL_OK;
}

static uint16_t clamp(int v, int hi) { return (uint16_t)(v < 0 ? 0 : v > hi ? hi : v); }

bool hal_touch_read(hal_touch_point_t *out) {
    if (out == NULL) {
        return false;
    }
    if (!s_up) {
        *out = (hal_touch_point_t){0};
        return false;
    }
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((uint32_t)(now - s_last_ms) < TOUCH_POLL_MS) {
        *out = s_last;
        return s_last_ok;
    }
    s_last_ms = now;
    uint8_t b[7];
    if (rd(REG_STATUS, b, sizeof b) != ESP_OK) {
        s_last = (hal_touch_point_t){0};
        s_last_ok = false;
        *out = s_last;
        return false;
    }
    s_last_ok = true;
    unsigned n = b[0] & 0x0Fu;
    unsigned event = b[1] >> 6; /* 0 press, 1 lift, 2 contact, 3 none */
    if (n == 0 || n > 2 || event == 1 || event == 3) {
        s_last = (hal_touch_point_t){0};
    } else {
        int x = ((b[1] & 0x0F) << 8) | b[2];
        int y = ((b[3] & 0x0F) << 8) | b[4];
        if (TOUCH_MIRROR_X) {
            x = PANEL_NATIVE_W - 1 - x;
        }
        if (TOUCH_MIRROR_Y) {
            y = PANEL_NATIVE_H - 1 - y;
        }
        int lx = TOUCH_SWAP_XY ? y : x;
        int ly = TOUCH_SWAP_XY ? x : y;
        s_last.x = clamp(lx, (int)hal_display_width() - 1);
        s_last.y = clamp(ly, (int)hal_display_height() - 1);
        s_last.down = true;
    }
    *out = s_last;
    return true;
}

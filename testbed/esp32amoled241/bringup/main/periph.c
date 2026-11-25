// SPDX-License-Identifier: MPL-2.0
#include "periph.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "results.h"

#define I2C_TMO_MS 50

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_tca, s_touch, s_imu, s_rtc;

static esp_err_t add_dev(uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(s_bus, &dc, out);
}

static esp_err_t rd(i2c_master_dev_handle_t d, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(d, &reg, 1, buf, n, I2C_TMO_MS);
}

static esp_err_t wr(i2c_master_dev_handle_t d, uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(d, b, 2, I2C_TMO_MS);
}

esp_err_t periph_i2c_init(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&bc, &s_bus);
    if (e != ESP_OK) return e;
    (void)add_dev(I2C_ADDR_TCA9554, &s_tca);
    (void)add_dev(I2C_ADDR_FT6336, &s_touch);
    (void)add_dev(I2C_ADDR_QMI8658, &s_imu);
    (void)add_dev(I2C_ADDR_PCF85063, &s_rtc);
    return ESP_OK;
}

void periph_i2c_scan(void)
{
    char list[96] = "";
    int found = 0;
    bool want[4] = { 0 };
    static const uint8_t expect[4] = { I2C_ADDR_TCA9554, I2C_ADDR_FT6336, I2C_ADDR_PCF85063, I2C_ADDR_QMI8658 };
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(s_bus, a, I2C_TMO_MS) == ESP_OK) {
            found++;
            size_t l = strlen(list);
            snprintf(list + l, sizeof list - l, "0x%02X ", a);
            for (int i = 0; i < 4; i++) if (a == expect[i]) want[i] = true;
        }
    }
    bool all = want[0] && want[1] && want[2] && want[3];
    res_set(R_I2C, all ? ST_PASS : ST_FAIL, "%d found: %s| need TCA 0x20=%d touch 0x38=%d rtc 0x51=%d imu 0x6B=%d",
            found, list, want[0], want[1], want[2], want[3]);
}

#define TCA_IN  0x00
#define TCA_OUT 0x01
#define TCA_CFG 0x03

void periph_tca_init(void)
{
    // Output latch first (PWR_EN high), then make EXIO1 an output, so it never glitches low.
    uint8_t out = 1 << EXIO_PWR_EN;
    uint8_t cfg = (uint8_t)~(1 << EXIO_PWR_EN);
    esp_err_t e1 = wr(s_tca, TCA_OUT, out);
    esp_err_t e2 = wr(s_tca, TCA_CFG, cfg);
    uint8_t rcfg = 0xFF, rout = 0, rin = 0;
    esp_err_t e3 = rd(s_tca, TCA_CFG, &rcfg, 1);
    esp_err_t e4 = rd(s_tca, TCA_OUT, &rout, 1);
    esp_err_t e5 = rd(s_tca, TCA_IN, &rin, 1);
    bool ok = !e1 && !e2 && !e3 && !e4 && !e5 && rcfg == cfg && (rout & out);
    res_set(R_TCA, ok ? ST_PASS : ST_FAIL,
            "cfg=0x%02X (want 0x%02X) out=0x%02X in=0x%02X; PWR_EN(EXIO1)=%d TE(EXIO0)=%d TP_INT(EXIO2)=%d IMU_INT2(EXIO3)=%d RTC_INT(EXIO4)=%d",
            rcfg, cfg, rout, rin, (rin >> 1) & 1, rin & 1, (rin >> 2) & 1, (rin >> 3) & 1, (rin >> 4) & 1);
}

int periph_tca_inputs(void)
{
    uint8_t v;
    return rd(s_tca, TCA_IN, &v, 1) == ESP_OK ? v : -1;
}

void periph_touch_init(void)
{
    gpio_config_t g = { .pin_bit_mask = 1ULL << PIN_TOUCH_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&g);
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    uint8_t chip = 0, fw = 0, vendor = 0;
    esp_err_t e1 = rd(s_touch, 0xA3, &chip, 1);   // chip ID (FT6336: 0x64)
    esp_err_t e2 = rd(s_touch, 0xA6, &fw, 1);     // firmware version
    esp_err_t e3 = rd(s_touch, 0xA8, &vendor, 1); // vendor ID (FocalTech: 0x11)
    bool ok = !e1 && !e2 && !e3 && chip != 0x00 && chip != 0xFF;
    res_set(R_TOUCH_ID, ok ? ST_PASS : ST_FAIL, "chip id 0x%02X (FT6336=0x64) fw 0x%02X vendor 0x%02X%s",
            chip, fw, vendor, ok ? "" : " (read error)");
    res_set(R_TOUCH_PT, ST_PENDING, "touch the screen");
}

int periph_touch_read(touch_pt_t pts[2])
{
    uint8_t b[13];
    if (rd(s_touch, 0x02, b, sizeof b) != ESP_OK) return -1;
    int n = b[0] & 0x0F;
    if (n > 2) n = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = &b[1 + 6 * i];
        pts[i].event = p[0] >> 6;
        pts[i].x = ((p[0] & 0x0F) << 8) | p[1];
        pts[i].y = ((p[2] & 0x0F) << 8) | p[3];
    }
    return n;
}

void periph_imu_probe(void)
{
    uint8_t who = 0, rev = 0;
    esp_err_t e = rd(s_imu, 0x00, &who, 1);
    if (e != ESP_OK) {
        res_set(R_IMU, ST_FAIL, "no answer at 0x%02X (%s)", I2C_ADDR_QMI8658, esp_err_to_name(e));
        return;
    }
    (void)rd(s_imu, 0x01, &rev, 1);
    // Enable the accelerometer (+-2 g, 250 Hz) for a quick sanity reading; the chip is left running.
    (void)wr(s_imu, 0x02, 0x40);
    (void)wr(s_imu, 0x03, 0x05);
    (void)wr(s_imu, 0x08, 0x01);
    vTaskDelay(pdMS_TO_TICKS(80));
    uint8_t d[6] = { 0 };
    esp_err_t e2 = rd(s_imu, 0x35, d, sizeof d);
    int16_t ax = (int16_t)(d[0] | d[1] << 8), ay = (int16_t)(d[2] | d[3] << 8), az = (int16_t)(d[4] | d[5] << 8);
    float g = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az) / 16384.0f;
    res_set(R_IMU, who == 0x05 ? ST_PASS : ST_FAIL,
            "WHO_AM_I 0x%02X (QMI8658=0x05) rev 0x%02X; accel %d,%d,%d |a|=%.2f g%s",
            who, rev, ax, ay, az, (double)g, e2 ? " (data read failed)" : "");
}

static int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

void periph_rtc_probe(void)
{
    uint8_t t[7];
    esp_err_t e = rd(s_rtc, 0x04, t, sizeof t);
    if (e != ESP_OK) {
        res_set(R_RTC, ST_FAIL, "no answer at 0x%02X (%s)", I2C_ADDR_PCF85063, esp_err_to_name(e));
        return;
    }
    bool os = t[0] & 0x80;
    int sec = bcd(t[0] & 0x7F), min = bcd(t[1] & 0x7F), hour = bcd(t[2] & 0x3F);
    int day = bcd(t[3] & 0x3F), mon = bcd(t[5] & 0x1F), year = 2000 + bcd(t[6]);
    bool valid = sec < 60 && min < 60 && hour < 24 && day >= 1 && day <= 31 && mon >= 1 && mon <= 12;
    res_set(R_RTC, valid ? ST_PASS : ST_FAIL, "%04d-%02d-%02d %02d:%02d:%02d%s",
            year, mon, day, hour, min, sec, os ? "  (oscillator-stop flag set: clock never set)" : "");
}

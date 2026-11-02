// SPDX-License-Identifier: MPL-2.0
// MAX17048 driver: scaling, alert config encoding and the reset path against a fake chip.
#include <stdio.h>

#include "hal_battery.h"
#include "hal_i2c.h"
#include "hal_i2c_mock.h"
#include "max17048.h"

static int s_fail;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                   \
            s_fail++;                                                             \
        }                                                                         \
    } while (0)

static uint8_t regs[0x100];

static void put16(uint8_t reg, uint16_t v) {
    regs[reg] = (uint8_t)(v >> 8);
    regs[reg + 1] = (uint8_t)v;
}
static uint16_t get16(uint8_t reg) { return (uint16_t)((regs[reg] << 8) | regs[reg + 1]); }

static unsigned s_por;
static void on_w(void *ctx, uint8_t reg, uint8_t v) {
    (void)ctx;
    if (reg == MAX17048_REG_CMD + 1 && v == 0x00 && regs[MAX17048_REG_CMD] == 0x54) {
        s_por++;
        put16(MAX17048_REG_STATUS, MAX17048_STATUS_RI);
    }
}

int main(void) {
    // Scaling (78.125 uV/LSB, 1/256 %, 0.208 %/h)
    CHECK(max17048_vcell_to_mv(0) == 0);
    CHECK(max17048_vcell_to_mv(0xC000) == 3840);  // 49152 * 78.125 uV = 3.84 V exactly
    CHECK(max17048_vcell_to_mv(0xD000) == 4160);  // 53248 * 78.125 uV = 4.16 V
    CHECK(max17048_vcell_to_mv(1) == 0);          // 0.078 mV rounds to 0
    CHECK(max17048_vcell_to_mv(13) == 1);         // 1.0156 mV
    CHECK(max17048_vcell_to_mv(0xFFFF) == 5120);  // full scale 5119.92 mV
    CHECK(max17048_soc_to_pct(0x6400) == 100);
    CHECK(max17048_soc_to_pct(0x3280) == 50);     // 50.5 %
    CHECK(max17048_soc_to_pct(0xFFFF) == 100);    // clamped
    CHECK(max17048_soc_to_pct(0x00FF) == 0);
    CHECK(max17048_crate_to_mpct_per_h(0) == 0);
    CHECK(max17048_crate_to_mpct_per_h(1) == 208);
    CHECK(max17048_crate_to_mpct_per_h(0xFFFF) == -208);   // -1 LSB
    CHECK(max17048_crate_to_mpct_per_h(0x8000) == -32768 * 208);
    CHECK(max17048_crate_to_mpct_per_h(0x7FFF) == 32767 * 208);
    // ATHD = 32 - pct
    CHECK(max17048_config_set_athd(0x971C, 4) == 0x971C);  // power-on default is 4 %
    CHECK(max17048_config_set_athd(0x9700, 1) == 0x971F);
    CHECK(max17048_config_set_athd(0x97E0, 32) == 0x97E0);
    CHECK(max17048_config_set_athd(0x9700, 0) == 0x971F);  // clamped to 1 %
    CHECK(max17048_config_set_athd(0x9700, 40) == 0x9700); // clamped to 32 %
    CHECK(max17048_config_athd_pct(0x971C) == 4);
    // HIBRT encode/decode maths only: hib 0.208 %/h per LSB (high byte), act 1.25 mV per LSB (low).
    // (A chip read 0x8030 on the bench; no power-on default is asserted here.)
    uint32_t hm, am;
    max17048_hibrt_decode(0x8030, &hm, &am);
    CHECK(hm == 128u * 208u && am == 60);
    max17048_hibrt_decode(0xFFFF, &hm, &am);
    CHECK(hm == 255u * 208u && am == 318);
    CHECK(max17048_hibrt_encode(128u * 208u, 60) == 0x8030);
    CHECK(max17048_hibrt_encode(0, 0) == 0);
    CHECK(max17048_hibrt_encode(1000000, 1000000) == 0xFFFF);
    CHECK(max17048_status_to_alerts(0x4000 | MAX17048_STATUS_HD | MAX17048_STATUS_RI) ==
          (HAL_BATTERY_ALERT_SOC_LOW | HAL_BATTERY_ALERT_RESET));

    // Driver against a fake chip
    hal_i2c_mock_reset();
    CHECK(hal_i2c_init(400000) == HAL_I2C_OK);
    CHECK(hal_battery_init() == HAL_ENODEV);               // nothing at 0x36
    hal_battery_status_t b;
    CHECK(hal_battery_read(&b) == HAL_ENODEV && !b.valid);
    CHECK(hal_i2c_mock_add_regmap(0x36, regs, sizeof regs, on_w, NULL));
    put16(MAX17048_REG_VERSION, 0x0012);
    put16(MAX17048_REG_CONFIG, 0x971C);
    put16(MAX17048_REG_STATUS, MAX17048_STATUS_RI | MAX17048_STATUS_HD);
    CHECK(hal_battery_init() == HAL_OK);
    CHECK(get16(MAX17048_REG_STATUS) == MAX17048_STATUS_HD); // RI cleared, other flags kept
    put16(MAX17048_REG_VERSION, 0x0002);
    CHECK(hal_battery_init() == HAL_ENODEV);               // wrong chip
    put16(MAX17048_REG_VERSION, 0x0010);
    CHECK(hal_battery_init() == HAL_OK);

    put16(MAX17048_REG_VCELL, 0xC000);
    put16(MAX17048_REG_SOC, 0x3280);
    put16(MAX17048_REG_CRATE, 0xFFFF - 9);                 // -10 LSB = -2.080 %/h
    put16(MAX17048_REG_STATUS, MAX17048_STATUS_HD | MAX17048_STATUS_ENVR);
    CHECK(hal_battery_read(&b) == HAL_OK && b.valid && b.present);
    CHECK(b.mv == 3840 && b.soc_x256 == 0x3280 && b.soc_pct == 50);
    CHECK(b.rate_mpct_per_h == -2080);
    CHECK(b.alerts == HAL_BATTERY_ALERT_SOC_LOW);
    put16(MAX17048_REG_VCELL, 0x7000);                     // 2.24 V: below the present threshold
    CHECK(hal_battery_read(&b) == HAL_OK && !b.present && b.mv == 2240);

    CHECK(hal_battery_set_alert_threshold(10) == HAL_OK && get16(MAX17048_REG_CONFIG) == 0x9716);
    CHECK(hal_battery_set_alert_threshold(0) == HAL_EINVAL);
    CHECK(hal_battery_set_alert_threshold(33) == HAL_EINVAL);
    put16(MAX17048_REG_CONFIG, 0x9736);                    // ALRT set
    CHECK(hal_battery_clear_alert() == HAL_OK);
    CHECK(get16(MAX17048_REG_CONFIG) == 0x9716);
    CHECK(get16(MAX17048_REG_STATUS) == MAX17048_STATUS_ENVR);
    CHECK(hal_battery_quick_start() == HAL_OK && get16(MAX17048_REG_MODE) == 0x4000);
    CHECK(hal_battery_set_hibernate(128u * 208u, 60) == HAL_OK && get16(MAX17048_REG_HIBRT) == 0x8030);

    // Reset: the chip NACKs the end of the command (injected); it must be treated as success.
    hal_i2c_mock_inject_error(HAL_I2C_NACK_DATA, 1);
    CHECK(hal_battery_reset() == HAL_OK);
    CHECK(hal_battery_reset() == HAL_OK && s_por == 1);    // second time the write lands
    CHECK(get16(MAX17048_REG_STATUS) & MAX17048_STATUS_RI);

    printf(s_fail ? "test_max17048: %d FAILED\n" : "test_max17048: ok\n", s_fail);
    return s_fail ? 1 : 0;
}

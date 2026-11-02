// SPDX-License-Identifier: MPL-2.0
// hal_i2c over the host mock bus: scan, register helpers, error mapping, recovery.
#include <stdio.h>

#include "hal_i2c.h"
#include "hal_i2c_mock.h"

static int s_fail;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        if (!(c)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            s_fail++;                                                                     \
        }                                                                                 \
    } while (0)

static uint8_t regs[0x20];
static unsigned s_cb_calls;
static void on_w(void *ctx, uint8_t reg, uint8_t v) {
    (void)ctx;
    (void)reg;
    (void)v;
    s_cb_calls++;
}

static void collect(uint8_t a, void *ctx) { ((uint8_t *)ctx)[a] = 1; }

// B1 regression: a scan with absent addresses (NACKs) must never trigger bus recovery.
static void test_scan_no_recovery(void) {
    static uint8_t a[4], b[4];
    hal_i2c_mock_reset();
    CHECK(hal_i2c_mock_add_regmap(0x36, a, sizeof a, NULL, NULL));
    CHECK(hal_i2c_mock_add_regmap(0x5A, b, sizeof b, NULL, NULL));
    CHECK(hal_i2c_init(400000) == HAL_I2C_OK);
    size_t n = 0;
    CHECK(hal_i2c_scan(HAL_I2C_ADDR_FIRST, HAL_I2C_ADDR_LAST, NULL, NULL, &n) == HAL_I2C_OK);
    CHECK(n == 2);
    hal_i2c_stats_t st;
    hal_i2c_get_stats(&st);
    CHECK(st.recoveries == 0 && st.recovery_failures == 0);
    CHECK(st.probe_nacks == 110 && st.timeouts == 0 && st.nack_addr == 0);
    hal_i2c_deinit();
}

int main(void) {
    test_scan_no_recovery();
    hal_i2c_mock_reset();
    CHECK(hal_i2c_probe(0x36) == HAL_I2C_NOT_INIT);
    CHECK(hal_i2c_mock_add_regmap(0x36, regs, sizeof regs, on_w, NULL));
    CHECK(!hal_i2c_mock_add_regmap(0x36, regs, sizeof regs, NULL, NULL));
    CHECK(hal_i2c_init(5) == HAL_I2C_BAD_PARAM);
    CHECK(hal_i2c_init(400000) == HAL_I2C_OK);
    CHECK(hal_i2c_get_baud() == 400000);

    uint8_t seen[128] = {0};
    size_t n = 99;
    CHECK(hal_i2c_scan(0, 0x7F, collect, seen, &n) == HAL_I2C_OK);
    CHECK(n == 1 && seen[0x36]);
    CHECK(hal_i2c_probe(0x05) == HAL_I2C_BAD_PARAM);
    CHECK(hal_i2c_probe(0x50) == HAL_I2C_NACK_ADDR);

    CHECK(hal_i2c_reg_write16_be(0x36, 0x04, 0xBEEF) == HAL_I2C_OK);
    CHECK(regs[4] == 0xBE && regs[5] == 0xEF && s_cb_calls == 2);
    uint16_t v = 0;
    CHECK(hal_i2c_reg_read16_be(0x36, 0x04, &v) == HAL_I2C_OK && v == 0xBEEF);
    uint8_t b = 0, burst[4];
    CHECK(hal_i2c_reg_read8(0x36, 5, &b) == HAL_I2C_OK && b == 0xEF);
    CHECK(hal_i2c_reg_write8(0x36, 7, 0x42) == HAL_I2C_OK && regs[7] == 0x42);
    CHECK(hal_i2c_reg_read_burst(0x36, 4, burst, 4) == HAL_I2C_OK && burst[0] == 0xBE && burst[3] == regs[7]);
    CHECK(hal_i2c_reg_read8(0x37, 0, &b) == HAL_I2C_NACK_ADDR);
    CHECK(hal_i2c_write(0x36, NULL, 1) == HAL_I2C_BAD_PARAM);
    CHECK(hal_i2c_read(0x36, &b, 0) == HAL_I2C_BAD_PARAM);
    CHECK(hal_i2c_write(0x80, &b, 1) == HAL_I2C_BAD_PARAM);
    CHECK(hal_i2c_write(0x00, &b, 1) == HAL_I2C_BAD_PARAM && hal_i2c_read(0x07, &b, 1) == HAL_I2C_BAD_PARAM);
    CHECK(hal_i2c_write(0x78, &b, 1) == HAL_I2C_BAD_PARAM && hal_i2c_reg_read8(0x7F, 0, &b) == HAL_I2C_BAD_PARAM);

    hal_i2c_stats_t st;
    hal_i2c_get_stats(&st);
    CHECK(st.nack_addr == 1 && st.probe_nacks > 100);

    hal_i2c_reset_stats();
    hal_i2c_mock_inject_error(HAL_I2C_TIMEOUT, 1);
    CHECK(hal_i2c_reg_read8(0x36, 0, &b) == HAL_I2C_TIMEOUT);
    CHECK(hal_i2c_reg_read8(0x36, 0, &b) == HAL_I2C_OK);
    hal_i2c_get_stats(&st);
    CHECK(st.timeouts == 1 && st.transactions == 2);

    // SDA stuck but freeable: next call times out, recovers, and the one after works.
    hal_i2c_mock_set_stuck_sda(3);
    bool sda = true, scl = true;
    CHECK(hal_i2c_bus_state(&sda, &scl) == HAL_I2C_OK && !sda && scl);
    CHECK(hal_i2c_reg_read8(0x36, 0, &b) == HAL_I2C_TIMEOUT);
    CHECK(hal_i2c_reg_read8(0x36, 0, &b) == HAL_I2C_OK);
    hal_i2c_get_stats(&st);
    CHECK(st.recoveries == 1 && st.recovery_failures == 0);

    // Unfreeable: BUS_STUCK, also at init.
    hal_i2c_mock_set_stuck_sda(20);
    CHECK(hal_i2c_reg_read8(0x36, 0, &b) == HAL_I2C_BUS_STUCK);
    CHECK(hal_i2c_init(100000) == HAL_I2C_BUS_STUCK);
    hal_i2c_get_stats(&st);
    CHECK(st.recovery_failures == 1);
    hal_i2c_mock_set_stuck_sda(0);

    CHECK(hal_i2c_set_baud(100000) == HAL_I2C_OK && hal_i2c_get_baud() == 100000);
    hal_i2c_deinit();
    CHECK(hal_i2c_get_baud() == 0 && hal_i2c_set_baud(100000) == HAL_I2C_NOT_INIT);

    printf(s_fail ? "test_i2c: %d FAILED\n" : "test_i2c: ok\n", s_fail);
    return s_fail ? 1 : 0;
}

// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 hal_i2c: a stub. The firmware does not use the Y2's I2C buses (battery comes from sysfs, haptics are the
 * timed_output vibrator, hal_haptic.c). init succeeds so the boot checks that run on the bus start normally; every
 * transfer then reports that nothing acknowledges (HAL_I2C_NACK_ADDR).
 */
#include "hal_i2c.h"

static bool g_init;
static uint32_t g_baud;

hal_i2c_err_t hal_i2c_init(uint32_t bus_hz) {
    g_init = true;
    g_baud = bus_hz;
    return HAL_I2C_OK;
}

void hal_i2c_deinit(void) { g_init = false; }
uint32_t hal_i2c_get_baud(void) { return g_baud; }

hal_i2c_err_t hal_i2c_set_baud(uint32_t bus_hz) {
    g_baud = bus_hz;
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_bus_state(bool *sda_high, bool *scl_high) {
    if (sda_high != NULL) {
        *sda_high = true;
    }
    if (scl_high != NULL) {
        *scl_high = true;
    }
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_recover(void) { return HAL_I2C_OK; }

hal_i2c_err_t hal_i2c_probe(uint8_t addr) {
    (void)addr;
    return g_init ? HAL_I2C_NACK_ADDR : HAL_I2C_NOT_INIT;
}

hal_i2c_err_t hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    (void)addr;
    (void)data;
    (void)len;
    return g_init ? HAL_I2C_NACK_ADDR : HAL_I2C_NOT_INIT;
}

hal_i2c_err_t hal_i2c_read(uint8_t addr, uint8_t *data, size_t len) {
    (void)addr;
    (void)data;
    (void)len;
    return g_init ? HAL_I2C_NACK_ADDR : HAL_I2C_NOT_INIT;
}

hal_i2c_err_t hal_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen) {
    (void)addr;
    (void)wdata;
    (void)wlen;
    (void)rdata;
    (void)rlen;
    return g_init ? HAL_I2C_NACK_ADDR : HAL_I2C_NOT_INIT;
}

void hal_i2c_get_stats(hal_i2c_stats_t *out) { *out = (hal_i2c_stats_t){0}; }
void hal_i2c_reset_stats(void) {}

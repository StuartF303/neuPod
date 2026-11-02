// SPDX-License-Identifier: MPL-2.0
// Platform-independent parts of hal_i2c: scan, register helpers, error strings.
#include <string.h>

#include "hal_i2c.h"

const char *hal_i2c_strerror(hal_i2c_err_t err) {
    switch (err) {
    case HAL_I2C_OK: return "OK";
    case HAL_I2C_NACK_ADDR: return "NACK_ADDR";
    case HAL_I2C_NACK_DATA: return "NACK_DATA";
    case HAL_I2C_TIMEOUT: return "TIMEOUT";
    case HAL_I2C_BUS_STUCK: return "BUS_STUCK";
    case HAL_I2C_ARB_LOST: return "ARB_LOST";
    case HAL_I2C_BAD_PARAM: return "BAD_PARAM";
    case HAL_I2C_NOT_INIT: return "NOT_INIT";
    default: return "?";
    }
}

hal_i2c_err_t hal_i2c_scan(uint8_t first, uint8_t last, hal_i2c_scan_cb cb, void *ctx, size_t *found) {
    if (first < HAL_I2C_ADDR_FIRST) {
        first = HAL_I2C_ADDR_FIRST;
    }
    if (last > HAL_I2C_ADDR_LAST) {
        last = HAL_I2C_ADDR_LAST;
    }
    size_t n = 0;
    hal_i2c_err_t worst = HAL_I2C_OK;
    for (unsigned a = first; a <= last; a++) {
        hal_i2c_err_t e = hal_i2c_probe((uint8_t)a);
        if (e == HAL_I2C_OK) {
            n++;
            if (cb) {
                cb((uint8_t)a, ctx);
            }
        } else if (e != HAL_I2C_NACK_ADDR && worst == HAL_I2C_OK) {
            worst = e; // keep scanning: the backend has already recovered the bus
        }
    }
    if (found) {
        *found = n;
    }
    return worst;
}

hal_i2c_err_t hal_i2c_reg_read8(uint8_t addr, uint8_t reg, uint8_t *value) {
    return hal_i2c_write_read(addr, &reg, 1, value, 1);
}

hal_i2c_err_t hal_i2c_reg_write8(uint8_t addr, uint8_t reg, uint8_t value) {
    const uint8_t buf[2] = {reg, value};
    return hal_i2c_write(addr, buf, sizeof buf);
}

hal_i2c_err_t hal_i2c_reg_read16_be(uint8_t addr, uint8_t reg, uint16_t *value) {
    if (!value) {
        return HAL_I2C_BAD_PARAM;
    }
    uint8_t b[2];
    hal_i2c_err_t e = hal_i2c_write_read(addr, &reg, 1, b, 2);
    if (e == HAL_I2C_OK) {
        *value = (uint16_t)((b[0] << 8) | b[1]);
    }
    return e;
}

hal_i2c_err_t hal_i2c_reg_write16_be(uint8_t addr, uint8_t reg, uint16_t value) {
    const uint8_t buf[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return hal_i2c_write(addr, buf, sizeof buf);
}

hal_i2c_err_t hal_i2c_reg_read_burst(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
    return hal_i2c_write_read(addr, &reg, 1, buf, len);
}

hal_i2c_err_t hal_i2c_reg_write_burst(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t len) {
    if (!buf || len == 0 || len > 32) {
        return HAL_I2C_BAD_PARAM;
    }
    uint8_t tmp[33];
    tmp[0] = reg;
    memcpy(&tmp[1], buf, len);
    return hal_i2c_write(addr, tmp, len + 1);
}

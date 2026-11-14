// SPDX-License-Identifier: MPL-2.0
/*
 * hal_i2c on the board's one I2C bus (board.c creates it; the touch HAL uses it too). The i2c_master driver wants a device handle
 * per address: they are created on first use and kept (at most HANDLES; a bus scan probes without handles). The driver locks the
 * bus for each transaction, so the touch task and the battery/haptic checks do not corrupt each other; the runtime's I2C mutex
 * still serialises the app-level sequences as on the other boards.
 */
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "hal_i2c.h"

#define HANDLES 8
#define XFER_TIMEOUT_MS 50

static struct {
    uint8_t addr;
    i2c_master_dev_handle_t dev;
} g_dev[HANDLES];
static uint32_t g_baud = 400000;
static hal_i2c_stats_t g_stats;

hal_i2c_err_t hal_i2c_init(uint32_t bus_hz) {
    if (board_i2c_bus() == NULL) {
        return HAL_I2C_NOT_INIT;
    }
    g_baud = bus_hz; // the bus already runs (board_init); device handles carry the speed they were created with
    return HAL_I2C_OK;
}

void hal_i2c_deinit(void) {} // the bus is shared with the touch controller and stays up

uint32_t hal_i2c_get_baud(void) { return g_baud; }

hal_i2c_err_t hal_i2c_set_baud(uint32_t bus_hz) {
    g_baud = bus_hz;
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_bus_state(bool *sda_high, bool *scl_high) {
    if (board_i2c_bus() == NULL) {
        return HAL_I2C_NOT_INIT;
    }
    if (sda_high != NULL) {
        *sda_high = gpio_get_level(PIN_I2C_SDA) != 0;
    }
    if (scl_high != NULL) {
        *scl_high = gpio_get_level(PIN_I2C_SCL) != 0;
    }
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_recover(void) {
    g_stats.recoveries++;
    if (board_i2c_bus() == NULL) {
        return HAL_I2C_NOT_INIT;
    }
    if (i2c_master_bus_reset(board_i2c_bus()) != ESP_OK) {
        g_stats.recovery_failures++;
        return HAL_I2C_BUS_STUCK;
    }
    return HAL_I2C_OK;
}

static hal_i2c_err_t map_err(esp_err_t e, bool probe) {
    switch (e) {
    case ESP_OK: return HAL_I2C_OK;
    case ESP_ERR_TIMEOUT:
        g_stats.timeouts++;
        return HAL_I2C_TIMEOUT;
    case ESP_ERR_INVALID_ARG: return HAL_I2C_BAD_PARAM;
    default: // ESP_FAIL / NOT_FOUND: no acknowledge (the driver does not say whether it was the address or a data byte)
        if (probe) {
            g_stats.probe_nacks++;
        } else {
            g_stats.nack_addr++;
        }
        return HAL_I2C_NACK_ADDR;
    }
}

static i2c_master_dev_handle_t handle_for(uint8_t addr) {
    int free_slot = -1;
    for (int i = 0; i < HANDLES; i++) {
        if (g_dev[i].dev != NULL && g_dev[i].addr == addr) {
            return g_dev[i].dev;
        }
        if (g_dev[i].dev == NULL && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return NULL;
    }
    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = g_baud};
    i2c_master_dev_handle_t d = NULL;
    if (i2c_master_bus_add_device(board_i2c_bus(), &dc, &d) != ESP_OK) {
        return NULL;
    }
    g_dev[free_slot].addr = addr;
    g_dev[free_slot].dev = d;
    return d;
}

static hal_i2c_err_t check(uint8_t addr, const void *p, size_t len) {
    if (board_i2c_bus() == NULL) {
        return HAL_I2C_NOT_INIT;
    }
    if (addr < HAL_I2C_ADDR_FIRST || addr > HAL_I2C_ADDR_LAST || p == NULL || len == 0 || len > HAL_I2C_MAX_XFER) {
        return HAL_I2C_BAD_PARAM;
    }
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_probe(uint8_t addr) {
    if (board_i2c_bus() == NULL) {
        return HAL_I2C_NOT_INIT;
    }
    if (addr < HAL_I2C_ADDR_FIRST || addr > HAL_I2C_ADDR_LAST) {
        return HAL_I2C_BAD_PARAM;
    }
    g_stats.transactions++;
    return map_err(i2c_master_probe(board_i2c_bus(), addr, XFER_TIMEOUT_MS), true);
}

hal_i2c_err_t hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    hal_i2c_err_t r = check(addr, data, len);
    if (r != HAL_I2C_OK) {
        return r;
    }
    i2c_master_dev_handle_t d = handle_for(addr);
    if (d == NULL) {
        return HAL_I2C_BAD_PARAM;
    }
    g_stats.transactions++;
    return map_err(i2c_master_transmit(d, data, len, XFER_TIMEOUT_MS), false);
}

hal_i2c_err_t hal_i2c_read(uint8_t addr, uint8_t *data, size_t len) {
    hal_i2c_err_t r = check(addr, data, len);
    if (r != HAL_I2C_OK) {
        return r;
    }
    i2c_master_dev_handle_t d = handle_for(addr);
    if (d == NULL) {
        return HAL_I2C_BAD_PARAM;
    }
    g_stats.transactions++;
    return map_err(i2c_master_receive(d, data, len, XFER_TIMEOUT_MS), false);
}

hal_i2c_err_t hal_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen) {
    hal_i2c_err_t r = check(addr, wdata, wlen);
    if (r == HAL_I2C_OK) {
        r = check(addr, rdata, rlen);
    }
    if (r != HAL_I2C_OK) {
        return r;
    }
    i2c_master_dev_handle_t d = handle_for(addr);
    if (d == NULL) {
        return HAL_I2C_BAD_PARAM;
    }
    g_stats.transactions++;
    return map_err(i2c_master_transmit_receive(d, wdata, wlen, rdata, rlen, XFER_TIMEOUT_MS), false);
}

void hal_i2c_get_stats(hal_i2c_stats_t *out) { *out = g_stats; }
void hal_i2c_reset_stats(void) { memset(&g_stats, 0, sizeof g_stats); }

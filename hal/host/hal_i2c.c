// SPDX-License-Identifier: MPL-2.0
// Host hal_i2c: a simulated bus with fake devices (see hal_i2c_mock.h). No timing is simulated.
#include <string.h>

#include "hal_i2c.h"
#include "hal_i2c_mock.h"

#define MAX_DEVICES 8

typedef struct {
    bool used;
    uint8_t addr;
    hal_i2c_mock_ops_t ops;
    uint8_t *regs; // register-map model if non-NULL
    size_t nregs;
    size_t ptr;
    hal_i2c_mock_reg_write_cb on_write;
    void *cb_ctx;
} device_t;

static device_t s_dev[MAX_DEVICES];
static bool s_init;
static uint32_t s_baud;
static hal_i2c_stats_t s_stats;
static hal_i2c_err_t s_inject_err;
static uint32_t s_inject_count;
static uint32_t s_stuck_pulses; // 0 = SDA free
static uint32_t s_mock_txns;

// --- mock control --------------------------------------------------------------------------

static device_t *find(uint8_t addr) {
    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (s_dev[i].used && s_dev[i].addr == addr) {
            return &s_dev[i];
        }
    }
    return NULL;
}

static device_t *alloc_dev(uint8_t addr) {
    if (addr > 0x7Fu || find(addr)) {
        return NULL;
    }
    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (!s_dev[i].used) {
            memset(&s_dev[i], 0, sizeof s_dev[i]);
            s_dev[i].used = true;
            s_dev[i].addr = addr;
            return &s_dev[i];
        }
    }
    return NULL;
}

bool hal_i2c_mock_add_device(uint8_t addr, const hal_i2c_mock_ops_t *ops) {
    device_t *d = ops ? alloc_dev(addr) : NULL;
    if (!d) {
        return false;
    }
    d->ops = *ops;
    return true;
}

bool hal_i2c_mock_add_regmap(uint8_t addr, uint8_t *regs, size_t nregs,
                             hal_i2c_mock_reg_write_cb on_write, void *ctx) {
    device_t *d = regs ? alloc_dev(addr) : NULL;
    if (!d) {
        return false;
    }
    d->regs = regs;
    d->nregs = nregs;
    d->on_write = on_write;
    d->cb_ctx = ctx;
    return true;
}

void hal_i2c_mock_reset(void) {
    memset(s_dev, 0, sizeof s_dev);
    s_stats = (hal_i2c_stats_t){0};
    s_inject_err = HAL_I2C_OK;
    s_inject_count = 0;
    s_stuck_pulses = 0;
    s_mock_txns = 0;
}

void hal_i2c_mock_inject_error(hal_i2c_err_t err, uint32_t count) {
    s_inject_err = err;
    s_inject_count = count;
}

void hal_i2c_mock_set_stuck_sda(uint32_t pulses_needed) { s_stuck_pulses = pulses_needed; }

uint32_t hal_i2c_mock_transaction_count(void) { return s_mock_txns; }

// --- device model --------------------------------------------------------------------------

static hal_i2c_err_t dev_write(device_t *d, const uint8_t *data, size_t len) {
    if (!d->regs) {
        return d->ops.write ? d->ops.write(d->ops.ctx, data, len) : HAL_I2C_NACK_DATA;
    }
    d->ptr = data[0];
    for (size_t i = 1; i < len; i++, d->ptr++) {
        if (d->ptr < d->nregs) {
            d->regs[d->ptr] = data[i];
            if (d->on_write) {
                d->on_write(d->cb_ctx, (uint8_t)d->ptr, data[i]);
            }
        }
    }
    return HAL_I2C_OK;
}

static hal_i2c_err_t dev_read(device_t *d, uint8_t *data, size_t len) {
    if (!d->regs) {
        return d->ops.read ? d->ops.read(d->ops.ctx, data, len) : HAL_I2C_NACK_DATA;
    }
    for (size_t i = 0; i < len; i++, d->ptr++) {
        data[i] = d->ptr < d->nregs ? d->regs[d->ptr] : 0xFF;
    }
    return HAL_I2C_OK;
}

// --- bus ------------------------------------------------------------------------------------

// Count a finished transaction; mirrors the device backend's accounting.
static hal_i2c_err_t finish(hal_i2c_err_t e, bool is_probe) {
    s_stats.transactions++;
    switch (e) {
    case HAL_I2C_OK: break;
    case HAL_I2C_NACK_ADDR:
        if (is_probe) {
            s_stats.probe_nacks++;
        } else {
            s_stats.nack_addr++;
        }
        break;
    case HAL_I2C_NACK_DATA: s_stats.nack_data++; break;
    case HAL_I2C_TIMEOUT: s_stats.timeouts++; break;
    case HAL_I2C_ARB_LOST: s_stats.arb_lost++; break;
    case HAL_I2C_BUS_STUCK: s_stats.bus_stuck++; break;
    default: break;
    }
    return e;
}

static hal_i2c_err_t take_injected(void) {
    if (s_inject_count > 0) {
        s_inject_count--;
        return s_inject_err;
    }
    return HAL_I2C_OK;
}

static hal_i2c_err_t xfer(uint8_t addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen,
                          bool is_probe) {
    s_mock_txns++;
    hal_i2c_err_t inj = take_injected();
    if (inj != HAL_I2C_OK) {
        return finish(inj, is_probe);
    }
    if (s_stuck_pulses) { // a held-low SDA wedges the transaction until recovered
        return finish(HAL_I2C_TIMEOUT, is_probe);
    }
    device_t *d = find(addr);
    if (!d) {
        return finish(HAL_I2C_NACK_ADDR, is_probe);
    }
    hal_i2c_err_t e = HAL_I2C_OK;
    if (wlen) {
        e = dev_write(d, w, wlen);
    }
    if (e == HAL_I2C_OK && rlen) {
        e = dev_read(d, r, rlen);
    }
    return finish(e, is_probe);
}

hal_i2c_err_t hal_i2c_init(uint32_t bus_hz) {
    if (bus_hz < 10000u || bus_hz > 1000000u) {
        return HAL_I2C_BAD_PARAM;
    }
    s_init = true;
    s_baud = bus_hz;
    s_stats = (hal_i2c_stats_t){0};
    if (s_stuck_pulses) {
        return hal_i2c_recover();
    }
    return HAL_I2C_OK;
}

void hal_i2c_deinit(void) {
    s_init = false;
    s_baud = 0;
}

uint32_t hal_i2c_get_baud(void) { return s_init ? s_baud : 0; }

hal_i2c_err_t hal_i2c_set_baud(uint32_t bus_hz) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    if (bus_hz < 10000u || bus_hz > 1000000u) {
        return HAL_I2C_BAD_PARAM;
    }
    s_baud = bus_hz;
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_bus_state(bool *sda_high, bool *scl_high) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    if (sda_high) {
        *sda_high = s_stuck_pulses == 0;
    }
    if (scl_high) {
        *scl_high = true;
    }
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_recover(void) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    if (s_stuck_pulses == 0) {
        return HAL_I2C_OK;
    }
    s_stats.recoveries++;
    if (s_stuck_pulses <= 9u) {
        s_stuck_pulses = 0;
        return HAL_I2C_OK;
    }
    s_stats.recovery_failures++;
    return HAL_I2C_BUS_STUCK;
}

static hal_i2c_err_t check(uint8_t addr, const void *p, size_t len) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    bool reserved = (addr & 0x78u) == 0 || (addr & 0x78u) == 0x78u; // 0x00-0x07, 0x78-0x7F
    return (reserved || addr > 0x7Fu || !p || len == 0 || len > HAL_I2C_MAX_XFER) ? HAL_I2C_BAD_PARAM
                                                                                 : HAL_I2C_OK;
}

// Timeouts and stuck lines get the same treatment as on the device: recover, then report.
static hal_i2c_err_t after(hal_i2c_err_t e) {
    if (e == HAL_I2C_TIMEOUT && s_stuck_pulses && hal_i2c_recover() != HAL_I2C_OK) {
        s_stats.bus_stuck++;
        return HAL_I2C_BUS_STUCK;
    }
    return e;
}

hal_i2c_err_t hal_i2c_probe(uint8_t addr) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    if (addr < HAL_I2C_ADDR_FIRST || addr > HAL_I2C_ADDR_LAST) {
        return HAL_I2C_BAD_PARAM;
    }
    uint8_t b;
    return after(xfer(addr, NULL, 0, &b, 1, true));
}

hal_i2c_err_t hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    hal_i2c_err_t e = check(addr, data, len);
    if (e != HAL_I2C_OK) {
        return e;
    }
    return after(xfer(addr, data, len, NULL, 0, false));
}

hal_i2c_err_t hal_i2c_read(uint8_t addr, uint8_t *data, size_t len) {
    hal_i2c_err_t e = check(addr, data, len);
    if (e != HAL_I2C_OK) {
        return e;
    }
    return after(xfer(addr, NULL, 0, data, len, false));
}

hal_i2c_err_t hal_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata,
                                 size_t rlen) {
    hal_i2c_err_t e = check(addr, wdata, wlen);
    if (e == HAL_I2C_OK) {
        e = check(addr, rdata, rlen);
    }
    if (e != HAL_I2C_OK) {
        return e;
    }
    return after(xfer(addr, wdata, wlen, rdata, rlen, false));
}

void hal_i2c_get_stats(hal_i2c_stats_t *out) {
    if (out) {
        *out = s_stats;
    }
}

void hal_i2c_reset_stats(void) { s_stats = (hal_i2c_stats_t){0}; }

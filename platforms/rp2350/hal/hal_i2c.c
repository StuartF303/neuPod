// SPDX-License-Identifier: MPL-2.0
// hal_i2c on RP2350 I2C0: SDA GP20, SCL GP21. See hal/include/hal_i2c.h for the contract.
#include "hal_i2c.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/time.h"

#define I2C_INST i2c0
#define PIN_SDA 20u
#define PIN_SCL 21u

#define BAUD_MIN 10000u
#define BAUD_MAX 1000000u
#define RECOVERY_HALF_PERIOD_US 5u    // ~100 kHz pulses
#define RECOVERY_SCL_STRETCH_US 1000u // how long SCL may stay low after release before giving up
#define RECOVERY_MAX_PULSES 9u
#define LINES_SETTLE_US 100u // after a failed transfer the controller still has to issue its STOP

static bool s_init;
static uint s_baud; // actual baud
static hal_i2c_stats_t s_stats;

// Both breakouts have their own pull-ups. The RP2350 pad reset state is pull-DOWN enabled (and
// the pad isolated), so the internal pulls must be switched off explicitly: a ~50 kOhm pull-down
// in parallel with the breakout pull-ups would drag the idle level down. gpio_set_function()
// also clears the RP2350 pad isolation bit (SDK 2.x, HAS_PADS_BANK0_ISOLATION), so the pins are
// live afterwards; pulls are disabled first so there is no window with a pull-down on a live pad.
static void pins_to_i2c(void) {
    gpio_disable_pulls(PIN_SDA);
    gpio_disable_pulls(PIN_SCL);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
}

// Pins as plain GPIO inputs (= released) with output latch 0, so toggling direction pulls the
// line low / releases it. Never drives high: the external pull-ups do that.
static void pins_to_gpio_released(void) {
    for (uint pin = PIN_SDA; pin <= PIN_SCL; pin++) {
        gpio_disable_pulls(pin);
        gpio_init(pin); // SIO function, input, output latch 0
    }
}

static bool sda_high(void) { return gpio_get(PIN_SDA); }
static bool scl_high(void) { return gpio_get(PIN_SCL); }

// Release SCL and wait for it to actually go high (a slave may be stretching).
static bool scl_release_wait(void) {
    gpio_set_dir(PIN_SCL, GPIO_IN);
    for (uint i = 0; i < RECOVERY_SCL_STRETCH_US; i++) {
        if (scl_high()) {
            return true;
        }
        busy_wait_us(1);
    }
    return false;
}

// Wait (bounded) for both lines to be high. A NACK is a normal answer: the controller aborts
// and releases the bus by itself, so lines that are momentarily low right after it must not
// trigger recovery. Only lines still low after the settle time count as stuck.
static bool lines_settle(void) {
    for (uint i = 0; i < LINES_SETTLE_US; i++) {
        if (sda_high() && scl_high()) {
            return true;
        }
        busy_wait_us(1);
    }
    return sda_high() && scl_high();
}

static void hw_restart(uint hz) {
    i2c_deinit(I2C_INST); // resets the block: flushes FIFOs and any half-finished transfer
    s_baud = i2c_init(I2C_INST, hz);
    pins_to_i2c();
}

static hal_i2c_err_t recover_bitbang(void) {
    s_stats.recoveries++;
    uint want = s_baud;
    i2c_deinit(I2C_INST);
    pins_to_gpio_released();

    bool ok = false;
    if (scl_release_wait()) {
        if (sda_high()) {
            ok = true;
        } else {
            for (uint i = 0; i < RECOVERY_MAX_PULSES && !sda_high(); i++) {
                gpio_set_dir(PIN_SCL, GPIO_OUT); // SCL low
                busy_wait_us(RECOVERY_HALF_PERIOD_US);
                if (!scl_release_wait()) {
                    break;
                }
                busy_wait_us(RECOVERY_HALF_PERIOD_US);
            }
            if (sda_high() && scl_high()) {
                // STOP: SDA low -> high while SCL is high.
                gpio_set_dir(PIN_SDA, GPIO_OUT);
                busy_wait_us(RECOVERY_HALF_PERIOD_US);
                gpio_set_dir(PIN_SDA, GPIO_IN);
                busy_wait_us(RECOVERY_HALF_PERIOD_US);
                ok = sda_high() && scl_high();
            }
        }
    }
    s_baud = i2c_init(I2C_INST, want);
    pins_to_i2c();
    if (!ok) {
        s_stats.recovery_failures++;
    }
    return ok ? HAL_I2C_OK : HAL_I2C_BUS_STUCK;
}

hal_i2c_err_t hal_i2c_recover(void) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    // Reset the controller first (clears a wedged transfer); bit-bang only if a line is low.
    hw_restart(s_baud);
    if (lines_settle()) {
        return HAL_I2C_OK;
    }
    return recover_bitbang();
}

hal_i2c_err_t hal_i2c_init(uint32_t bus_hz) {
    if (bus_hz < BAUD_MIN || bus_hz > BAUD_MAX) {
        return HAL_I2C_BAD_PARAM;
    }
    s_baud = i2c_init(I2C_INST, bus_hz); // also takes the block out of reset
    pins_to_i2c();
    s_init = true;
    s_stats = (hal_i2c_stats_t){0};
    if (!lines_settle()) { // give a slow pull-up / powering-up device a moment before declaring stuck
        return recover_bitbang();
    }
    return HAL_I2C_OK;
}

void hal_i2c_deinit(void) {
    if (s_init) {
        i2c_deinit(I2C_INST);
    }
    pins_to_gpio_released();
    s_init = false;
    s_baud = 0;
}

uint32_t hal_i2c_get_baud(void) { return s_init ? s_baud : 0; }

hal_i2c_err_t hal_i2c_set_baud(uint32_t bus_hz) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    if (bus_hz < BAUD_MIN || bus_hz > BAUD_MAX) {
        return HAL_I2C_BAD_PARAM;
    }
    s_baud = i2c_set_baudrate(I2C_INST, bus_hz);
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_bus_state(bool *sda, bool *scl) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    if (sda) {
        *sda = sda_high();
    }
    if (scl) {
        *scl = scl_high();
    }
    return HAL_I2C_OK;
}

// Budget for @p nbytes on the wire (address byte included): 9 clocks per byte plus START/STOP,
// doubled for margin, never below the floor.
static uint timeout_us(size_t nbytes) {
    uint64_t bits = (uint64_t)(nbytes + 1u) * 9u + 2u;
    uint64_t us = 2u * bits * 1000000u / (s_baud ? s_baud : BAUD_MIN);
    return us < HAL_I2C_TIMEOUT_FLOOR_US ? HAL_I2C_TIMEOUT_FLOOR_US : (uint)us;
}

// Translate an SDK result into the HAL error (and counters), recovering the bus where needed.
// @p done is the number of bytes the call was expected to transfer. The SDK reports an address
// NACK as PICO_ERROR_GENERIC and a data NACK as a short byte count; it clears the abort-source
// register itself, so arbitration loss is indistinguishable from a NACK here (a lost arbitration
// with a line stuck low still ends up as BUS_STUCK via the line check below).
static hal_i2c_err_t finish(int r, size_t done, bool is_probe) {
    s_stats.transactions++;
    if (r == (int)done) {
        return HAL_I2C_OK;
    }
    hal_i2c_err_t e;
    if (r == PICO_ERROR_TIMEOUT) {
        s_stats.timeouts++;
        e = HAL_I2C_TIMEOUT;
    } else if (r >= 0 && (size_t)r < done) {
        s_stats.nack_data++;
        e = HAL_I2C_NACK_DATA;
    } else if (is_probe) {
        s_stats.probe_nacks++;
        e = HAL_I2C_NACK_ADDR;
    } else {
        s_stats.nack_addr++;
        e = HAL_I2C_NACK_ADDR;
    }
    I2C_INST->restart_on_next = false; // a failed nostop write must not leak a RESTART
    if (e == HAL_I2C_TIMEOUT) {
        hw_restart(s_baud); // abandoned transfer may leave the FIFOs/state machine dirty
    }
    // NACKs never reset the controller or start recovery; only a line that stays low does.
    if (!lines_settle()) {
        if (recover_bitbang() != HAL_I2C_OK) {
            s_stats.bus_stuck++;
            return HAL_I2C_BUS_STUCK;
        }
    }
    return e;
}

static hal_i2c_err_t check(uint8_t addr, const void *p, size_t len) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    if (addr > 0x7Fu || (addr & 0x78u) == 0 || (addr & 0x78u) == 0x78u || !p || len == 0 ||
        len > HAL_I2C_MAX_XFER) { // reserved ranges 0x00-0x07 / 0x78-0x7F
        return HAL_I2C_BAD_PARAM;
    }
    return HAL_I2C_OK;
}

hal_i2c_err_t hal_i2c_probe(uint8_t addr) {
    if (!s_init) {
        return HAL_I2C_NOT_INIT;
    }
    if (addr < HAL_I2C_ADDR_FIRST || addr > HAL_I2C_ADDR_LAST) {
        return HAL_I2C_BAD_PARAM;
    }
    uint8_t b;
    return finish(i2c_read_timeout_us(I2C_INST, addr, &b, 1, false, timeout_us(1)), 1, true);
}

hal_i2c_err_t hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    hal_i2c_err_t e = check(addr, data, len);
    if (e != HAL_I2C_OK) {
        return e;
    }
    return finish(i2c_write_timeout_us(I2C_INST, addr, data, len, false, timeout_us(len)), len, false);
}

hal_i2c_err_t hal_i2c_read(uint8_t addr, uint8_t *data, size_t len) {
    hal_i2c_err_t e = check(addr, data, len);
    if (e != HAL_I2C_OK) {
        return e;
    }
    return finish(i2c_read_timeout_us(I2C_INST, addr, data, len, false, timeout_us(len)), len, false);
}

hal_i2c_err_t hal_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata,
                                 size_t rlen) {
    hal_i2c_err_t e = check(addr, wdata, wlen);
    if (e != HAL_I2C_OK) {
        return e;
    }
    e = check(addr, rdata, rlen);
    if (e != HAL_I2C_OK) {
        return e;
    }
    // nostop on the write -> the read starts with a repeated START.
    e = finish(i2c_write_timeout_us(I2C_INST, addr, wdata, wlen, true, timeout_us(wlen)), wlen, false);
    if (e != HAL_I2C_OK) {
        return e;
    }
    s_stats.transactions--; // one logical transaction, not two
    return finish(i2c_read_timeout_us(I2C_INST, addr, rdata, rlen, false, timeout_us(rlen)), rlen, false);
}

void hal_i2c_get_stats(hal_i2c_stats_t *out) {
    if (out) {
        *out = s_stats;
    }
}

void hal_i2c_reset_stats(void) { s_stats = (hal_i2c_stats_t){0}; }

// SPDX-License-Identifier: MPL-2.0
// Host i2c_test: the same menu as the device, over the simulated bus with fake devices at 0x36
// (MAX17048) and 0x5A (DRV2605L).
#include <stdio.h>

#include "hal_i2c_mock.h"
#include "hal_system.h"
#include "i2c_tests.h"

static uint8_t s_gauge_regs[0x100];
static uint8_t s_haptic_regs[0x80];

// Minimal DRV2605L behaviour: DEV_RESET self-clears; GO self-clears at once (auto-cal also writes results).
static void haptic_on_write(void *ctx, uint8_t reg, uint8_t value) {
    (void)ctx;
    if (reg == 0x01 && (value & 0x80)) {
        s_haptic_regs[0x01] = 0x40;
        s_haptic_regs[0x1D] = 0xA0;
    } else if (reg == 0x0C && (value & 1)) {
        if ((s_haptic_regs[0x01] & 7) == 7) {
            s_haptic_regs[0x18] = 0x0D;
            s_haptic_regs[0x19] = 0x7B;
        }
        s_haptic_regs[0x0C] = 0;
    }
}

int main(void) {
    if (hal_system_init() != HAL_OK) {
        fprintf(stderr, "i2c_test: init failed\n");
        return 1;
    }
    // MAX17048: VCELL 0x02 = 3.9 V (78 uV/LSB -> 0xC3 0x50), SOC 0x04 = 50 %, VERSION 0x08 = 0x0012.
    s_gauge_regs[0x02] = 0xC3;
    s_gauge_regs[0x03] = 0x50;
    s_gauge_regs[0x04] = 50;
    s_gauge_regs[0x08] = 0x00;
    s_gauge_regs[0x09] = 0x12;
    // DRV2605L: STATUS 0x00 device ID 7 (bits 7:5) = DRV2605L.
    s_haptic_regs[0x00] = 0xE0;
    hal_i2c_mock_add_regmap(0x36, s_gauge_regs, sizeof s_gauge_regs, NULL, NULL);
    hal_i2c_mock_add_regmap(0x5A, s_haptic_regs, sizeof s_haptic_regs, haptic_on_write, NULL);
    i2c_tests_run_menu(); // exits on stdin EOF or Ctrl-C
    return 0;
}

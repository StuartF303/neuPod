// SPDX-License-Identifier: MPL-2.0
// i2c_test: interactive I2C test menu (fuel gauge + haptic driver) on the UART console.
#include <stdio.h>

#include "hal_system.h"
#include "i2c_tests.h"

int main(void) {
    if (hal_system_init() != HAL_OK) {
        printf("i2c_test: hal_system_init failed\n");
        return -1;
    }
    printf("i2c_test: starting (stage 1: bus scan)\n");
    hal_status_led(true);
    i2c_tests_run_menu(); // never returns
    return 0;
}

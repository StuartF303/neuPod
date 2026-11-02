// SPDX-License-Identifier: MPL-2.0
// display_test: interactive ST7789 test menu on the UART console (see docs/display-tests.md).
#include <stdio.h>

#include "display_tests.h"
#include "hal_display.h"
#include "hal_system.h"

int main(void) {
    if (hal_system_init() != HAL_OK) {
        printf("display_test: hal_system_init failed\n");
        return -1;
    }
    printf("display_test: starting\n");

    hal_status_t st = hal_display_init();
    if (st != HAL_OK) {
        printf("display_test: hal_display_init failed (%d)\n", (int)st);
        return -1;
    }
    hal_display_backlight(255);
    hal_status_led(true);
    display_tests_run_menu(); // never returns
    return 0;
}

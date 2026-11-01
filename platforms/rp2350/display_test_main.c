// SPDX-License-Identifier: MPL-2.0
// display_test: colour bars + gradient on the ST7789, then blink the status LED.
#include <stdio.h>

#include "display_test.h"
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
    st = display_test_draw();
    printf("display_test: draw %s (%d)\n", st == HAL_OK ? "ok" : "failed", (int)st);
    hal_display_backlight(255);

    bool led = false;
    for (;;) {
        led = !led;
        hal_status_led(led);
        hal_delay_ms(500);
    }
}

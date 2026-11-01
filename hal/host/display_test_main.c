// SPDX-License-Identifier: MPL-2.0
// Host display_test: draws the portable test pattern; with SDL it stays up until closed,
// headless it draws once (set NEUPOD_DISPLAY_PPM=out.ppm to capture the frame).
#include <stdio.h>

#include "display_test.h"
#include "hal_display.h"
#include "hal_system.h"

int main(void) {
    if (hal_system_init() != HAL_OK || hal_display_init() != HAL_OK) {
        fprintf(stderr, "display_test: init failed\n");
        return 1;
    }
    hal_display_backlight(255);
    if (display_test_draw() != HAL_OK) {
        fprintf(stderr, "display_test: draw failed\n");
        return 1;
    }
    printf("display_test: pattern drawn\n");
#ifdef NEUPOD_HOST_SDL
    for (;;) {
        hal_display_present();
        hal_delay_ms(50); // pumps events; exits when the window is closed
    }
#endif
    return 0;
}

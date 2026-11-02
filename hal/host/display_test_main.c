// SPDX-License-Identifier: MPL-2.0
// Host display_test: the same interactive menu as the device, keys from the terminal.
// Optional: NEUPOD_DISPLAY_PPM=out.ppm captures each presented frame (works headless).
#include <stdio.h>

#include "display_tests.h"
#include "hal_display.h"
#include "hal_system.h"

int main(void) {
    if (hal_system_init() != HAL_OK || hal_display_init() != HAL_OK) {
        fprintf(stderr, "display_test: init failed\n");
        return 1;
    }
    hal_display_backlight(255);
    display_tests_run_menu(); // exits on stdin EOF, Ctrl-C or window close
    return 0;
}

// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "hal_system.h"
#include "neupod_app.h"

void neupod_app_run(void) {
    printf("neuPod %s starting\n", hal_platform_name());

    bool led = false;
    for (;;) {
        led = !led;
        hal_status_led(led);
        printf("neuPod alive\n");
        hal_delay_ms(1000);
    }
}

// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "hal_system.h"
#include "neupod_app.h"

void app_main(void) {
    if (hal_system_init() != HAL_OK) {
        printf("neuPod: hal_system_init failed\n");
        return;
    }
    neupod_app_run();
}

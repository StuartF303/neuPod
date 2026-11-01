// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "hal_system.h"
#include "neupod_app.h"

int main(void) {
    if (hal_system_init() != HAL_OK) {
        printf("neuPod: hal_system_init failed\n");
        return -1;
    }
    neupod_app_run();
    return 0;
}

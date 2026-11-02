// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "hal_system.h"
#include "runtime.h"

int main(void) {
    if (hal_system_init() != HAL_OK) {
        printf("neuPod: hal_system_init failed\n");
        return -1;
    }
    neupod_runtime_config_t cfg = {.console_keys = true}; // UART keys are input events (docs/runtime.md key map)
    neupod_runtime_run(&cfg); // launches the audio core, starts FreeRTOS on core 0; does not return
    return 0;
}

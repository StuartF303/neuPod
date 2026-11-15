// SPDX-License-Identifier: MPL-2.0
// Entry point on ESP-IDF: app_main() is a task of the already-running scheduler, so the runtime's os_start() just retires it.
#include <stdio.h>

#include "hal_system.h"
#include "runtime.h"

void app_main(void) {
    if (hal_system_init() != HAL_OK) {
        printf("neuPod: hal_system_init failed\n");
        return;
    }
    neupod_runtime_config_t cfg = {.console_keys = true}; // keys typed on the USB console are input events (docs/runtime.md key map)
    neupod_runtime_run(&cfg);                              // creates the tasks and the audio core; does not return
}

// SPDX-License-Identifier: MPL-2.0
// storage_test: interactive microSD test menu on the UART console. Stage 1 is read-only.
#include <stdio.h>

#include "hal_system.h"
#include "storage_tests.h"

int main(void) {
    if (hal_system_init() != HAL_OK) {
        printf("storage_test: hal_system_init failed\n");
        return -1;
    }
    printf("storage_test: starting (stage 1, read-only)\n");
    hal_status_led(true);
    storage_tests_run_menu(); // never returns
    return 0;
}

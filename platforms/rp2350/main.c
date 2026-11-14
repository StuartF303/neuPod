// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "hal_system.h"
#include "runtime.h"

#ifdef NEUPOD_BOARD_AMOLED241
#include "board.h"
#include "hal_storage.h"
#include "storage_service.h"

// Power-off on this board is the battery switch's soft latch (hal_system_poweroff): let the card finish what it is writing first, as the
// runtime's orderly stop does (app/runtime/runtime.c shutdown_now). Bounded, so a stuck task cannot keep the board from switching off.
static void poweroff_prepare(void) {
    uint32_t t0 = hal_millis();
    while (!storage_service_idle() && hal_millis() - t0 < 2000u) {
        hal_delay_ms(20);
    }
    hal_storage_set_write_enabled(false);
    (void)hal_storage_sync();
}
#endif

int main(void) {
    if (hal_system_init() != HAL_OK) {
        printf("neuPod: hal_system_init failed\n");
        return -1;
    }
#ifdef NEUPOD_BOARD_AMOLED241
    amoled241_set_poweroff_prepare(poweroff_prepare);
#endif
    neupod_runtime_config_t cfg = {.console_keys = true}; // UART keys are input events (docs/runtime.md key map)
    neupod_runtime_run(&cfg); // launches the audio core, starts FreeRTOS on core 0; does not return
    return 0;
}

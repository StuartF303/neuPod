// SPDX-License-Identifier: MPL-2.0
/*
 * The audio core on the ESP32-S3. The RP2350's "core 1 runs bare metal" design (ADR-0011) is kept in shape, not in letter: the
 * S3's FreeRTOS is SMP, so core 1 here is one task pinned to the second CPU (nothing else of ours is ever scheduled there) and
 * the block "interrupt" is a higher-priority task on that same CPU (hal_audio.c), which preempts the decoder exactly as the
 * interrupt does on the RP2350. All RTOS tasks of the runtime are pinned to core 0 (app/os/os_freertos.c).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_core.h"

// The MP3 decoder alone keeps about 10 KB of scratch on the stack (minimp3), FLAC and ALAC a few KB (same sizing as the RP2350).
#ifndef NEUPOD_CORE1_STACK_BYTES
#define NEUPOD_CORE1_STACK_BYTES 20480u
#endif
#define CORE1_TASK_PRIORITY 5 /* below the block clock task (hal_audio.c), above the idle task */

static TaskHandle_t g_task;
static void (*g_entry)(void);

static void core1_task(void *arg) {
    (void)arg;
    g_entry(); // never returns
    vTaskDelete(NULL);
}

size_t hal_core1_stack_free_min(void) {
    return g_task != NULL ? (size_t)uxTaskGetStackHighWaterMark(g_task) : 0; // bytes on this port
}

hal_status_t hal_core1_launch(void (*entry)(void)) {
    if (entry == NULL) {
        return HAL_EINVAL;
    }
    if (g_task != NULL) {
        return HAL_EBUSY;
    }
    g_entry = entry;
    return xTaskCreatePinnedToCore(core1_task, "audio", NEUPOD_CORE1_STACK_BYTES, NULL, CORE1_TASK_PRIORITY, &g_task, 1) == pdPASS
               ? HAL_OK
               : HAL_ERR;
}

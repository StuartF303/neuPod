// SPDX-License-Identifier: MPL-2.0
/**
 * @file memory_budget.h
 * @brief The big static-RAM consumers of the RP2350 build, as named constants (docs/runtime.md, docs/bluetooth.md).
 *
 * Plain macros only, so FreeRTOSConfig.h and lv_conf.h can include it. The RP2350 has 512 KB of main SRAM;
 * these three plus the audio core's rings and the BTstack pools are most of it.
 *
 * History (the sizes were trimmed to make room for the BTstack classic stack, stage 1 of docs/bluetooth.md):
 *   FreeRTOS heap  112 KB -> 80 KB   (bench minimum free was ~58 KB of 112 KB: ~54 KB used before the haptic/storage growth)
 *   LVGL pool       96 KB -> 72 KB   (bench peak ~38 KB after navigating every menu; the host tour passes at 72 KB)
 *   codec arena    116 KB unchanged  (app/include/arena.h, CODEC_ARENA_BYTES)
 */
#ifndef NEUPOD_MEMORY_BUDGET_H
#define NEUPOD_MEMORY_BUDGET_H

/** FreeRTOS heap_4 (task stacks, queues, mutexes, the CYW43 worker task). */
#define NEUPOD_RTOS_HEAP_BYTES (80u * 1024u)

/** LVGL's built-in allocator pool (screens, widgets, styles). */
#define NEUPOD_LVGL_POOL_BYTES (72u * 1024u)

#endif // NEUPOD_MEMORY_BUDGET_H

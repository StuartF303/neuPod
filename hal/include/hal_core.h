// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_core.h
 * @brief Launch code on the second core, outside the RTOS.
 *
 * The RTOS (FreeRTOS, single-core) runs on core 0 only. The second core runs bare-metal audio code
 * (app/audio/audio_core.c): no kernel calls, no stdio, no malloc, no SDK primitive that is routed
 * to the kernel (docs/adr/0011-bare-metal-audio-core.md).
 */
#ifndef NEUPOD_HAL_CORE_H
#define NEUPOD_HAL_CORE_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start @p entry on core 1 (RP2350: multicore_launch_core1; host: a native thread). @p entry must
 * not return. Call from core 0 after hal_system_init(); before or after the RTOS scheduler starts.
 */
hal_status_t hal_core1_launch(void (*entry)(void));

/**
 * Smallest amount of core-1 stack (bytes) ever free since launch, measured by painting the stack at
 * start; 0 where unknown (host). Read-only; safe from core 0.
 */
size_t hal_core1_stack_free_min(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_CORE_H

// SPDX-License-Identifier: MPL-2.0
/*
 * FreeRTOS configuration for the host build (kernel POSIX port: one pthread per task, a single
 * "core"). Lets app/ code run unchanged on the PC; these RTOS tasks are the core-0 side, and the
 * bare-metal audio core is emulated with native threads (hal/host/hal_core.c, hal_audio.c).
 * Timing is NOT representative of the RP2350.
 */
#ifndef NEUPOD_HOST_FREERTOS_CONFIG_H
#define NEUPOD_HOST_FREERTOS_CONFIG_H

#define configCPU_CLOCK_HZ 1000000UL
#define configNUMBER_OF_CORES 1
#define configUSE_16_BIT_TICKS 0
#define configUSE_PASSIVE_IDLE_HOOK 0
#define configTOTAL_HEAP_SIZE (4 * 1024 * 1024) /* generous: x86-64 stacks and heap are not the budget */

#define configASSERT_DEFINED 1
#ifndef __ASSEMBLER__
extern void neupod_panic_assert(const char *file, int line);
#endif
#define configASSERT(x) do { if (!(x)) neupod_panic_assert(__FILE__, __LINE__); } while (0)

#ifdef NEUPOD_PLATFORM_Y2
/* On the Y2 (a battery-powered Linux device) the POSIX port's idle task must sleep instead of spinning:
 * platforms/y2/hal/hal_system.c defines vApplicationIdleHook(). */
#define configUSE_IDLE_HOOK 1
#endif

#include "FreeRTOSConfig_common.h"

#endif // NEUPOD_HOST_FREERTOS_CONFIG_H

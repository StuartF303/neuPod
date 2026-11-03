// SPDX-License-Identifier: MPL-2.0
/*
 * FreeRTOS configuration for the RP2350 (Raspberry Pi FreeRTOS-Kernel fork, ARM_CM33_NTZ port,
 * single-core mode on core 0). Design: docs/adr/0009-rtos-freertos-smp.md (amended), docs/adr/0011-bare-metal-audio-core.md, docs/runtime.md.
 */
#ifndef NEUPOD_RP2350_FREERTOS_CONFIG_H
#define NEUPOD_RP2350_FREERTOS_CONFIG_H

/* SysTick runs from the core clock. A literal, not clock_get_hz(): this header is pulled in by the
 * SDK's own config.h, before the SDK headers it would need. Must equal NEUPOD_SYS_CLOCK_KHZ in
 * platforms/rp2350/hal/hal_system.c (125 MHz, set before the scheduler starts). */
#define configCPU_CLOCK_HZ 125000000UL

/* Port requirements for RP2350 (see the port's README): single privilege level, secure state. */
#define configENABLE_MPU 0
#define configENABLE_TRUSTZONE 0
#define configRUN_FREERTOS_SECURE_ONLY 1
#define configENABLE_FPU 1
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 16
#define configKERNEL_INTERRUPT_PRIORITY 255

/* SINGLE-CORE kernel on core 0 (docs/adr/0011-bare-metal-audio-core.md). Core 1 is launched by the
 * runtime as a bare-metal audio core; the kernel never schedules onto it. (The SMP configuration
 * lost cross-core yield requests on the bench, and woke tasks corruptly from a core-1 interrupt.) */
#define configNUMBER_OF_CORES 1
#define configUSE_PASSIVE_IDLE_HOOK 0

/* RP2040-family SDK interop: pico_sync / pico_time block cooperatively under the scheduler. Core 1
 * must therefore avoid those primitives (the core-1 rules); they are for core 0 tasks only. */
#define configSUPPORT_PICO_SYNC_INTEROP 1
#define configSUPPORT_PICO_TIME_INTEROP 1

/* Heap: heap_4, one static array in .bss. Budget in docs/runtime.md; the size is a named constant. */
#include "memory_budget.h"
#define configTOTAL_HEAP_SIZE NEUPOD_RTOS_HEAP_BYTES

/* Run-time-stats clock: the free-running 1 MHz hardware timer (app/os/os_freertos.c). */
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#ifndef __ASSEMBLER__
extern void neupod_rtos_stats_timer_init(void);
extern unsigned long neupod_rtos_stats_timer_get(void);
#endif
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() neupod_rtos_stats_timer_init()
#define portGET_RUN_TIME_COUNTER_VALUE() neupod_rtos_stats_timer_get()

#define configASSERT_DEFINED 1
#ifndef __ASSEMBLER__
extern void neupod_panic_assert(const char *file, int line);
#endif
#define configASSERT(x) do { if (!(x)) neupod_panic_assert(__FILE__, __LINE__); } while (0)

#include "FreeRTOSConfig_common.h"
#include "rp2040_config.h"

#endif // NEUPOD_RP2350_FREERTOS_CONFIG_H

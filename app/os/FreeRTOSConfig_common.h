// SPDX-License-Identifier: MPL-2.0
/*
 * FreeRTOS settings shared by the RP2350 and host (POSIX port) builds. Each platform's
 * FreeRTOSConfig.h sets the core count, heap and port-specific items, then includes this file.
 * ESP-IDF configures its own FreeRTOS through sdkconfig and does not use this header.
 */
#ifndef NEUPOD_FREERTOS_CONFIG_COMMON_H
#define NEUPOD_FREERTOS_CONFIG_COMMON_H

#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE 0
#define configTICK_RATE_HZ 1000 /* 1 ms tick; os_tick_ms() == ticks */
#define configMAX_PRIORITIES 8  /* 0 idle ... 7; see docs/runtime.md */
#define configMINIMAL_STACK_SIZE 256 /* words */
#define configMAX_TASK_NAME_LEN 12
#define configIDLE_SHOULD_YIELD 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configQUEUE_REGISTRY_SIZE 0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configUSE_NEWLIB_REENTRANT 0

/* Allocation: heap_4 only (no static allocation, so the SDK's cyw43 async context can use the
 * dynamic path). Each platform defines configTOTAL_HEAP_SIZE. */
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configAPPLICATION_ALLOCATED_HEAP 0

/* Features used by os.h and the runtime. */
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_QUEUE_SETS 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configUSE_EVENT_GROUPS 1
#define configUSE_STREAM_BUFFERS 0
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 6
#define configTIMER_QUEUE_LENGTH 8
#define configTIMER_TASK_STACK_DEPTH 384
#define configUSE_CO_ROUTINES 0

/* Safety nets (docs/runtime.md, "Failure handling"). Handlers live in app/runtime/panic.c. */
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

/* Task statistics for the monitor task (per-task CPU % and stack high-water marks). */
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configGENERATE_RUN_TIME_STATS 1 /* the platform config supplies the clock (RP2350) or the port does (POSIX) */

/* API subset. */
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle 1
#define INCLUDE_pcTaskGetTaskName 1
#define INCLUDE_xTimerPendFunctionCall 1
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xTaskAbortDelay 0
#define INCLUDE_xSemaphoreGetMutexHolder 1
#define INCLUDE_xTaskGetHandle 0

#endif // NEUPOD_FREERTOS_CONFIG_COMMON_H

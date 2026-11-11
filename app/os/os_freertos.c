// SPDX-License-Identifier: MPL-2.0
/*
 * os.h on FreeRTOS: the Raspberry Pi RP2350 port (single-core, core 0) and the kernel's POSIX port on the host.
 * Also holds the kernel hooks (stack overflow, malloc failure) and the run-time-stats clock,
 * which are part of the same FreeRTOSConfig contract.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "hal_system.h"
#include "os.h"
#include "panic.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

// --- Time ----------------------------------------------------------------------------------

static TickType_t to_ticks(uint32_t ms) {
    if (ms == OS_WAIT_FOREVER) {
        return portMAX_DELAY;
    }
#ifdef NEUPOD_PLATFORM_Y2
    // 250 Hz tick: round up, and never turn a real wait into 0 ticks (pdMS_TO_TICKS(2) would be 0 = no wait at all).
    if (ms == 0) {
        return 0;
    }
    uint64_t t = ((uint64_t)ms * configTICK_RATE_HZ + 999u) / 1000u;
    return t == 0 ? 1 : (TickType_t)t;
#else
    return pdMS_TO_TICKS(ms);
#endif
}

uint32_t os_tick_ms(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

void os_delay_ms(uint32_t ms) { vTaskDelay(to_ticks(ms)); }

void os_delay_until_ms(uint32_t *last_wake, uint32_t period_ms) {
    TickType_t last = (TickType_t)(*last_wake / portTICK_PERIOD_MS);
    (void)xTaskDelayUntil(&last, to_ticks(period_ms));
    *last_wake = (uint32_t)(last * portTICK_PERIOD_MS);
}

void os_priority_set(uint8_t prio) {
    if (prio >= 1 && prio <= OS_PRIO_MAX) {
        vTaskPrioritySet(NULL, prio);
    }
}

uint32_t os_stack_free_min(void) {
    return (uint32_t)uxTaskGetStackHighWaterMark(NULL) * (uint32_t)sizeof(StackType_t);
}

void os_freeze_other_tasks(void) {
    static TaskStatus_t status[OS_STATS_MAX_TASKS];
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n > OS_STATS_MAX_TASKS) {
        n = OS_STATS_MAX_TASKS;
    }
    n = uxTaskGetSystemState(status, n, NULL);
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    for (UBaseType_t i = 0; i < n; i++) {
        const char *name = status[i].pcTaskName;
        if (status[i].xHandle == self || strncmp(name, "IDLE", 4) == 0 ||
            strncmp(name, "Tmr Svc", 7) == 0 || strncmp(name, "async_", 6) == 0) {
            continue;
        }
        vTaskSuspend(status[i].xHandle);
    }
}

void os_yield(void) { taskYIELD(); }

// --- Tasks ---------------------------------------------------------------------------------

#if defined(__linux__) || defined(__APPLE__)
#define OS_STACK_SCALE 4u // x86-64 frames are bigger than Thumb-2 ones; the host heap is large
#else
#define OS_STACK_SCALE 1u
#endif

typedef struct {
    os_task_fn fn;
    void *arg;
} task_boot_t;

static void task_trampoline(void *p) {
    task_boot_t boot = *(task_boot_t *)p;
    vPortFree(p);
    boot.fn(boot.arg);
    vTaskDelete(NULL); // a returning task cleans up instead of tripping the kernel's exit trap
}

os_task_t *os_task_create(const os_task_cfg_t *cfg) {
    if (cfg == NULL || cfg->fn == NULL || cfg->name == NULL || cfg->priority == 0 ||
        cfg->priority > OS_PRIO_MAX || cfg->stack_bytes < 256 || cfg->core != OS_CORE_0) {
        return NULL;
    }
    task_boot_t *boot = pvPortMalloc(sizeof *boot);
    if (boot == NULL) {
        return NULL;
    }
    boot->fn = cfg->fn;
    boot->arg = cfg->arg;
    TaskHandle_t handle = NULL;
    configSTACK_DEPTH_TYPE depth =
        (configSTACK_DEPTH_TYPE)(cfg->stack_bytes * OS_STACK_SCALE / sizeof(StackType_t));
    BaseType_t ok = xTaskCreate(task_trampoline, cfg->name, depth, boot, cfg->priority, &handle);
    if (ok != pdPASS) {
        vPortFree(boot);
        return NULL;
    }
    return (os_task_t *)handle;
}

void os_task_exit(void) { vTaskDelete(NULL); }

void os_start(void) {
    vTaskStartScheduler();
    // Only reached if the kernel could not create its idle/timer tasks (out of heap).
    neupod_panic(NEUPOD_PANIC_MALLOC_FAILED, "scheduler did not start");
}

// --- Queue / mutex / events ----------------------------------------------------------------

os_queue_t *os_queue_create(size_t item_size, size_t length) {
    return (os_queue_t *)xQueueCreate((UBaseType_t)length, (UBaseType_t)item_size);
}

bool os_queue_send(os_queue_t *q, const void *item, uint32_t timeout_ms) {
    return xQueueSend((QueueHandle_t)q, item, to_ticks(timeout_ms)) == pdPASS;
}

bool os_queue_recv(os_queue_t *q, void *item, uint32_t timeout_ms) {
    return xQueueReceive((QueueHandle_t)q, item, to_ticks(timeout_ms)) == pdPASS;
}

bool os_queue_pending(os_queue_t *q) { return uxQueueMessagesWaiting((QueueHandle_t)q) != 0; }

os_mutex_t *os_mutex_create(void) { return (os_mutex_t *)xSemaphoreCreateMutex(); }

bool os_mutex_lock(os_mutex_t *m, uint32_t timeout_ms) {
    return xSemaphoreTake((SemaphoreHandle_t)m, to_ticks(timeout_ms)) == pdPASS;
}

void os_mutex_unlock(os_mutex_t *m) { (void)xSemaphoreGive((SemaphoreHandle_t)m); }

os_event_t *os_event_create(void) { return (os_event_t *)xEventGroupCreate(); }

void os_event_set(os_event_t *e, uint32_t bits) {
    (void)xEventGroupSetBits((EventGroupHandle_t)e, bits & OS_EVENT_MASK);
}

void os_event_clear(os_event_t *e, uint32_t bits) {
    (void)xEventGroupClearBits((EventGroupHandle_t)e, bits & OS_EVENT_MASK);
}

uint32_t os_event_get(os_event_t *e) { return (uint32_t)xEventGroupGetBits((EventGroupHandle_t)e); }

uint32_t os_event_wait(os_event_t *e, uint32_t bits, bool all, bool clear_on_exit,
                       uint32_t timeout_ms) {
    return (uint32_t)xEventGroupWaitBits((EventGroupHandle_t)e, bits & OS_EVENT_MASK,
                                         clear_on_exit ? pdTRUE : pdFALSE, all ? pdTRUE : pdFALSE,
                                         to_ticks(timeout_ms));
}

// --- Diagnostics ---------------------------------------------------------------------------

typedef struct {
    UBaseType_t number;
    uint32_t runtime;
} prev_task_t;

size_t os_task_stats(os_task_info_t *out, size_t max) {
    static TaskStatus_t status[OS_STATS_MAX_TASKS];
    static prev_task_t prev[OS_STATS_MAX_TASKS];
    static uint32_t prev_total;

    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n > OS_STATS_MAX_TASKS) {
        n = OS_STATS_MAX_TASKS;
    }
    configRUN_TIME_COUNTER_TYPE total = 0;
    n = uxTaskGetSystemState(status, n, &total);
    uint32_t elapsed = (uint32_t)total - prev_total; // wraps correctly (32-bit microseconds)
    prev_total = (uint32_t)total;

    prev_task_t now[OS_STATS_MAX_TASKS];
    size_t count = 0;
    for (UBaseType_t i = 0; i < n; i++) {
        uint32_t before = 0;
        for (UBaseType_t j = 0; j < OS_STATS_MAX_TASKS; j++) {
            if (prev[j].number == status[i].xTaskNumber) {
                before = prev[j].runtime;
                break;
            }
        }
        uint32_t used = (uint32_t)status[i].ulRunTimeCounter - before;
        now[i].number = status[i].xTaskNumber;
        now[i].runtime = (uint32_t)status[i].ulRunTimeCounter;
        if (count < max) {
            os_task_info_t *o = &out[count++];
            strncpy(o->name, status[i].pcTaskName, sizeof o->name - 1);
            o->name[sizeof o->name - 1] = '\0';
            o->priority = (uint8_t)status[i].uxCurrentPriority;
            o->stack_free_min_bytes = (uint32_t)status[i].usStackHighWaterMark * sizeof(StackType_t);
            o->cpu_permille = elapsed == 0 ? 0 : (uint32_t)(((uint64_t)used * 1000u) / elapsed);
            if (o->cpu_permille > 1000u) {
                o->cpu_permille = 1000u;
            }
        }
    }
    memset(prev, 0, sizeof prev);
    memcpy(prev, now, n * sizeof now[0]);
    return count;
}

size_t os_heap_free(void) { return xPortGetFreeHeapSize(); }
size_t os_heap_free_min(void) { return xPortGetMinimumEverFreeHeapSize(); }

// --- Run-time-stats clock (portGET_RUN_TIME_COUNTER_VALUE in the FreeRTOSConfig.h files) ------

void neupod_rtos_stats_timer_init(void) {}
unsigned long neupod_rtos_stats_timer_get(void) { return (unsigned long)(uint32_t)hal_micros(); }

// --- Kernel failure hooks: all end in neupod_panic() -----------------------------------------

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    neupod_panic(NEUPOD_PANIC_STACK_OVERFLOW, name);
}

void vApplicationMallocFailedHook(void) {
    neupod_panic(NEUPOD_PANIC_MALLOC_FAILED, "kernel heap exhausted");
}

// SPDX-License-Identifier: MPL-2.0
/**
 * @file os.h
 * @brief Thin portable OS layer (tasks, delays, queues, mutexes, events) for app/ code.
 *
 * Backed by FreeRTOS on every target (RP2350 single-core port, the kernel's POSIX port on the host;
 * ESP-IDF ships FreeRTOS, so app/os/os_freertos.c should build there unchanged). app/ code uses
 * this header instead of calling FreeRTOS directly so that
 *   - the core is an explicit argument of task creation (always core 0: core 1 is the bare-metal
 *     audio core, docs/runtime.md), and
 *   - times are in milliseconds, not ticks, and no FreeRTOS type leaks into portable code.
 *
 * Timeouts are in milliseconds: OS_NO_WAIT (0) polls, OS_WAIT_FOREVER blocks. Nothing here may
 * be called from an interrupt handler.
 */
#ifndef NEUPOD_OS_H
#define NEUPOD_OS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_NO_WAIT 0u
#define OS_WAIT_FOREVER 0xFFFFFFFFu

/** Highest valid task priority (0 is the idle task's). Application priorities: 1..OS_PRIO_MAX. */
#define OS_PRIO_MAX 7u

/**
 * Which core a task runs on. The kernel is single-core (core 0): core 1 is the bare-metal audio core
 * and never runs RTOS tasks (docs/adr/0011-bare-metal-audio-core.md). The field is kept so call
 * sites state their core; os_task_create() rejects anything but OS_CORE_0.
 */
typedef enum {
    OS_CORE_0 = 0,
    OS_CORE_1 = 1, /* NOT allowed for tasks */
} os_core_t;

typedef void (*os_task_fn)(void *arg);

typedef struct os_task os_task_t; /**< Opaque task handle. */

typedef struct {
    const char *name;       /**< Up to 11 characters (longer is truncated). */
    os_task_fn fn;          /**< Task body. May return: the task then deletes itself. */
    void *arg;
    uint32_t stack_bytes;
    uint8_t priority;       /**< 1..OS_PRIO_MAX, higher runs first. */
    os_core_t core;         /**< Must be OS_CORE_0. */
} os_task_cfg_t;

/**
 * Create a task (before or after os_start()). Returns NULL if memory is exhausted, the config is
 * invalid, or cfg->core is not OS_CORE_0.
 */
os_task_t *os_task_create(const os_task_cfg_t *cfg);

/** Delete the calling task (also what returning from the task function does). */
void os_task_exit(void);

/** Start scheduling. Does not return. Create the first tasks before calling it. */
void os_start(void);

/** Milliseconds since the scheduler started (wraps after ~49 days). */
uint32_t os_tick_ms(void);

/** Block the calling task for at least @p ms. */
void os_delay_ms(uint32_t ms);

/**
 * Periodic wake-up without drift: blocks until *@p last_wake + @p period_ms and advances
 * *@p last_wake. Initialise *last_wake with os_tick_ms().
 */
void os_delay_until_ms(uint32_t *last_wake, uint32_t period_ms);

/**
 * Set the calling task's BASE priority (1..OS_PRIO_MAX). Used to lift a task above the UI for a short
 * bus transaction (hal_i2c timeouts are wall-clock) and to put it back afterwards: the caller always
 * passes an explicit RT_PRIO_* constant, never a value read back from the kernel (which could be a
 * temporarily inherited priority).
 */
void os_priority_set(uint8_t prio);

/** Smallest amount of stack (bytes) ever free for the calling task. */
uint32_t os_stack_free_min(void);

/**
 * Panic support: suspend every task except the caller, the idle task, the kernel timer task and the
 * radio worker (which the status LED needs). Only valid in ordinary task context.
 */
void os_freeze_other_tasks(void);

/** Give up the rest of the time slice to an equal-priority ready task. */
void os_yield(void);

/* ---- Queue: fixed-size items, copied in and out ------------------------------------------- */

typedef struct os_queue os_queue_t;

os_queue_t *os_queue_create(size_t item_size, size_t length);
/** True if the item was queued within @p timeout_ms. */
bool os_queue_send(os_queue_t *q, const void *item, uint32_t timeout_ms);
/** True if an item was received into @p item within @p timeout_ms. */
bool os_queue_recv(os_queue_t *q, void *item, uint32_t timeout_ms);

/* ---- Mutex (non-recursive, priority inheritance) ------------------------------------------ */

typedef struct os_mutex os_mutex_t;

os_mutex_t *os_mutex_create(void);
bool os_mutex_lock(os_mutex_t *m, uint32_t timeout_ms);
void os_mutex_unlock(os_mutex_t *m);

/* ---- Event flags: up to 24 bits, set by one task, waited on by another ---------------------- */

typedef struct os_event os_event_t;

#define OS_EVENT_MASK 0x00FFFFFFu

os_event_t *os_event_create(void);
void os_event_set(os_event_t *e, uint32_t bits);
void os_event_clear(os_event_t *e, uint32_t bits);
uint32_t os_event_get(os_event_t *e);
/**
 * Wait until @p bits are set (all of them if @p all, otherwise any). Returns the flag word at
 * wake-up (so the caller can test which bits are set); on timeout the requested bits are not all
 * (any) set in it. @p clear_on_exit clears the awaited bits when the wait succeeds.
 */
uint32_t os_event_wait(os_event_t *e, uint32_t bits, bool all, bool clear_on_exit,
                       uint32_t timeout_ms);

/* ---- Diagnostics (monitor task) ----------------------------------------------------------- */

#define OS_STATS_MAX_TASKS 16u

typedef struct {
    char name[12];
    uint8_t priority;
    uint32_t stack_free_min_bytes; /**< Smallest unused stack ever seen. */
    uint32_t cpu_permille;         /**< Share of ONE core since the previous call, 0..1000. */
} os_task_info_t;

/**
 * Snapshot of every task (idle tasks included). CPU shares are measured over the time since the
 * previous call (the first call covers the time since boot). Returns the number of entries.
 */
size_t os_task_stats(os_task_info_t *out, size_t max);

/** Heap bytes free now / the lowest free ever (the kernel heap that holds task stacks). */
size_t os_heap_free(void);
size_t os_heap_free_min(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_OS_H

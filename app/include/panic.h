// SPDX-License-Identifier: MPL-2.0
/**
 * @file panic.h
 * @brief Fatal-error path: print what died over the UART, then halt with the LED showing a code.
 *
 * Reached from the FreeRTOS stack-overflow hook, the malloc-failed hook and configASSERT
 * (app/os/os_freertos.c), and callable directly. Never returns.
 */
#ifndef NEUPOD_PANIC_H
#define NEUPOD_PANIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** LED blink counts (blinks, then a pause, repeated). */
typedef enum {
    NEUPOD_PANIC_STACK_OVERFLOW = 2,
    NEUPOD_PANIC_MALLOC_FAILED = 3,
    NEUPOD_PANIC_ASSERT = 4,
    NEUPOD_PANIC_OTHER = 5,
} neupod_panic_code_t;

/**
 * Report a fatal error and halt. @p detail is shown after the running task's name (a task name
 * for a stack overflow, "file:line" for an assert).
 */
void neupod_panic(neupod_panic_code_t code, const char *detail) __attribute__((noreturn));

/** configASSERT target (see the FreeRTOSConfig.h files). */
void neupod_panic_assert(const char *file, int line) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PANIC_H

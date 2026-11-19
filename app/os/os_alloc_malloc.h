// SPDX-License-Identifier: MPL-2.0
/**
 * @file os_alloc_malloc.h
 * @brief Fault injection for the malloc-backed os_alloc() used by binaries that do not link the RTOS (unit tests, storage_test).
 */
#ifndef NEUPOD_OS_ALLOC_MALLOC_H
#define NEUPOD_OS_ALLOC_MALLOC_H

/** The n-th os_alloc() from now (0 = the next one) and every later one returns NULL; a negative n turns the injection off. */
void os_alloc_fail_from(int n);
/** Number of os_alloc() calls since start (successful or not) and blocks not yet freed. */
int os_alloc_calls(void);
int os_alloc_live(void);

#endif

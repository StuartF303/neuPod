// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_host.h
 * @brief Host-only extras that are not part of the portable HAL (used by the host executables).
 */
#ifndef NEUPOD_HAL_HOST_H
#define NEUPOD_HAL_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Replace the blocking sleep inside hal_delay_ms() (default: nanosleep). The runtime installs
 * os_delay_ms here so a HAL sleep inside a task lets other tasks run (the kernel's POSIX port
 * has one "core").
 */
typedef void (*hal_host_delay_fn)(uint32_t ms);
void hal_host_set_delay_hook(hal_host_delay_fn fn);

/** Write the panel's current contents to @p path as a binary PPM (P6). False on I/O error. */
bool hal_host_display_dump_ppm(const char *path);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_HOST_H

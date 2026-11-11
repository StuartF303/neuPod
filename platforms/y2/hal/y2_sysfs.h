// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_sysfs.h
 * @brief Reading small sysfs attributes safely under the FreeRTOS POSIX port, whose 1 kHz SIGALRM tick interrupts
 *        blocking calls (the handler is installed without SA_RESTART): a read that comes back EINTR is retried,
 *        never turned into "missing".
 */
#ifndef NEUPOD_Y2_SYSFS_H
#define NEUPOD_Y2_SYSFS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read the first line of @p path into @p out (at most cap-1 bytes, trailing CR/LF removed, always NUL terminated).
 * False if the file cannot be opened or read; EINTR is retried.
 */
bool y2_sysfs_read_line(const char *path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_SYSFS_H

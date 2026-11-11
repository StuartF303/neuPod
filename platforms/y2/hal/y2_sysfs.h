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

/** Where the kernel publishes the bootloader's boot mode (MediaTek BOOT driver). */
#define Y2_BOOT_MODE_PATH "/sys/class/BOOT/BOOT/boot/boot_mode"
/** MediaTek boot modes that mean "powered on by the charger, not by the user". */
#define Y2_BOOT_MODE_KERNEL_POWER_OFF_CHARGING 8
#define Y2_BOOT_MODE_LOW_POWER_OFF_CHARGING 9

/**
 * Read the boot mode number from @p path. Returns -1 if the file is absent, unreadable, empty or not a number (a normal
 * boot on a kernel without the node, or a host): callers treat that as a normal boot.
 */
int y2_boot_mode_read(const char *path);

/** True for the two power-off-charging boot modes (8 and 9). */
bool y2_boot_mode_is_charging(int mode);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_SYSFS_H

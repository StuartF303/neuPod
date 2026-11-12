// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_host.h
 * @brief Host-only extras that are not part of the portable HAL (used by the host executables).
 */
#ifndef NEUPOD_HAL_HOST_H
#define NEUPOD_HAL_HOST_H

#include <stdbool.h>
#include <stddef.h>
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

/** Tests: change the simulated chip ID that hal_chip_id() reports (a card moved to another board). */
void hal_host_set_chip_id(const uint8_t id[8]);

/** Tests: the simulated access point goes away (link loss): hal_wifi_state() then reports HAL_WIFI_IDLE. */
void hal_host_wifi_drop_link(void);

/** Tests: the simulated Bluetooth headset goes away: a CONNECTED link turns FAILED with HAL_BT_FAIL_LINK_LOST. */
void hal_host_bt_drop_link(void);

/**
 * Tests: the simulated headset hangs up (stage 2a, nothing is open on the link). CONNECTED turns DISCONNECTED (no failure, like reason 0x13 on the
 * board); a Pair in progress ends FAILED with HAL_BT_FAIL_BUSY and a Connect with HAL_BT_FAIL_NOT_FOUND, and the Pair window closes at once.
 */
void hal_host_bt_remote_hangup(void);

/**
 * Tests only: say that a FatFs volume is mounted (the host's file API is POSIX and reports "not mounted"). Lets a test link the FatFs
 * backend of nfs.h (app/storage/nfs_fatfs.c) and run it on a disk image it mounted itself.
 */
void hal_host_storage_set_mounted(bool mounted);

/** Tests only: the card reports write-protected (hal_storage_get_info) and refuses writes, as a card with its lock switch on does. */
void hal_host_storage_set_read_only(bool read_only);

/**
 * The host simulator's card is a folder: the real path of the card path @p path (normalised, mapped into the folder, kept inside it even through
 * symbolic links), as nfs_posix.c resolves it. For the library's POSIX file layer (app/include/lib_ff_posix.h). False if the path is not allowed.
 */
bool hal_host_resolve_path(const char *path, char *out, size_t cap);

/** Write the panel's current contents to @p path as a binary PPM (P6). False on I/O error. */
bool hal_host_display_dump_ppm(const char *path);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_HOST_H

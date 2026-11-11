// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_sd.h
 * @brief Which block device may the Y2 firmware open as its microSD? Pure sysfs/filename logic, unit-tested
 *        against a fake sysfs tree.
 *
 * SAFETY: mmcblk0 is the Y2's internal eMMC (boot chain and our rootfs). Nothing in this firmware may open
 * it or any partition of it. The rule, enforced here and again by hal_storage.c right before open():
 *   - the name must be exactly mmcblk1 or mmcblk1p<N>   (mmcblk0*, mmcblk1boot0, mmcblk10 ... all refused);
 *   - the disk must say it is a removable card: /sys/block/<disk>/removable == 1, or
 *     /sys/block/<disk>/device/type == "SD";
 *   - after open, the node's major:minor must equal /sys/block/<disk>[/<part>]/dev, so a wrong or stale
 *     device node cannot redirect us to another disk.
 * Failure stance: fail closed. Anything doubtful is refused and logged, and the firmware runs without a card.
 */
#ifndef NEUPOD_Y2_SD_H
#define NEUPOD_Y2_SD_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True only for "mmcblk1" and "mmcblk1p<digits>". */
bool y2_sd_name_allowed(const char *name);

typedef enum {
    Y2_SD_FOUND = 0,  /**< a card device was chosen (out->node) */
    Y2_SD_ABSENT,     /**< no /sys/block/mmcblk1: no card inserted (or not yet probed) */
    Y2_SD_REFUSED,    /**< mmcblk1 exists but does not identify as a removable SD card */
    Y2_SD_NO_NODE,    /**< sysfs shows the card but mdev has not created the device node yet */
} y2_sd_result_t;

typedef struct {
    char disk[16];  /**< "mmcblk1" */
    char name[24];  /**< node name chosen: "mmcblk1" (whole card) or "mmcblk1p1" */
    char node[128]; /**< <dev_dir>/<name> */
} y2_sd_pick_t;

/**
 * Choose the card device. @p sys_block is "/sys/block" and @p dev_dir "/dev" on the device (a fake tree in
 * tests). The whole card is preferred (FatFs then finds the partition from the MBR, as on the Pico); the
 * first partition is used only if the whole-disk node is missing. @p why (may be NULL) gets a one-line
 * reason for every result other than FOUND.
 */
y2_sd_result_t y2_sd_pick(const char *sys_block, const char *dev_dir, y2_sd_pick_t *out, char *why, size_t why_cap);

/** Read "major:minor" from <sys_block>/<disk>[/<name>]/dev. False if unreadable. */
bool y2_sd_expected_devnum(const char *sys_block, const char *name, unsigned *major, unsigned *minor);

/** True if @p major:@p minor is what sysfs says @p name is (call with fstat's st_rdev after open). */
bool y2_sd_devnum_matches(const char *sys_block, const char *name, unsigned major, unsigned minor);

/** Card identity strings from <sys_block>/<disk>/device (name, serial, manfid, oemid, date); missing ones stay empty. */
typedef struct {
    char name[24], serial[24], manfid[16], oemid[16], date[16];
} y2_sd_identity_t;
void y2_sd_identity(const char *sys_block, const char *disk, y2_sd_identity_t *out);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_SD_H

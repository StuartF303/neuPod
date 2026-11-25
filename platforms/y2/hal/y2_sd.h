// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_sd.h
 * @brief Which block device may the Y2 firmware open as its microSD? Pure sysfs/filename logic, unit-tested
 *        against a fake sysfs tree.
 *
 * SAFETY: mmcblk0 is the Y2's internal eMMC (boot chain and our rootfs). Nothing in this firmware may open
 * it or any partition of it. The rule, enforced here and again by hal_storage.c right before open():
 *   - the name must be mmcblkN or mmcblkNp<M> with N >= 1 and no leading zeros (mmcblk0*, mmcblk1boot0, mmcblk01
 *     ... all refused by name);
 *   - the disk must say it is an SD card: /sys/block/<disk>/device/type == "SD";
 *   - its controller must not be mmc0 (the eMMC's host): the host is read from where /sys/block/<disk> really
 *     resolves (.../mmc_host/mmcX/...); if it cannot be determined the disk is refused. The removable attribute is
 *     ignored (the MTK slot driver reports 0 for the real SD slot);
 *   - before and after open, the node must be a real block device (not a symlink) whose major:minor equals
 *     /sys/block/<disk>[/<part>]/dev, and neither may be mmcblk0's device numbers, so a wrong or stale
 *     device node cannot redirect us to another disk.
 * The card may enumerate as any mmcblkN: /sys/block is scanned and the lowest qualifying N is used.
 * Failure stance: fail closed. Anything doubtful is refused and logged, and the firmware runs without a card.
 */
#ifndef NEUPOD_Y2_SD_H
#define NEUPOD_Y2_SD_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True only for "mmcblk<N>" and "mmcblk<N>p<M>", N and M >= 1 without leading zeros (so never mmcblk0*). */
bool y2_sd_name_allowed(const char *name);

typedef enum {
    Y2_SD_FOUND = 0,  /**< a card device was chosen (out->node) */
    Y2_SD_ABSENT,     /**< no /sys/block/mmcblkN with N >= 1: no card inserted (or not yet probed) */
    Y2_SD_REFUSED,    /**< mmcblkN disks exist but none is an SD card on a non-mmc0 host */
    Y2_SD_NO_NODE,    /**< sysfs shows the card but mdev has not created the device node yet */
} y2_sd_result_t;

typedef struct {
    char disk[24];  /**< "mmcblk<N>" */
    char name[40];  /**< node name chosen: "mmcblk<N>" (whole card) or "mmcblk<N>p1" */
    char node[128]; /**< <dev_dir>/<name> */
    size_t qualifying; /**< how many disks qualified; the lowest N was taken (log when > 1) */
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

/** Card identity strings from <sys_block>/<disk>/device (name, serial, manfid, oemid, date); missing stay empty. */
typedef struct {
    char name[24], serial[24], manfid[16], oemid[16], date[16];
} y2_sd_identity_t;
void y2_sd_identity(const char *sys_block, const char *disk, y2_sd_identity_t *out);

/** What identifies the card that is held: sysfs dev numbers, device/cid and the identity strings. */
typedef struct {
    char dev[16];
    char cid[48];
    y2_sd_identity_t id;
} y2_sd_snapshot_t;
void y2_sd_snapshot(const char *sys_block, const char *disk, y2_sd_snapshot_t *out);

typedef enum {
    Y2_SD_HELD_SAME,    /**< the held disk is still there and is the same card */
    Y2_SD_HELD_CHANGED, /**< the disk is there but its identity changed: pulled and re-inserted (maybe another card) */
    Y2_SD_HELD_GONE,    /**< the held disk (or its node) is gone, or no longer qualifies */
} y2_sd_held_t;

/** Is the disk in @p pick (taken when the card was opened) still there and still the card @p snap describes? */
y2_sd_held_t y2_sd_held_state(const char *sys_block, const char *dev_dir, const y2_sd_pick_t *pick,
                              const y2_sd_snapshot_t *snap);

/**
 * Validate a device node's stat result for @p name: block device, not a symlink, major:minor equal to sysfs, and not
 * mmcblk0's numbers (its sysfs dev if present, else 179:0..7). @p why (may be NULL) gets the reason on false.
 */
bool y2_sd_check_stat(const char *sys_block, const char *name, const struct stat *st, char *why, size_t why_cap);

/** lstat() @p node (no symlink following) and run y2_sd_check_stat() on it; the pre-open check. */
bool y2_sd_check_node(const char *sys_block, const char *node, const char *name, char *why, size_t why_cap);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_SD_H

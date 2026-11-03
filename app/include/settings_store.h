// SPDX-License-Identifier: MPL-2.0
/**
 * @file settings_store.h
 * @brief Small persistent key/value store on the microSD card (docs/bluetooth.md, "Settings store").
 *
 * Keys are 32-bit tags with a short byte value: the user settings (volume, backlight, click feedback) and, through
 * settings_bt_storage(), BTstack's link keys and device database (they use the same tag/value/length model).
 * The whole store lives in RAM and is always usable. It is persisted as ONE file, /.neupod/settings.bin
 * (magic, version, sequence, items, CRC-32), written only by settings_flush(), which only the storage task calls.
 *
 * Why the SD card and not the internal flash (BTstack's default): core 1 runs its code from the same QSPI flash
 * (XIP) and is not multicore-lockout aware by design, so ANY flash erase/program at run time stalls or crashes the
 * audio core. Nothing in this firmware writes the internal flash.
 *
 * RAM-only fallback: until settings_attach_storage(true) (the card mounted and healthy) every setter still works
 * but nothing is written; a card that stops accepting writes puts the store back in RAM-only mode (reported by
 * settings_get_status()). The values then simply do not survive a power cycle.
 *
 * Write safety: the card's write lock is opened ONLY inside settings_flush(), for the duration of one save, under an
 * nfs write scope that confines every mutation to /.neupod (nfs.h). The store never formats and never writes
 * anywhere else. Atomic save: full image to settings.tmp, sync, delete settings.bin, rename tmp to bin. If power is
 * lost anywhere in the sequence the next load finds a complete file: bin, or tmp (valid CRC, higher sequence).
 *
 * Threading: setters/getters may be called from any task (a caller-supplied lock guards the RAM image).
 * settings_load() and settings_flush() do file I/O: storage task (or the boot check) only, never core 1.
 */
#ifndef NEUPOD_SETTINGS_STORE_H
#define NEUPOD_SETTINGS_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_DIR "/.neupod"
#define SETTINGS_FILE SETTINGS_DIR "/settings.bin"
#define SETTINGS_TMP_FILE SETTINGS_DIR "/settings.tmp"

#define SETTINGS_MAX_ITEMS 24u /**< distinct tags kept (3 settings + link keys + BT device records) */
#define SETTINGS_VALUE_MAX 48u /**< bytes per value (a BTstack link key record is 27) */
#define SETTINGS_VERSION 1u

/** Tags of the user settings, four ASCII characters like BTstack's own ('NPxx'; BTstack uses 'BTL?' / 'BTD?'). */
#define SETTINGS_TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define SETTING_VOLUME_ATT_DB SETTINGS_TAG('N', 'P', 'v', 'o') /**< u8, attenuation in dB */
#define SETTING_BACKLIGHT_PCT SETTINGS_TAG('N', 'P', 'b', 'l') /**< u8, 5..100 */
#define SETTING_CLICK_FEEDBACK SETTINGS_TAG('N', 'P', 'c', 'k') /**< u8, 0 or 1 */

/** Lock callbacks around the RAM image (NULL members = no locking; unit tests). Must not be recursive. */
typedef struct {
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void *ctx;
} settings_lock_t;

/** What the last settings_load() found. */
typedef enum {
    SETTINGS_LOAD_NONE = 0,   /**< not loaded (no card, or not tried yet) */
    SETTINGS_LOAD_FRESH,      /**< no file yet: first boot or a new card; defaults */
    SETTINGS_LOAD_OK,         /**< settings.bin valid */
    SETTINGS_LOAD_RECOVERED,  /**< settings.tmp was newer/the only valid file (power was lost during a save) */
    SETTINGS_LOAD_CORRUPT,    /**< files exist but fail magic/version/CRC: defaults, the next save replaces them */
    SETTINGS_LOAD_IO_ERROR,   /**< could not read the card: defaults, RAM-only */
} settings_load_result_t;

typedef struct {
    bool persistent;          /**< storage attached and not failing: changes will be written */
    bool loaded;              /**< settings_load() ran (values come from the card, or defaults after a clean miss) */
    bool dirty;               /**< a change is waiting to be written */
    settings_load_result_t load;
    uint32_t seq;             /**< sequence number of the file last read/written */
    uint32_t saves;           /**< successful saves since boot */
    uint32_t save_failures;   /**< failed saves since boot */
    uint32_t items;
} settings_status_t;

/** Reset to an empty RAM-only store. Call once before anything else. */
void settings_init(const settings_lock_t *lock);

/** The storage task/boot check calls this with the card's state. false = RAM-only (no card, mount failed, degraded). */
void settings_attach_storage(bool available);

/**
 * Read the file(s) from the card into RAM (read-only; needs the volume mounted). Replaces the RAM contents, so call it
 * once, before the first change. Bumps settings_generation(). Without storage it returns SETTINGS_LOAD_NONE.
 */
settings_load_result_t settings_load(void);

/** Changes whenever settings_load() replaced the RAM contents; consumers re-read their values when it moves. */
uint32_t settings_generation(void);

/** Milliseconds-free dirty probe for the storage task's poll. */
bool settings_dirty(void);

/**
 * Write the image if it changed. Returns 0 when nothing needed writing or the save succeeded, otherwise an
 * nfs_err_t (never writes in RAM-only mode: returns NFS_ERR_NOT_MOUNTED). After SETTINGS_MAX_SAVE_FAILURES in a row the
 * store gives up on the card (RAM-only) until settings_attach_storage(true) is called again.
 */
int settings_flush(void);
#define SETTINGS_MAX_SAVE_FAILURES 3u

void settings_get_status(settings_status_t *out);

/* ---- values ---- */

/** Copy the value of @p tag (at most @p size bytes); returns its full length, 0 if absent. NULL @p buf: length only. */
uint32_t settings_get(uint32_t tag, void *buf, uint32_t size);
/** Store a value. false if it is too long, empty, or the store is full. Unchanged values do not mark the store dirty. */
bool settings_put(uint32_t tag, const void *data, uint32_t len);
void settings_delete(uint32_t tag);

bool settings_get_u8(uint32_t tag, uint8_t *out);
void settings_set_u8(uint32_t tag, uint8_t v);

/* ---- BTstack ---- */

/**
 * The store as BTstack-shaped tag/value/length storage (hal_radio.h); the BTstack port wraps it in a btstack_tlv_t.
 * While the card's contents are still to be loaded (or it is not yet known whether there is a card) writes are refused,
 * since the load would drop them; with no card they work for the session (RAM-only).
 */
const hal_bt_storage_t *settings_bt_storage(void);

/* ---- test hooks ---- */

/**
 * Unit tests only: stop settings_flush() as if the power failed. Steps: 0 = half of the temp file written,
 * 1 = temp file complete and closed, 2 = old file deleted (before the rename), 3 = after the rename.
 * -1 = off. The aborted flush returns NFS_ERR_IO.
 */
void settings_test_set_power_loss_step(int step);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_SETTINGS_STORE_H

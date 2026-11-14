// SPDX-License-Identifier: MPL-2.0
/**
 * @file settings_store.h
 * @brief Small persistent key/value store on the microSD card (docs/bluetooth.md, "Settings store").
 *
 * Keys are 32-bit tags with a short byte value: the user settings (volume, backlight, click feedback) and, through
 * settings_bt_storage(), BTstack's link keys and device database (they use the same tag/value/length model).
 * The whole store lives in RAM and is always usable. It is persisted as TWO alternating files, /.neupod/settings.a and settings.b
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
 * anywhere else. Crash-safe save without rename or delete: the image is written IN PLACE over the OLDER (or invalid) of the two
 * files, then synced; the other file is never touched. Load takes the valid file with the higher sequence, so a cut at any point
 * leaves at least the previous complete state.
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
#define SETTINGS_FILE_A SETTINGS_DIR "/settings.a"
#define SETTINGS_FILE_B SETTINGS_DIR "/settings.b"
#define SETTINGS_FILE_OLD SETTINGS_DIR "/settings.old" /**< a file written by a NEWER firmware, kept aside instead of being wiped */

#define SETTINGS_MAX_ITEMS 48u /**< distinct tags kept (settings, Wi-Fi networks, link keys, BT device records): worst case about 41 */
#define SETTINGS_VALUE_MAX 96u /**< bytes per stored value; a sealed secret holds up to 96 - 29 = 67 bytes of plaintext (Wi-Fi passwords are at most 63) */
#define SETTINGS_VERSION 2u /**< written; 1 (same layout, at most 32 items) is still read. Firmware older than 2 sets a version-2 file aside instead of reading it */
#define SETTINGS_VERSION_V1 1u

/** Tags of the user settings, four ASCII characters like BTstack's own ('NPxx'; BTstack uses 'BTL' + slot / 'BTD' + slot). */
#define SETTINGS_TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define SETTING_VOLUME_ATT_DB SETTINGS_TAG('N', 'P', 'v', 'o') /**< u8, attenuation in dB */
#define SETTING_BACKLIGHT_PCT SETTINGS_TAG('N', 'P', 'b', 'l') /**< u8, 5..100 */
#define SETTING_CLICK_FEEDBACK SETTINGS_TAG('N', 'P', 'c', 'k') /**< u8, click_mode_t: 0 off, 1 haptic (the old "on"), 2 piezo, 3 both */
#define SETTING_GAPLESS SETTINGS_TAG('N', 'P', 'g', 'p')       /**< u8, 0 or 1; absent = on */
#define SETTING_VOLUME_PATH SETTINGS_TAG('N', 'P', 'v', 'p')   /**< u8, output_path_t the volume was saved for (player.h); absent = unknown */
#define SETTING_RESUME0 SETTINGS_TAG('N', 'P', 'r', '0')       /**< resume record header + start of the track path (resume.h) */
#define SETTING_RESUME1 SETTINGS_TAG('N', 'P', 'r', '1')       /**< rest of the track path */
#define SETTING_HAPTIC_ACTUATOR SETTINGS_TAG('N', 'P', 'h', 'a') /**< u8, 0 = ERM, 1 = LRA; absent = the build default */
#define SETTING_HAPTIC_CAL SETTINGS_TAG('N', 'P', 'h', 'c')      /**< 11 bytes, haptic_logic.h: DRV2605L auto-calibration for one actuator */
#define SETTING_SHUFFLE SETTINGS_TAG('N', 'P', 's', 'h')       /**< u8, pq_shuffle_t: 0 off, 1 songs; absent = off */
#define SETTING_REPEAT SETTINGS_TAG('N', 'P', 'r', 'p')        /**< u8, pq_repeat_t: 0 off, 1 one, 2 all; absent = off */
#define SETTING_RESUME_PLAY SETTINGS_TAG('N', 'P', 'r', 'a')   /**< u8, 0 or 1: start playing at boot when the resume record says playing; absent = 0 (restore paused) */
#define SETTING_AUTODIM SETTINGS_TAG('N', 'P', 'a', 'd')       /**< u8, 0 or 1: dim the backlight when idle; absent = 1 */
#define SETTING_AUTODIM_IDX SETTINGS_TAG('N', 'P', 'a', 't')   /**< u8, 0..5: idle time before dimming (ui_logic.h: 10 s .. 5 min); absent = 2 (30 s) */
#define SETTING_SCREENOFF_IDX SETTINGS_TAG('N', 'P', 's', 'o') /**< u8, 0..6: time after the dim until the backlight is fully off (ui_logic.h: Never .. 5 min); absent = 5 (3 min) */
#define SETTING_AUTOLOCK SETTINGS_TAG('N', 'P', 'a', 'l')       /**< u8, 0 or 1: Hold turns on by itself when the screen goes off (docs/adr/0020-hold.md); absent = 1 */
#define SETTING_THEME SETTINGS_TAG('N', 'P', 't', 'h')         /**< u8, ui_theme_mode_t: 0 Light, 1 Dark, 2 Auto (dark while the backlight is dimmed); absent = 0 (Light) */
#define SETTING_BT_DEVICE SETTINGS_TAG('N', 'P', 'b', 'd')     /**< the one remembered headset: version, address, class, name (bt_saved.h; 10..58 bytes) */
#define SETTING_AUDIO_OUTPUT SETTINGS_TAG('N', 'P', 'a', 'o') /**< u8, audio_route_pref_t: 0 Auto (Bluetooth whenever a headset's stream is open), 1 Wired, 2 Bluetooth (BT-3: the output is Bluetooth whether or not a headset is there), 3 Speaker (the Y2's internal speaker); absent or unknown = Auto */
#define SETTING_LINE_OUT SETTINGS_TAG('N', 'P', 'l', 'o')     /**< u8, 0 or 1: Line out, the jack at full level for an external amplifier (Y2); absent = 0 (Off). The route turns it Off by itself when the jack is empty (unplugged, or at boot) */
#define SETTING_SPEAKER SETTINGS_TAG('N', 'P', 's', 'k')      /**< u8, 0 or 1: the internal speaker may be used (Auto without headphones or headset, or Audio output = Speaker); absent = 0 (Off: nothing comes out of the speaker by surprise) */
#define SETTING_WIFI SETTINGS_TAG('N', 'P', 'w', 'f')            /**< 4 bytes: enabled, during playback, idle-off minutes, zone */
#define SETTING_NTP_SERVER SETTINGS_TAG('N', 'P', 'n', 's')      /**< string */
#define SETTING_TEST_URL SETTINGS_TAG('N', 'P', 't', 'u')        /**< string */
#define SETTING_NET_SSID(i) SETTINGS_TAG('N', 'P', 'n', (char)('0' + (i)))  /**< flags byte (1 = has password) + SSID */
#define SETTING_NET_PSK(i) SETTINGS_TAG('N', 'P', 'p', (char)('0' + (i)))   /**< sealed password */

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
    SETTINGS_LOAD_OK,         /**< a valid file was read */
    SETTINGS_LOAD_RECOVERED,  /**< ... and the other file was damaged (a save was cut short): the valid one was used */
    SETTINGS_LOAD_CORRUPT,    /**< files exist but fail magic/version/CRC: defaults, the next save replaces them */
    SETTINGS_LOAD_IO_ERROR,   /**< could not read the card (or a validated file on the second read): RAM-only for this boot, no save touches the card; defaults, or the older file's contents when the newer one failed its re-read */
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
 * store gives up on the card (RAM-only) until settings_attach_storage(true) is called again. NFS_ERR_BUSY (another task holds the nfs
 * write scope, e.g. a library scan) is not a failure: the change stays dirty, nothing is counted, the caller retries later.
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

/* ---- Secrets (crypto_secret.h): AES-256-GCM sealed, bound to the chip ---- */

/** Store @p len secret bytes (<= SETTINGS_VALUE_MAX - SECRET_OVERHEAD) sealed under @p tag. false if too long, or no key. */
bool settings_put_secret(uint32_t tag, const void *data, uint32_t len);
/** Read a secret: its length, or 0 if absent OR it does not authenticate (another board, edited card): ask the user again. */
uint32_t settings_get_secret(uint32_t tag, void *buf, uint32_t size);
/** Seal any plaintext BTstack record (tags 'BT..' shorter than a sealed record) written before secrets were encrypted. */
void settings_migrate_bt_secrets(void);

/* ---- BTstack ---- */

/**
 * The store as BTstack-shaped tag/value/length storage (hal_radio.h); the BTstack port wraps it in a btstack_tlv_t.
 * While the card's contents are still to be loaded (or it is not yet known whether there is a card) writes are refused,
 * since the load would drop them; with no card they work for the session (RAM-only).
 */
const hal_bt_storage_t *settings_bt_storage(void); /* link keys and device records are stored SEALED (settings_put_secret) */

/* ---- test hooks ---- */

/**
 * Unit tests only: stop settings_flush() as if the power failed. Steps: 0 = half of the inactive file written (not synced),
 * 1 = file complete, synced and closed.
 * -1 = off. The aborted flush returns NFS_ERR_IO.
 */
void settings_test_set_power_loss_step(int step);
/** Unit tests only: make the next re-read of the winning file during settings_load() fail, to exercise the fall back to the other file. */
void settings_test_fail_next_reread(void);
/** Unit tests only: as above, but the re-read fails with an I/O error. */
void settings_test_ioerr_next_reread(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_SETTINGS_STORE_H

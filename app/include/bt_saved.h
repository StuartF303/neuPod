// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_saved.h
 * @brief The one remembered Bluetooth headset (docs/bluetooth.md, stage 2a): its address, class and name in the settings store
 *        (SETTING_BT_DEVICE), and the rule for when a Pair action makes a device the remembered one.
 *
 * The pairing key itself is BTstack's link key record (tag 'B','T','L' and a slot number, sealed) in the same store; this record only says WHICH
 * device the Settings > Bluetooth rows act on. Remembering a headset replaces the previous one. A device is remembered only after
 * a Pair action ended CONNECTED (the key is stored by then); a failed or cancelled Pair leaves the previous headset untouched.
 *
 * Value layout (10..42 bytes, little endian): [0] version (1), [1..6] address MSB first, [7..9] class of device (24 bit),
 * [10..] name bytes (at most HAL_BT_NAME_MAX, no NUL).
 *
 * Threading: bt_saved_on_event() runs on the radio worker (hardware) or the UI task (host simulator); everything else on the UI
 * task. The settings store takes its own lock. The pending-Pair candidate is written by bt_saved_pair_begin() BEFORE
 * hal_bt_pair() asks for anything, so no event for it can arrive earlier.
 */
#ifndef NEUPOD_BT_SAVED_H
#define NEUPOD_BT_SAVED_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_SAVED_VERSION 1u
#define BT_SAVED_MIN_LEN 10u
#define BT_SAVED_MAX_LEN (BT_SAVED_MIN_LEN + HAL_BT_NAME_MAX)

/** The remembered headset (address, class, name; rssi unknown). false if none, or the stored record is damaged. */
bool bt_saved_get(hal_bt_device_t *out);
/** Remember @p dev (replacing any previous one). false if the address is empty or the store is full. */
bool bt_saved_put(const hal_bt_device_t *dev);
/** Forget the record (not the link key: hal_bt_forget() does that). Does nothing while the store is not writable (the record would come back at the next boot). */
void bt_saved_clear(void);

/**
 * Make the stale queue and the pending candidate safe between the radio worker and the UI task: @p lock / @p unlock bracket every access (a short
 * critical section; bt_service passes an OS mutex). Without it (host tests) there is no locking. Call once at start-up, before any other call.
 */
void bt_saved_set_lock(void (*lock)(void), void (*unlock)(void));

/** A Pair action for @p dev is about to start: remember it as the candidate. Call BEFORE hal_bt_pair(). false (nothing changed) while another candidate is live: do not start the Pair. */
bool bt_saved_pair_begin(const hal_bt_device_t *dev);
/** The Pair was not started after all (hal_bt_pair() refused): drop the candidate. */
void bt_saved_pair_abort(void);
/**
 * Feed every HAL_BT_EVT_LINK event here. CONNECTED while a Pair candidate is pending makes it the remembered headset (and, if it
 * replaced a different one, queues that one's address for deletion); if the record cannot be stored (store full) the new key is queued for deletion
 * instead and bt_saved_notice() says why. FAILED or DISCONNECTED drops the candidate and queues its address for deletion, unless it is the remembered
 * headset: BTstack already deleted that key when the Pair began, so the record is cleared instead.
 * Other events are ignored, and so are link events of an attempt older than the pending candidate's (hal_bt_link_attempt() when it began). Safe to call from the radio worker: it only touches the settings store.
 */
void bt_saved_on_event(const hal_bt_event_t *ev);
/** The oldest address whose link key should now be deleted (a replaced headset, a Pair that did not finish, a record that could not be saved); false if none. Stays queued until bt_saved_drop_stale(). */
bool bt_saved_peek_stale(uint8_t addr[6]);
void bt_saved_drop_stale(void);
/**
 * Delete the link key for @p addr: hal_bt_forget() when the stack is up, otherwise (OFF, STARTING, FAILED) straight from the settings
 * store. HAL_OK when done, HAL_EBUSY when the radio could not take the request yet or the store is not writable (the key is still on the card).
 */
hal_status_t bt_saved_delete_key(const uint8_t addr[6]);
/** Queue @p addr for deletion by the next bt_saved_drain_stale(). */
void bt_saved_queue_delete(const uint8_t addr[6]);
/** Process the stale queue (up to four deep): each address is deleted, or kept for the next call if the radio is busy. Called from bt_service_tick(). */
void bt_saved_drain_stale(void);

/** Why the last Pair ended without a remembered headset although it connected. Cleared by the next bt_saved_pair_begin(). */
typedef enum { BT_SAVED_NOTICE_NONE = 0, BT_SAVED_NOTICE_FULL /**< the settings store is full: "Can't save: settings full" */,
                         BT_SAVED_NOTICE_NOT_SAVED /**< remembered in RAM only (card read-only / absent): gone after a reboot */ } bt_saved_notice_t;
bt_saved_notice_t bt_saved_notice(void);
/** A Connect is starting: a "not saved" notice from an earlier Pair no longer applies. */
void bt_saved_connect_begin(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_SAVED_H

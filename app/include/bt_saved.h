// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_saved.h
 * @brief The one remembered Bluetooth headset (docs/bluetooth.md, stage 2a): its address, class and name in the settings store
 *        (SETTING_BT_DEVICE), and the rule for when a Pair action makes a device the remembered one.
 *
 * The pairing key itself is BTstack's link key record (tag 'BTL?', sealed) in the same store; this record only says WHICH
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
/** Forget the record (not the link key: hal_bt_forget() does that). */
void bt_saved_clear(void);

/** A Pair action for @p dev is about to start: remember it as the candidate. Call BEFORE hal_bt_pair(). */
void bt_saved_pair_begin(const hal_bt_device_t *dev);
/** The Pair was not started after all (hal_bt_pair() refused): drop the candidate. */
void bt_saved_pair_abort(void);
/**
 * Feed every HAL_BT_EVT_LINK event here. CONNECTED while a Pair candidate is pending makes it the remembered headset (and, if it
 * replaced a different one, queues that one's address for bt_saved_take_stale()); FAILED or DISCONNECTED drops the candidate (its address is queued for
 * bt_saved_take_stale() too, unless it is the remembered headset: a Pair that got as far as making a key must not leave it behind).
 * Other events are ignored. Safe to call from the radio worker: it only touches the settings store.
 */
void bt_saved_on_event(const hal_bt_event_t *ev);
/** The address of a headset that a new Pair replaced, whose link key should now be deleted (hal_bt_forget). Returns it once. */
bool bt_saved_take_stale(uint8_t addr[6]);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_SAVED_H

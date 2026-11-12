// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_bt_tlv.h
 * @brief BTstack's link-key storage for the Y2, where BTstack runs on its own thread.
 *
 * The settings store (hal_bt_storage_t) takes RTOS locks and wakes the storage task, and the FreeRTOS POSIX port is not safe to call
 * from a thread that is not an RTOS task. So BTstack's thread never touches it: it works on a small RAM copy of the link-key
 * records, preloaded from the store when Bluetooth starts, and every put / delete is written behind to the store by
 * y2_bt_tlv_flush(), which an RTOS task calls (bt_port_poll). A write the store refuses (card missing, read-only, not loaded yet) stays
 * pending and is retried about once a second; meanwhile pairing works and the key lives in RAM for this session. A key the store never
 * accepted is not remembered after a restart.
 *
 * Only link-key records are cached (tags 'B','T','L', index): that is all BTstack's classic link key database stores.
 */
#ifndef NEUPOD_Y2_BT_TLV_H
#define NEUPOD_Y2_BT_TLV_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Forget everything (tests, and a fresh start). */
void y2_bt_tlv_reset(void);

/**
 * Preload the link-key records from @p backing (RTOS task context) and return the RAM-backed view for BTstack's thread. A NULL
 * @p backing returns NULL (pairing data is not kept, as hal_bt_init documents).
 */
const hal_bt_storage_t *y2_bt_tlv_attach(const hal_bt_storage_t *backing);

/** RTOS task context: write pending puts and deletes through to the backing store. @p now_ms: any monotonic clock. */
void y2_bt_tlv_flush(uint32_t now_ms);

/** true while a put or delete has not been accepted by the backing store yet. */
bool y2_bt_tlv_pending(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_BT_TLV_H

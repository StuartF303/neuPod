// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_port.h
 * @brief What bt_btstack.c needs from a platform (ADR-0019): the controller transport, the run loop and the thread
 *        BTstack runs on, and the hand-over between that thread and the rest of the firmware.
 *
 * One implementation per platform: platforms/rp2350/hal/bt_port_cyw43.c (CYW43 transport, async_context run loop) and
 * platforms/y2/hal/bt_port_stpbt.c (/dev/stpbt, POSIX run loop on its own pthread). A port also owns everything
 * BTstack-specific that is not link logic: btstack_memory_init(), the run loop, hci_init() with its transport and chipset
 * driver, and where the local address comes from (the CYW43 has its own; the Y2 reads it and programs it into the chip
 * before HCI_Reset). Nothing here locks: the adapter's state is touched by the BTstack thread, or through atomics.
 */
#ifndef NEUPOD_BT_PORT_H
#define NEUPOD_BT_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "btstack_run_loop.h"
#include "hal_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Can the controller be used at all (RP2350: the radio driver is up; Y2: /dev/stpbt was opened)? false: hal_bt_init() is HAL_ENODEV. */
bool bt_port_present(void);

/** The storage BTstack's thread may use, made from the one hal_bt_init() was given (RTOS task context). RP2350: the same pointer, BTstack's
 * thread is a task and may call the settings store. Y2: a RAM copy that is written back by bt_port_poll() (platforms/y2/hal/y2_bt_tlv.h). */
const hal_bt_storage_t *bt_port_storage(const hal_bt_storage_t *backing);

/** Called by hal_bt_init() before the first request is handed over. Make BTstack ready (memory, run loop, hci_init); idempotent. false: HAL_ERR. */
bool bt_port_start(void);

typedef uint32_t (*bt_port_fn)(void *arg);
/** Run fn(arg) on BTstack's thread and wait for it. false if it could not be done (the port gave up waiting). */
bool bt_port_run_sync(bt_port_fn fn, void *arg);

/** Queue @p reg to run on BTstack's thread; never waits for it. Any task. @p reg must stay valid until it has run. */
void bt_port_post(btstack_context_callback_registration_t *reg);

/** hci_power_control(ON or OFF), on BTstack's thread. Returns its result (0 = ok). */
int bt_port_power(bool on);

/** Hand an event to the listener. RP2350: called right here, on the radio worker. Y2: BTstack's thread must never touch the RTOS, so
 * the event is queued and bt_port_poll() makes the call from a task. Either way the callback must be quick and must not block. */
void bt_port_deliver(hal_bt_event_fn cb, void *ctx, const hal_bt_event_t *ev);

/** hal_bt_poll(): deliver what bt_port_deliver() queued, from the calling task. A no-op where events are delivered at once. */
void bt_port_poll(void);

#ifdef NEUPOD_BT_PORT_HCI_UP
/** Optional (a port defines NEUPOD_BT_PORT_HCI_UP for the whole build; the others pay nothing for it). Called on BTstack's thread when HCI is
 * working, before the adapter reports the radio ON: run extra controller commands through BTstack's command flow, then call @p done() once, on
 * BTstack's thread. Returns false if there is nothing to do (done() is then not called). A command that fails or is never answered is logged
 * and skipped; it must never keep HCI from coming up. Used at every HCI-up: first start and after each controller power-on. */
bool bt_port_hci_up(void (*done)(void));

/** Optional, same guard. Length of one inquiry round in 1.28 s units, or 0 for the default (one inquiry covering the whole scan). When non-zero the
 * adapter runs rounds of this length (the last one shortened) and starts another as long as the scan window has at least a whole unit left. Called
 * on BTstack's thread once at the start of each scan (the value is kept for its rounds). */
uint8_t bt_port_inquiry_round_units(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_PORT_H

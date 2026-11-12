// SPDX-License-Identifier: MPL-2.0
/*
 * bt_port.h on the RP2350 + CYW43439: HCI over the CYW43's shared bus, BTstack on the CYW43 driver's async context (the worker task the
 * SDK's pico_cyw43_arch_sys_freertos creates on core 0). RTOS build only.
 *
 * NO FLASH. The SDK's own btstack_cyw43_init() installs a TLV store in the internal flash (pico_flash, flash_safe_execute). Core 1
 * executes from the same XIP flash and is not lockout-aware, so that would crash the audio core: this file provides
 * btstack_cyw43_init()/btstack_cyw43_deinit() itself (the SDK's cyw43_arch_init() calls them) and the SDK's btstack_cyw43.c,
 * pico_btstack_flash_bank and pico_flash are NOT linked. Link keys go to the SD-card settings store through hal_bt_storage_t
 * (bt_btstack.c).
 */
#include "bt_port.h"

#include "btstack.h"
#include "pico/async_context.h"
#include "pico/btstack_cyw43.h"
#include "pico/btstack_hci_transport_cyw43.h"
#include "pico/btstack_run_loop_async_context.h"
#include "pico/cyw43_arch.h"

/* ---- the SDK's cyw43_arch_init() hooks ---- */

bool btstack_cyw43_init(async_context_t *context) {
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_async_context_get_instance(context));
    hci_init(hci_transport_cyw43_instance(), NULL);
    return true;
}

void btstack_cyw43_deinit(__unused async_context_t *context) {
    hci_power_control(HCI_POWER_OFF);
    hci_close();
    btstack_run_loop_async_context_deinit();
    btstack_run_loop_deinit();
    btstack_memory_deinit();
}

/* ---- bt_port.h ---- */

bool bt_port_present(void) { return cyw43_arch_async_context() != NULL; }

bool bt_port_start(void) { return true; } /* cyw43_arch_init() did it, through btstack_cyw43_init() */

bool bt_port_run_sync(bt_port_fn fn, void *arg) {
    (void)async_context_execute_sync(cyw43_arch_async_context(), fn, arg);
    return true;
}

void bt_port_post(btstack_context_callback_registration_t *reg) { btstack_run_loop_execute_on_main_thread(reg); }

int bt_port_power(bool on) { return hci_power_control(on ? HCI_POWER_ON : HCI_POWER_OFF); }

void bt_port_deliver(hal_bt_event_fn cb, void *ctx, const hal_bt_event_t *ev) { cb(ev, ctx); }

void bt_port_poll(void) {}

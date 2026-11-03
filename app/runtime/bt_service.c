// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "bt_service.h"
#include "os.h"
#include "settings_store.h"

#define EVENT_QUEUE_LEN 16u

static os_queue_t *g_q;

// Runs on the radio worker (hardware) or the calling task (host simulator): must not block. If the UI does not keep up the
// event is dropped, never the radio worker held up; the scan table is rebuilt by the next scan, and states are also readable
// with hal_bt_state().
static void on_event(const hal_bt_event_t *ev, void *ctx) {
    (void)ctx;
    if (g_q != NULL) {
        (void)os_queue_send(g_q, ev, OS_NO_WAIT);
    }
}

void bt_service_init(void) { g_q = os_queue_create(sizeof(hal_bt_event_t), EVENT_QUEUE_LEN); }

hal_status_t bt_service_start(void) {
    if (g_q == NULL) {
        return HAL_ERR;
    }
    return hal_bt_init(on_event, NULL, settings_bt_storage());
}

bool bt_service_event_poll(hal_bt_event_t *out) { return g_q != NULL && os_queue_recv(g_q, out, OS_NO_WAIT); }

void bt_service_event_flush(void) {
    hal_bt_event_t e;
    while (bt_service_event_poll(&e)) {
    }
}

void bt_service_tick(void) { hal_bt_poll(); }

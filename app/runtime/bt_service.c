// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "audio_route.h"
#include "bt_control.h"
#include "bt_reconnect.h"
#include "bt_saved.h"
#include "bt_service.h"
#include "os.h"
#include "runtime_config.h"
#include "settings_store.h"

#define EVENT_QUEUE_LEN 16u

static os_queue_t *g_q;
static bt_rc_t g_rc;              /* UI tick only */
static volatile bool g_lost;      /* set by the radio event, taken by the tick */
static bool g_powerup_done;

// Runs on the radio worker (hardware) or the calling task (host simulator): must not block. If the UI does not keep up the
// event is dropped, never the radio worker held up; the scan table is rebuilt by the next scan, and states are also readable
// with hal_bt_state().
static void on_event(const hal_bt_event_t *ev, void *ctx) {
    (void)ctx;
    if (ev->type == HAL_BT_EVT_STREAM) {
        return; /* the audio routing reads the stream state itself (audio_route.c) */
    }
    if (ev->type == HAL_BT_EVT_KEY || ev->type == HAL_BT_EVT_VOLUME) {
        bt_control_on_event(ev); /* headset buttons are queued for the monitor task; the volume is read from the HAL */
        return;
    }
    if (ev->type == HAL_BT_EVT_LINK) {
        if (ev->link == HAL_BT_LINK_FAILED && ev->reason == HAL_BT_FAIL_LINK_LOST) {
            g_lost = true; /* the UI tick starts the reconnecting (the policy is not thread safe) */
        }
        /* Link changes are not queued: screens read hal_bt_link_state(). A finished Pair is remembered here, on the radio worker, so it does
         * not depend on any screen being open (the settings store takes its own lock; this is RAM only, the card is written by the storage task). */
        bt_saved_on_event(ev);
        return;
    }
    if (g_q != NULL) {
        (void)os_queue_send(g_q, ev, OS_NO_WAIT);
    }
}

static os_mutex_t *g_saved_mu;
static void saved_lock(void) { (void)os_mutex_lock(g_saved_mu, OS_WAIT_FOREVER); }
static void saved_unlock(void) { os_mutex_unlock(g_saved_mu); }

void bt_service_init(void) {
    g_q = os_queue_create(sizeof(hal_bt_event_t), EVENT_QUEUE_LEN);
    g_saved_mu = os_mutex_create(); /* the stale-key queue is written by the radio worker and the UI task */
    if (g_saved_mu != NULL) {
        bt_saved_set_lock(saved_lock, saved_unlock);
    }
}

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

hal_status_t bt_service_pair(const hal_bt_device_t *dev) {
    /* Wi-Fi coexistence policy point (runtime_config.h NEUPOD_BT_WIFI_COEX_ALLOW): allowed, nothing is checked. */
    bt_rc_user_action(&g_rc);
    hal_bt_device_t d = *dev;
    if (!bt_saved_pair_begin(&d)) { /* before the request: the first event for it can arrive at once */
        return HAL_EBUSY; /* a Pair is already in flight */
    }
    hal_status_t st = hal_bt_pair(&d);
    if (st != HAL_OK) {
        bt_saved_pair_abort();
    }
    return st;
}

static hal_status_t do_connect(void);

hal_status_t bt_service_connect(void) {
    bt_rc_user_action(&g_rc); /* the user took over */
    return do_connect();
}

static hal_status_t do_connect(void) {
    hal_bt_device_t d;
    if (!bt_saved_get(&d)) {
        return HAL_ENODEV; /* nothing remembered */
    }
    bt_saved_connect_begin();
    return hal_bt_connect(d.addr); /* coexistence policy point, as in bt_service_pair() */
}

void bt_service_disconnect(void) {
    bt_rc_user_action(&g_rc);
    audio_route_user_released(); /* a headset let go on purpose is no longer the output once its link is down */
    hal_bt_disconnect();
}

void bt_service_forget(void) {
    bt_rc_user_action(&g_rc);
    audio_route_user_released();
    hal_bt_device_t d;
    if (bt_saved_get(&d)) {
        /* Disconnects first if it is the current peer, and deletes the link key (straight from the store when the stack is down). If the radio
         * cannot take the request now, it is retried by the tick. */
        hal_bt_device_t keep = d;
        if (bt_saved_delete_key(keep.addr) == HAL_EBUSY) {
            bt_saved_queue_delete(keep.addr);
        }
    }
    bt_saved_clear();
}

bool bt_service_reconnecting(void) { return bt_rc_active(&g_rc); }

static void reconnect_tick(void) {
    const uint32_t now = os_tick_ms();
    if (g_lost) {
        g_lost = false;
        hal_bt_device_t d;
        if (bt_saved_get(&d)) {
            bt_rc_lost(&g_rc, now);
        }
    }
    if (!g_powerup_done && hal_bt_state() == HAL_BT_ON && settings_generation() != 0u) { /* the settings are loaded: the remembered headset is known */
        g_powerup_done = true;
        hal_bt_device_t d;
        if (bt_saved_get(&d) && hal_bt_link_state() == HAL_BT_LINK_DISCONNECTED) {
            bt_rc_powerup(&g_rc, now);
        }
    }
    bt_rc_in_t in = {.bt_on = hal_bt_state() == HAL_BT_ON, .have_saved = bt_saved_get(NULL), .link = hal_bt_link_state()};
    if (bt_rc_step(&g_rc, now, &in)) {
        if (do_connect() == HAL_OK) {
            bt_rc_attempt_made(&g_rc);
            printf("bt: reconnecting\n");
        } else {
            bt_rc_attempt_refused(&g_rc, now);
        }
    }
}

void bt_service_tick(void) {
    hal_bt_poll();
    reconnect_tick();
    bt_saved_drain_stale(); /* keys of a replaced headset, of a Pair that did not finish or could not be saved */
}

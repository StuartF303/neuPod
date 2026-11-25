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
    audio_route_connect_cancel(); /* a Play that was waiting for the headset stops waiting */
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
    audio_route_connect_cancel();
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

static volatile uint32_t g_gave_up;
uint32_t bt_service_gave_up_seq(void) { return g_gave_up; }
static volatile uint32_t g_lost_seq, g_back_seq;
uint32_t bt_service_lost_seq(void) { return g_lost_seq; }
uint32_t bt_service_reconnected_seq(void) { return g_back_seq; }

bool bt_service_reconnecting(void) { return bt_rc_active(&g_rc); }

static uint32_t g_play_seen; /* audio_route_connect_request_seq() taken in so far */

static bool connect_call(void *ctx) {
    (void)ctx;
    if (do_connect() != HAL_OK) {
        return false;
    }
    printf("bt: reconnecting\n");
    return true;
}

static void reconnect_tick(void) {
    const uint32_t now = os_tick_ms();
    if (g_lost) {
        g_lost = false;
        /* Not while a schedule runs: a reconnect attempt that fails with LINK_LOST (the ACL dropped during its SDP query) is one more failed attempt, not a
         * fresh loss; re-arming would restart the 2 s step and the 5 minute give-up for ever. */
        if (!bt_rc_active(&g_rc) && bt_saved_get(NULL)) {
            bt_rc_lost(&g_rc, now);
            g_lost_seq++; /* a headset that was connected went away (not a power-up attempt, not a failed attempt): the UI says so */
        }
    }
    if (!g_powerup_done && hal_bt_state() == HAL_BT_ON && settings_generation() != 0u) { /* the settings are loaded: the remembered headset is known */
        g_powerup_done = true;
        hal_bt_device_t d;
        if (bt_saved_get(&d) && hal_bt_link_state() == HAL_BT_LINK_DISCONNECTED) {
            bt_rc_powerup(&g_rc, now);
        }
    }
    /* Play with the headset away (audio_route.c asks from the monitor task; the policy is UI-task only, so the request is taken in here). A running
     * schedule is joined as it is; otherwise a short one of its own starts, and it ends when the route stops waiting (paused, or the wait ran out). */
    const bt_rc_in_t in = {.bt_on = hal_bt_state() == HAL_BT_ON, .have_saved = bt_saved_get(NULL), .link = hal_bt_link_state()};
    switch (bt_rc_service(&g_rc, now, &in, audio_route_holding(), audio_route_connect_request_seq(), &g_play_seen, connect_call, NULL)) {
    case BT_RC_ENDED_AWAY:
        /* The schedule ended with the headset still away (a user action ends it through bt_rc_user_action, not here). While the route is waiting
         * for it, the route's own "Couldn't connect" notice says so and pauses; the generic toast would only say it twice. */
        if (audio_route_holding()) {
            audio_route_connect_gave_up();
        } else {
            g_gave_up++;
        }
        break;
    case BT_RC_ENDED_BACK:
        g_back_seq++; /* the schedule ended because the link is up again */
        break;
    default:
        break;
    }
}

#if NEUPOD_POWER_MGMT
uint8_t bt_service_sleep(void) {
    const hal_bt_state_t st = hal_bt_state();
    if (st == HAL_BT_OFF || st == HAL_BT_FAILED || st == HAL_BT_STARTING) {
        return 0;
    }
    uint8_t flags = BT_SLEEP_WAS_ON;
    if (hal_bt_link_state() == HAL_BT_LINK_CONNECTED || bt_rc_active(&g_rc)) {
        flags |= BT_SLEEP_WAS_LINKED;
    }
    bt_rc_user_action(&g_rc); /* no reconnect attempt may start while the controller goes away */
    hal_status_t r = hal_bt_power(false);
    g_lost = false; /* the link that ended with it is not a loss to reconnect from: the wake does that */
    if (r != HAL_OK) {
        printf("bt: could not power the controller off for sleep (%d)\n", (int)r);
        return r == HAL_ETIMEOUT ? flags : 0u; /* a controller that may still be on: the wake looks again */
    }
    printf("bt: controller off for sleep\n");
    return flags;
}

void bt_service_wake(uint8_t flags) {
    if ((flags & BT_SLEEP_WAS_ON) == 0) {
        return;
    }
    if (hal_bt_power(true) != HAL_OK) {
        printf("bt: could not power the controller on after sleep\n");
        return;
    }
    g_powerup_done = (flags & BT_SLEEP_WAS_LINKED) == 0; /* reconnect_tick() reconnects once HCI is up, but only if a headset was in use */
    g_lost = false;
    printf("bt: controller on after sleep\n");
}
#endif

void bt_service_tick(void) {
    hal_bt_poll();
    reconnect_tick();
    bt_saved_drain_stale(); /* keys of a replaced headset, of a Pair that did not finish or could not be saved */
}

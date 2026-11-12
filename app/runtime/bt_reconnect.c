// SPDX-License-Identifier: MPL-2.0
#include "bt_reconnect.h"

#include <string.h>

uint32_t bt_rc_delay_ms(uint32_t failed) {
    static const uint32_t k[] = {2000u, 5000u, 10000u};
    return failed < 3u ? k[failed] : 30000u;
}

static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

void bt_rc_init(bt_rc_t *rc) { memset(rc, 0, sizeof *rc); }

static void arm(bt_rc_t *rc, uint32_t now, uint32_t first_delay) {
    rc->armed = true;
    rc->attempting = false;
    rc->failed = 0;
    rc->start_ms = now;
    rc->due_ms = now + first_delay;
}

void bt_rc_lost(bt_rc_t *rc, uint32_t now) { arm(rc, now, bt_rc_delay_ms(0)); }
void bt_rc_powerup(bt_rc_t *rc, uint32_t now) {
    arm(rc, now, 0);
    rc->failed = UINT32_MAX; /* the first attempt is at once; its failure counts as step 0 of the schedule (2 s next), not step 1 */
}
void bt_rc_user_action(bt_rc_t *rc) { bt_rc_init(rc); }

static void failed_once(bt_rc_t *rc, uint32_t now) {
    rc->attempting = false;
    rc->failed++;
    rc->due_ms = now + bt_rc_delay_ms(rc->failed);
}

bool bt_rc_step(bt_rc_t *rc, uint32_t now, const bt_rc_in_t *in) {
    if (!rc->armed) {
        return false;
    }
    if (!in->have_saved || reached(now, rc->start_ms + BT_RC_GIVE_UP_MS)) {
        bt_rc_init(rc); /* forgotten meanwhile, or given up */
        return false;
    }
    if (rc->attempting) {
        if (in->link == HAL_BT_LINK_CONNECTED) {
            bt_rc_init(rc); /* done */
        } else if (in->link != HAL_BT_LINK_CONNECTING) {
            failed_once(rc, now); /* FAILED (not found, timed out ...) or ended: the next one later */
        }
        return false;
    }
    if (in->link == HAL_BT_LINK_CONNECTED) {
        bt_rc_init(rc); /* connected by other means */
        return false;
    }
    /* held back, not given up: a scan, the radio not up, or a Pair / Connect of someone else */
    if (!in->bt_on || in->link == HAL_BT_LINK_PAIRING || in->link == HAL_BT_LINK_CONNECTING) {
        return false;
    }
    return reached(now, rc->due_ms);
}

void bt_rc_attempt_made(bt_rc_t *rc) { rc->attempting = true; }
void bt_rc_attempt_refused(bt_rc_t *rc, uint32_t now) { failed_once(rc, now); }
bool bt_rc_active(const bt_rc_t *rc) { return rc->armed; }

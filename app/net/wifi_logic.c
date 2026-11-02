// SPDX-License-Identifier: MPL-2.0
#include "wifi_logic.h"

void wifi_backoff_init(wifi_backoff_t *b) {
    b->delay_ms = WIFI_BACKOFF_MIN_MS;
    b->retry_at = 0;
    b->failures = 0;
    b->reconnects = 0;
    b->joined_before = false;
}

void wifi_backoff_fail(wifi_backoff_t *b, uint32_t now_ms) {
    b->failures++;
    b->retry_at = now_ms + b->delay_ms;
    b->delay_ms = b->delay_ms * 2u > WIFI_BACKOFF_MAX_MS ? WIFI_BACKOFF_MAX_MS : b->delay_ms * 2u;
}

void wifi_backoff_success(wifi_backoff_t *b) {
    if (b->joined_before) {
        b->reconnects++;
    }
    b->joined_before = true;
    b->delay_ms = WIFI_BACKOFF_MIN_MS;
    b->failures = 0;
}

bool wifi_backoff_due(const wifi_backoff_t *b, uint32_t now_ms) { return (int32_t)(now_ms - b->retry_at) >= 0; }

wifi_power_t wifi_policy_decide(const wifi_policy_in_t *in, uint32_t now_ms) {
    if (!in->wifi_enabled) {
        return WIFI_PW_OFF;
    }
    if (in->playing && !in->during_playback) {
        return WIFI_PW_OFF; /* never compete with audio for the shared radio unless asked to */
    }
    if (in->want_online || in->idle_off_min == 0) {
        return WIFI_PW_ON;
    }
    return now_ms - in->idle_since_ms >= in->idle_off_min * 60000u ? WIFI_PW_OFF : WIFI_PW_ON;
}

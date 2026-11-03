// SPDX-License-Identifier: MPL-2.0
/**
 * @file wifi_logic.h
 * @brief Wi-Fi connection policy as pure state machines (host-tested): reconnect backoff and the idle/power policy.
 */
#ifndef NEUPOD_WIFI_LOGIC_H
#define NEUPOD_WIFI_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reconnect backoff: after each failed attempt wait 2, 4, 8 ... up to 120 s (plus none of the attempts in between);
 * a success resets it. Time is a caller-supplied millisecond counter (wraps are handled by signed differences). */
#define WIFI_BACKOFF_MIN_MS 2000u
#define WIFI_BACKOFF_MAX_MS 120000u
typedef struct {
    uint32_t delay_ms;   /* current wait */
    uint32_t retry_at;   /* when the next attempt may start (only meaningful when has_retry) */
    bool has_retry;      /* a failure armed retry_at; without it an attempt is always due (a 0 deadline would read as 'in the
                            future' once the millisecond counter passes 2^31, after 24.8 days) */
    uint32_t failures;   /* in a row */
    uint32_t reconnects; /* successful joins after the first */
    bool joined_before;
} wifi_backoff_t;
void wifi_backoff_init(wifi_backoff_t *b);
void wifi_backoff_fail(wifi_backoff_t *b, uint32_t now_ms);
void wifi_backoff_success(wifi_backoff_t *b);
bool wifi_backoff_due(const wifi_backoff_t *b, uint32_t now_ms);

/* Join deadline: a join (association plus DHCP) that is still JOINING or NO_IP this long after it started is given up (the
 * radio can sit in 'joined, no address' for ever). Wrap-safe: elapsed time is an unsigned difference. */
static inline bool wifi_join_timed_out(uint32_t started_ms, uint32_t now_ms, uint32_t timeout_ms) {
    uint32_t elapsed = now_ms - started_ms;
    return elapsed < 0x80000000u && elapsed >= timeout_ms; /* a 'now' slightly before 'started' is not a timeout */
}

/* Security of a scanned network from the CYW43 driver's scan result bits (cyw43_ll.c: 4 = an RSN element, 2 = a WPA element,
 * 1 = the privacy capability, which every encrypted network sets). The driver cannot tell WPA3 from WPA2 (both carry an RSN
 * element), so an RSN network is reported as WPA2: the join tries WPA2, WPA2/WPA3 and WPA3-only in turn anyway. WPA1-only and WEP
 * networks are "other" (secured, not offered a mode here). */
static inline hal_wifi_sec_t wifi_sec_from_scan_bits(unsigned bits) {
    if ((bits & 4u) != 0) {
        return HAL_WIFI_SEC_WPA2;
    }
    return (bits & 3u) != 0 ? HAL_WIFI_SEC_OTHER : HAL_WIFI_SEC_OPEN;
}

/* Power policy. Wi-Fi is OFF while playing (unless "Wi-Fi during playback" is On) and the radio is switched off after
 * @p idle_off_min minutes without a need for it (0 = never). Inputs are sampled by the service each second. */
typedef struct {
    bool wifi_enabled;       /* the user's master switch */
    bool playing;            /* audio is playing */
    bool during_playback;    /* the setting */
    bool want_online;        /* something needs the network now (a scan, a join request, a transfer, an NTP resync) */
    uint32_t idle_since_ms;  /* last time anything needed the radio */
    uint32_t idle_off_min;
} wifi_policy_in_t;
typedef enum { WIFI_PW_OFF, WIFI_PW_ON } wifi_power_t;
/** What the radio should be doing now. @p now_ms for the idle timer; the caller keeps idle_since_ms. */
wifi_power_t wifi_policy_decide(const wifi_policy_in_t *in, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

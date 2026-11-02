// SPDX-License-Identifier: MPL-2.0
/**
 * @file wifi_logic.h
 * @brief Wi-Fi connection policy as pure state machines (host-tested): reconnect backoff and the idle/power policy.
 */
#ifndef NEUPOD_WIFI_LOGIC_H
#define NEUPOD_WIFI_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reconnect backoff: after each failed attempt wait 2, 4, 8 ... up to 120 s (plus none of the attempts in between);
 * a success resets it. Time is a caller-supplied millisecond counter (wraps are handled by signed differences). */
#define WIFI_BACKOFF_MIN_MS 2000u
#define WIFI_BACKOFF_MAX_MS 120000u
typedef struct {
    uint32_t delay_ms;   /* current wait */
    uint32_t retry_at;   /* when the next attempt may start */
    uint32_t failures;   /* in a row */
    uint32_t reconnects; /* successful joins after the first */
    bool joined_before;
} wifi_backoff_t;
void wifi_backoff_init(wifi_backoff_t *b);
void wifi_backoff_fail(wifi_backoff_t *b, uint32_t now_ms);
void wifi_backoff_success(wifi_backoff_t *b);
bool wifi_backoff_due(const wifi_backoff_t *b, uint32_t now_ms);

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

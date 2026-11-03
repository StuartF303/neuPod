// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <string.h>

#include <stdatomic.h>
#include <stdlib.h>

#include "capabilities.h"
#include "os.h"
#include "player.h"
#include "settings_store.h"
#include "wifi_logic.h"
#include "wifi_service.h"

#define TICK_MS 200u
#define ONLINE_GRACE_MS 45000u /* after a user request, keep the radio on at least this long */

typedef enum { REQ_SCAN, REQ_JOIN, REQ_LEAVE, REQ_TEST } req_kind_t;
typedef struct {
    req_kind_t kind;
    bool save;
    char ssid[HAL_WIFI_SSID_MAX + 1];
    char psk[HAL_WIFI_PSK_MAX + 1];
    bool use_saved;
    uint32_t gen; /* REQ_SCAN: the scan generation it was made for (see g_scan_gen) */
} req_t;

static os_queue_t *g_q;
static os_mutex_t *g_mu;
static wifi_info_t g_info;
static wifi_settings_t g_set;
static bool g_stack_up;
static uint32_t g_gen_seen = 0xFFFFFFFFu;

static wifi_backoff_t g_backoff;
static uint32_t g_idle_since, g_online_until;
static bool g_online_armed; /* g_online_until is only meaningful once a request set it (0 would look like the future after 2^31 ms) */
static bool g_user_left;       /* the user disconnected: do not auto-join again until asked */
static bool g_joining_user;    /* current attempt came from a request */
static bool g_save_on_success;
static char g_cur_ssid[HAL_WIFI_SSID_MAX + 1], g_cur_psk[HAL_WIFI_PSK_MAX + 1];
static hal_wifi_status g_prev_state = HAL_WIFI_DOWN;
static uint32_t g_next_auto; /* round robin over saved networks */
static uint32_t g_nothing_until; /* with nothing saved: look again at this time (a plain timer, not a join failure) */
static bool g_nothing_armed;
#define NOTHING_SAVED_RECHECK_MS 10000u
static char g_bad_auth[WIFI_MAX_SAVED][HAL_WIFI_SSID_MAX + 1];
static bool g_save_failed; /* the last 'remember this network' did not fit (surfaced in the UI) */
static bool g_scan_active;  /* a scan is running on the radio */
static uint32_t g_scan_count; /* networks reported by the scan in progress (UI task only: request and next) */
/* Scan generation (under g_mu): bumped by every scan request and every cancel. A queued request or a pending retry whose
 * generation is no longer current belongs to a screen that has gone away and is dropped, so a closed Networks screen can never
 * leave a scan running for nobody that a later screen would then read half way through (the radio's result buffer is reset
 * by each start, and is only read while g_scan_active is set for the current generation). */
static uint32_t g_scan_gen;
static bool g_scan_retry;   /* the radio was still busy with an earlier scan: start this one when it is free */
static uint32_t g_scan_retry_until;
#define SCAN_RETRY_MS 8000u
static bool g_scan_wanted;  /* a scan was requested (it may not have started yet: the task handles requests every 200 ms) */

/* The test counters are written from the lwIP thread, which holds the radio context lock: NO mutex there (it would be an AB-BA
 * deadlock with code that takes g_mu and then calls into the radio). Plain C11 atomics only. */
static atomic_int g_t_state, g_t_err;
static atomic_uint g_t_bytes, g_t_ms, g_t_kbps;
static uint32_t g_test_t0;
static char g_url[96] = "http://192.168.1.10:8000/test.bin";

static void lock(void) { (void)os_mutex_lock(g_mu, OS_WAIT_FOREVER); }
static void unlock(void) { os_mutex_unlock(g_mu); }

/* ---- settings ---- */
static void defaults(wifi_settings_t *s) {
    s->enabled = true;
    s->during_playback = false;
    s->idle_off_min = 10;
    s->zone = CLOCK_ZONE_LONDON;
    snprintf(s->ntp_server, sizeof s->ntp_server, "pool.ntp.org");
}

static void load_settings(void) {
    wifi_settings_t s;
    defaults(&s);
    uint8_t w[4];
    if (settings_get(SETTING_WIFI, w, sizeof w) == 4) {
        s.enabled = w[0] != 0;
        s.during_playback = w[1] != 0;
        s.idle_off_min = w[2];
        s.zone = w[3] < CLOCK_ZONE_COUNT ? (clock_zone_t)w[3] : CLOCK_ZONE_LONDON;
    }
    char n[WIFI_NTP_MAX];
    uint32_t nl = settings_get(SETTING_NTP_SERVER, n, sizeof n - 1);
    if (nl > 0 && nl < sizeof n) {
        n[nl] = '\0';
        snprintf(s.ntp_server, sizeof s.ntp_server, "%s", n);
    }
    char u[96];
    uint32_t ul = settings_get(SETTING_TEST_URL, u, sizeof u - 1);
    if (ul >= sizeof u) {
        ul = 0; /* longer than we ever store: ignore */
    }
    lock();
    g_set = s;
    if (ul > 0) {
        u[ul] = '\0';
        snprintf(g_url, sizeof g_url, "%s", u);
    }
    unlock();
    clock_set_zone(s.zone);
}

void wifi_get_settings(wifi_settings_t *out) {
    lock();
    *out = g_set;
    unlock();
}

void wifi_set_settings(const wifi_settings_t *in) {
    lock();
    g_set = *in;
    unlock();
    uint8_t w[4] = {in->enabled, in->during_playback, in->idle_off_min, (uint8_t)in->zone};
    (void)settings_put(SETTING_WIFI, w, sizeof w);
    (void)settings_put(SETTING_NTP_SERVER, in->ntp_server, (uint32_t)strlen(in->ntp_server));
    clock_set_zone(in->zone);
}

void wifi_test_get_url(char *out, uint32_t cap) {
    lock();
    snprintf(out, cap, "%s", g_url);
    unlock();
}
void wifi_test_set_url(const char *url) {
    lock();
    snprintf(g_url, sizeof g_url, "%s", url);
    unlock();
    (void)settings_put(SETTING_TEST_URL, url, (uint32_t)strlen(url));
}

/* ---- saved networks ---- */
uint32_t wifi_saved_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < WIFI_MAX_SAVED; i++) {
        n += settings_get(SETTING_NET_SSID(i), NULL, 0) > 1;
    }
    return n;
}

static int slot_of(const char *ssid) {
    for (uint32_t i = 0; i < WIFI_MAX_SAVED; i++) {
        uint8_t b[HAL_WIFI_SSID_MAX + 2];
        uint32_t l = settings_get(SETTING_NET_SSID(i), b, sizeof b);
        if (l > 1 && l - 1 == strlen(ssid) && memcmp(b + 1, ssid, l - 1) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool wifi_saved_get(uint32_t idx, char ssid[HAL_WIFI_SSID_MAX + 1], bool *has_psk) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < WIFI_MAX_SAVED; i++) {
        uint8_t b[HAL_WIFI_SSID_MAX + 2];
        uint32_t l = settings_get(SETTING_NET_SSID(i), b, sizeof b);
        if (l > 1 && seen++ == idx) {
            memcpy(ssid, b + 1, l - 1);
            ssid[l - 1] = '\0';
            if (has_psk != NULL) {
                *has_psk = b[0] != 0;
            }
            return true;
        }
    }
    return false;
}

void wifi_forget(uint32_t idx) {
    char s[HAL_WIFI_SSID_MAX + 1];
    if (!wifi_saved_get(idx, s, NULL)) {
        return;
    }
    int slot = slot_of(s);
    if (slot >= 0) {
        settings_delete(SETTING_NET_SSID((uint32_t)slot));
        settings_delete(SETTING_NET_PSK((uint32_t)slot));
    }
}

static bool saved_psk(const char *ssid, char *psk, bool *needs) {
    int slot = slot_of(ssid);
    *needs = false;
    psk[0] = '\0';
    if (slot < 0) {
        return false;
    }
    uint8_t b[HAL_WIFI_SSID_MAX + 2];
    settings_get(SETTING_NET_SSID((uint32_t)slot), b, sizeof b);
    if (b[0] != 0) {
        uint32_t n = settings_get_secret(SETTING_NET_PSK((uint32_t)slot), psk, HAL_WIFI_PSK_MAX);
        if (n == 0) {
            *needs = true; /* it should have a password and it does not decrypt: ask again */
        }
        psk[n] = '\0';
    }
    return true;
}

bool wifi_saved_needs_password(const char *ssid) {
    char p[HAL_WIFI_PSK_MAX + 1];
    bool needs;
    bool known = saved_psk(ssid, p, &needs);
    lock();
    for (uint32_t i = 0; i < WIFI_MAX_SAVED; i++) {
        if (g_bad_auth[i][0] != '\0' && strcmp(g_bad_auth[i], ssid) == 0) {
            needs = true;
        }
    }
    unlock();
    return known && needs;
}

static bool save_network(const char *ssid, const char *psk) {
    bool ok = true;
    int slot = slot_of(ssid);
    if (slot < 0) {
        for (uint32_t i = 0; i < WIFI_MAX_SAVED && slot < 0; i++) {
            if (settings_get(SETTING_NET_SSID(i), NULL, 0) == 0) {
                slot = (int)i;
            }
        }
    }
    if (slot < 0) {
        slot = (int)(g_next_auto++ % WIFI_MAX_SAVED); /* all four used: replace one (the oldest by rotation) */
    }
    uint8_t b[HAL_WIFI_SSID_MAX + 2];
    size_t n = strlen(ssid);
    b[0] = psk[0] != '\0';
    memcpy(b + 1, ssid, n);
    ok = settings_put(SETTING_NET_SSID((uint32_t)slot), b, (uint32_t)n + 1u);
    if (psk[0] != '\0') {
        ok = settings_put_secret(SETTING_NET_PSK((uint32_t)slot), psk, (uint32_t)strlen(psk)) && ok;
    } else {
        settings_delete(SETTING_NET_PSK((uint32_t)slot));
    }
    return ok;
}

/* ---- requests ---- */
static bool post(const req_t *r) { return g_q != NULL && os_queue_send(g_q, r, OS_NO_WAIT); }

static const char *sec_name(hal_wifi_sec_t s) {
    switch (s) {
    case HAL_WIFI_SEC_OPEN: return "open";
    case HAL_WIFI_SEC_WPA2: return "WPA2";
    case HAL_WIFI_SEC_WPA3: return "WPA3";
    case HAL_WIFI_SEC_WPA2_WPA3: return "WPA2/3";
    default: return "other";
    }
}

bool wifi_scan_request(void) {
    g_scan_count = 0;
    lock();
    uint32_t gen = ++g_scan_gen;
    g_scan_wanted = true;
    g_scan_active = g_scan_retry = false;
    unlock();
    if (!post(&(req_t){.kind = REQ_SCAN, .gen = gen})) {
        lock();
        if (g_scan_gen == gen) {
            g_scan_wanted = false;
        }
        unlock();
        return false;
    }
    return true;
}
bool wifi_scan_next(hal_wifi_ap_t *out, bool *done) {
    lock();
    bool act = g_scan_active, pending = g_scan_wanted && !g_scan_active;
    unlock();
    if (pending) {
        *done = false; /* requested, the task has not started it yet */
        return false;
    }
    if (!act) {
        *done = true;
        return false;
    }
    bool got = hal_wifi_scan_next(out, done);
    if (got) {
        g_scan_count++;
        char shown[HAL_WIFI_SSID_MAX + 1];
        hal_log_text(shown, sizeof shown, out->ssid);
        printf("wifi: scan: '%s' rssi=%d sec=%s ch=%u\n", shown, (int)out->rssi, sec_name(out->sec), (unsigned)out->channel);
    }
    if (*done) {
        printf("wifi: scan done, %u networks\n", (unsigned)g_scan_count);
        lock();
        g_scan_wanted = g_scan_active = g_scan_retry = false;
        unlock();
    }
    return got;
}
void wifi_scan_cancel(void) {
    lock();
    g_scan_gen++; /* a request still in the queue, or in the middle of starting, is now stale */
    g_scan_wanted = g_scan_active = g_scan_retry = false; /* the radio finishes an abandoned scan by itself; its results are dropped */
    unlock();
}

bool wifi_connect_request(const char *ssid, const char *psk, bool save) {
    req_t r = {.kind = REQ_JOIN, .save = save, .use_saved = psk == NULL};
    snprintf(r.ssid, sizeof r.ssid, "%s", ssid);
    if (psk != NULL) {
        snprintf(r.psk, sizeof r.psk, "%s", psk);
    }
    return post(&r);
}
void wifi_disconnect_request(void) { (void)post(&(req_t){.kind = REQ_LEAVE}); }
bool wifi_test_start(void) { return post(&(req_t){.kind = REQ_TEST}); }
void wifi_test_get(wifi_test_t *out) {
    out->state = atomic_load_explicit(&g_t_state, memory_order_acquire);
    out->bytes = atomic_load(&g_t_bytes);
    out->ms = atomic_load(&g_t_ms);
    out->kbps = atomic_load(&g_t_kbps);
    out->err = atomic_load(&g_t_err);
}
bool wifi_service_stack_up(void) { return g_stack_up; }

void wifi_get_info(wifi_info_t *out) {
    lock();
    *out = g_info;
    unlock();
}

/* ---- the task ---- */
static void on_time(uint32_t unix_s) {
    if (!clock_ntp_plausible(unix_s)) {
        printf("wifi: ignoring an implausible NTP time (%u)\n", (unsigned)unix_s);
        return;
    }
    if (clock_set_utc(unix_s, true)) {
        printf("wifi: time set from NTP\n");
    }
}

static void radio_on(void) {
    if (hal_wifi_state() == HAL_WIFI_DOWN) {
        (void)hal_wifi_start(); /* only when it was really off: a scan or a test must not disturb a running SNTP client */
    }
}

static void start_join(const char *ssid, const char *psk, bool user, bool save) {
    snprintf(g_cur_ssid, sizeof g_cur_ssid, "%s", ssid);
    snprintf(g_cur_psk, sizeof g_cur_psk, "%s", psk);
    g_joining_user = user;
    g_save_on_success = save;
    if (hal_wifi_join(ssid, psk[0] != '\0' ? psk : NULL) != HAL_OK) {
        wifi_backoff_fail(&g_backoff, os_tick_ms());
    }
}

static void test_data(void *ctx, const uint8_t *d, uint32_t n) {
    (void)ctx;
    (void)d;
    atomic_fetch_add_explicit(&g_t_bytes, n, memory_order_relaxed); /* the target is /dev/null: only count */
}
static void test_done(void *ctx, int err, uint32_t total) {
    (void)ctx;
    uint32_t ms = os_tick_ms() - g_test_t0;
    atomic_store(&g_t_ms, ms);
    atomic_store(&g_t_bytes, total);
    atomic_store(&g_t_kbps, ms != 0 ? (uint32_t)((uint64_t)total * 1000u / 1024u / ms) : 0u);
    atomic_store(&g_t_err, err);
    atomic_store_explicit(&g_t_state, err == 0 ? WT_DONE : WT_FAILED, memory_order_release);
}

static void run_test(void) {
    char url[96];
    wifi_test_get_url(url, sizeof url);
    const char *p = strncmp(url, "http://", 7) == 0 ? url + 7 : url;
    char host[48];
    uint16_t port = 80;
    size_t i = 0;
    while (p[i] != '\0' && p[i] != '/' && p[i] != ':' && i < sizeof host - 1) {
        host[i] = p[i];
        i++;
    }
    host[i] = '\0';
    const char *rest = p + i;
    if (*rest == ':') {
        port = (uint16_t)strtoul(rest + 1, (char **)&rest, 10);
    }
    const char *path = *rest == '/' ? rest : "/";
    atomic_store(&g_t_bytes, 0);
    atomic_store(&g_t_ms, 0);
    atomic_store(&g_t_kbps, 0);
    atomic_store(&g_t_err, 0);
    atomic_store(&g_t_state, WT_RUNNING);
    g_test_t0 = os_tick_ms();
    if (hal_wifi_http_get(host, port, path, test_data, test_done, NULL) != HAL_OK) {
        atomic_store(&g_t_err, -2);
        atomic_store(&g_t_state, WT_FAILED);
    }
}

static void update_info(const wifi_policy_in_t *pin, wifi_power_t pw, hal_wifi_status st) {
    /* Gather first (hal_wifi_* take the radio context lock), then lock only to copy plain data: never hold g_mu across a HAL call. */
    wifi_info_t n;
    memset(&n, 0, sizeof n);
    n.state = st;
    n.radio_on = st != HAL_WIFI_DOWN;
    n.paused_for_playback = pw == WIFI_PW_OFF && pin->wifi_enabled && pin->playing && !pin->during_playback;
    if (st == HAL_WIFI_UP) {
        n.rssi = hal_wifi_rssi();
        (void)hal_wifi_ip(n.ip);
        (void)hal_wifi_ssid(n.ssid);
    }
    n.ntp_synced = clock_ntp_synced();
    n.save_failed = g_save_failed;
    n.saved = wifi_saved_count();
    lock();
    n.reconnects = g_backoff.reconnects;
    n.failures = g_backoff.failures;
    g_info = n;
    unlock();
}

void wifi_service_init(void) {
    g_q = os_queue_create(sizeof(req_t), 4);
    g_mu = os_mutex_create();
    defaults(&g_set);
    wifi_backoff_init(&g_backoff);
    g_info.state = HAL_WIFI_DOWN;
}

void wifi_task(void *arg) {
    (void)arg;
    g_stack_up = true; /* the radio driver was brought up by the boot check before this task was started */
    uint32_t last = os_tick_ms();
    uint32_t last_sec = 0;
    bool was_playing = false;
    g_idle_since = os_tick_ms();
    for (;;) {
        os_delay_until_ms(&last, TICK_MS);
        if (!caps_available(CAP_WIFI)) {
            continue; /* the boot check has not brought the radio driver up (yet, or at all) */
        }
        uint32_t now = os_tick_ms();
        uint32_t gen = settings_generation();
        if (gen != g_gen_seen) {
            g_gen_seen = gen;
            load_settings(); /* after the card's settings loaded (or defaults) */
        }
        hal_wifi_poll();
        clock_tick();

        /* a scan that had to wait for the radio */
        lock();
        bool retry = g_scan_retry;
        uint32_t retry_gen = g_scan_gen;
        unlock();
        if (retry) {
            hal_status_t sr = hal_wifi_scan_start();
            lock();
            if (retry_gen != g_scan_gen) {
                /* cancelled or replaced while starting: nobody waits for this one */
            } else if (sr == HAL_OK) {
                g_scan_retry = false;
                g_scan_active = true;
            } else if (sr != HAL_EBUSY || (int32_t)(now - g_scan_retry_until) >= 0) {
                g_scan_retry = g_scan_wanted = false; /* gave up: the screen sees "done" with nothing */
            }
            unlock();
        }

        /* requests */
        req_t r;
        while (os_queue_recv(g_q, &r, OS_NO_WAIT)) {
            g_online_until = now + ONLINE_GRACE_MS;
            g_online_armed = true;
            g_idle_since = now;
            radio_on();
            switch (r.kind) {
            case REQ_SCAN:
                {
                    lock();
                    bool current = r.gen == g_scan_gen;
                    unlock();
                    if (!current) {
                        break; /* the screen that asked has gone */
                    }
                    hal_status_t sr = hal_wifi_scan_start(); /* HAL call outside g_mu */
                    lock();
                    if (r.gen != g_scan_gen) {
                        unlock();
                        break; /* cancelled while it started: the radio scans for nobody and stops by itself */
                    }
                    g_scan_retry = sr == HAL_EBUSY; /* an earlier scan is still running (the user left and came back) */
                    g_scan_retry_until = now + SCAN_RETRY_MS;
                    g_scan_active = sr == HAL_OK;
                    g_scan_wanted = sr == HAL_OK || sr == HAL_EBUSY;
                    unlock();
                }
                break;
            case REQ_JOIN: {
                bool needs;
                char psk[HAL_WIFI_PSK_MAX + 1];
                if (r.use_saved) {
                    saved_psk(r.ssid, psk, &needs);
                    snprintf(r.psk, sizeof r.psk, "%s", psk);
                }
                g_user_left = false;
                lock();
                for (uint32_t i = 0; i < WIFI_MAX_SAVED; i++) { /* a fresh try clears the "wrong password" mark */
                    if (strcmp(g_bad_auth[i], r.ssid) == 0) {
                        g_bad_auth[i][0] = '\0';
                    }
                }
                unlock();
                hal_wifi_leave();
                start_join(r.ssid, r.psk, true, r.save);
                break;
            }
            case REQ_LEAVE:
                g_user_left = true;
                hal_wifi_leave();
                break;
            case REQ_TEST:
                run_test();
                break;
            }
        }

        if (atomic_load(&g_t_state) == WT_RUNNING && now - g_test_t0 > 60000u) {
            hal_wifi_http_abort(); /* a stalled transfer: end it and report */
        }

        /* power policy */
        player_info_t pi;
        player_get(&pi);
        bool playing = pi.state == PLAYER_PLAYING;
        wifi_settings_t s;
        wifi_get_settings(&s);
        hal_wifi_status st = hal_wifi_state();
        bool want = (g_online_armed && (int32_t)(g_online_until - now) > 0) || st == HAL_WIFI_JOINING || st == HAL_WIFI_NO_IP || atomic_load(&g_t_state) == WT_RUNNING;
        if (st == HAL_WIFI_UP && g_set.idle_off_min != 0 && !want) {
            /* joined and idle: the idle timer runs from the last time we needed the network */
        } else {
            g_idle_since = now;
        }
        wifi_policy_in_t pin = {.wifi_enabled = s.enabled, .playing = playing, .during_playback = s.during_playback,
                                .want_online = want, .idle_since_ms = g_idle_since, .idle_off_min = s.idle_off_min};
        wifi_power_t pw = wifi_policy_decide(&pin, now);
        if (pw == WIFI_PW_OFF && st != HAL_WIFI_DOWN) {
            printf("wifi: radio off (%s)\n", !s.enabled ? "disabled" : playing && !s.during_playback ? "playing" : "idle");
            hal_wifi_stop();
            st = HAL_WIFI_DOWN;
            g_prev_state = HAL_WIFI_DOWN;
        } else if (pw == WIFI_PW_ON && st == HAL_WIFI_DOWN && !g_user_left && wifi_saved_count() != 0) {
            radio_on(); /* come back after a playback pause or a manual enable */
            st = hal_wifi_state();
            wifi_backoff_init(&g_backoff);
        }
        was_playing = playing;

        /* state edges */
        if (st != g_prev_state) {
            if (st == HAL_WIFI_UP) {
                wifi_backoff_success(&g_backoff);
                g_nothing_armed = false;
                if (g_save_on_success && g_joining_user) {
                    g_save_failed = !save_network(g_cur_ssid, g_cur_psk);
                    if (g_save_failed) {
                        printf("wifi: could not save the network (settings full?)\n");
                    }
                    g_save_on_success = false;
                }
                (void)hal_wifi_sntp_start(g_set.ntp_server, on_time);
                char ip[16] = "-";
                (void)hal_wifi_ip(ip);
                char shown[HAL_WIFI_SSID_MAX + 1];
                hal_log_text(shown, sizeof shown, g_cur_ssid);
                printf("wifi: joined '%s' ip=%s rssi=%d\n", shown, ip, hal_wifi_rssi());
            } else if (st == HAL_WIFI_BAD_AUTH || st == HAL_WIFI_FAILED) {
                char shown[HAL_WIFI_SSID_MAX + 1];
                hal_log_text(shown, sizeof shown, g_cur_ssid);
                printf("wifi: join '%s' %s\n", shown, st == HAL_WIFI_BAD_AUTH ? "failed: wrong password" : "failed");
                if (st == HAL_WIFI_BAD_AUTH) {
                    lock();
                    snprintf(g_bad_auth[g_next_auto++ % WIFI_MAX_SAVED], sizeof g_bad_auth[0], "%s", g_cur_ssid);
                    unlock();
                }
                if (!g_joining_user || st != HAL_WIFI_BAD_AUTH) {
                    wifi_backoff_fail(&g_backoff, now);
                }
                g_joining_user = false;
            } else if (g_prev_state == HAL_WIFI_UP) {
                printf("wifi: link lost\n");
                wifi_backoff_fail(&g_backoff, now); /* start the reconnect timer */
            }
            g_prev_state = st;
        }

        /* scan finished? */
        /* auto-connect to a saved network */
        if (s.enabled && !g_user_left && st != HAL_WIFI_DOWN && (st == HAL_WIFI_IDLE || st == HAL_WIFI_FAILED || st == HAL_WIFI_BAD_AUTH) &&
            wifi_backoff_due(&g_backoff, now) && !(playing && !s.during_playback) &&
            !(g_nothing_armed && (int32_t)(now - g_nothing_until) < 0)) {
            uint32_t n = wifi_saved_count();
            g_nothing_armed = false;
            for (uint32_t k = 0; k < n; k++) {
                char ssid[HAL_WIFI_SSID_MAX + 1], psk[HAL_WIFI_PSK_MAX + 1];
                bool has, needs;
                uint32_t idx = (g_next_auto + k) % n;
                if (!wifi_saved_get(idx, ssid, &has) || wifi_saved_needs_password(ssid)) {
                    continue;
                }
                saved_psk(ssid, psk, &needs);
                g_next_auto = idx + 1u;
                char shown[HAL_WIFI_SSID_MAX + 1];
                hal_log_text(shown, sizeof shown, ssid);
                printf("wifi: joining saved network '%s'\n", shown);
                start_join(ssid, psk, false, false);
                break;
            }
            if (n == 0) { /* nothing saved: look again later. Nothing was attempted, so it is not a join failure. */
                g_nothing_until = now + NOTHING_SAVED_RECHECK_MS;
                g_nothing_armed = true;
            }
        }

        if (now - last_sec >= 1000u || last_sec == 0) {
            last_sec = now;
            update_info(&pin, pw, st);
            (void)was_playing;
        } else {
            lock();
            g_info.state = st;
            unlock();
        }
    }
}

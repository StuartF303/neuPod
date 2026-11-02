// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <string.h>

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
} req_t;

static os_queue_t *g_q;
static os_mutex_t *g_mu;
static wifi_info_t g_info;
static wifi_settings_t g_set;
static bool g_stack_up;
static uint32_t g_gen_seen = 0xFFFFFFFFu;

static wifi_backoff_t g_backoff;
static uint32_t g_idle_since, g_online_until;
static bool g_user_left;       /* the user disconnected: do not auto-join again until asked */
static bool g_joining_user;    /* current attempt came from a request */
static bool g_save_on_success;
static char g_cur_ssid[HAL_WIFI_SSID_MAX + 1], g_cur_psk[HAL_WIFI_PSK_MAX + 1];
static hal_wifi_status g_prev_state = HAL_WIFI_DOWN;
static uint32_t g_next_auto; /* round robin over saved networks */
static char g_bad_auth[WIFI_MAX_SAVED][HAL_WIFI_SSID_MAX + 1];
static bool g_scan_active;  /* a scan is running on the radio */
static bool g_scan_wanted;  /* a scan was requested (it may not have started yet: the task handles requests every 200 ms) */

static wifi_test_t g_test;
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
    for (uint32_t i = 0; i < WIFI_MAX_SAVED; i++) {
        if (g_bad_auth[i][0] != '\0' && strcmp(g_bad_auth[i], ssid) == 0) {
            needs = true;
        }
    }
    return known && needs;
}

static void save_network(const char *ssid, const char *psk) {
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
    (void)settings_put(SETTING_NET_SSID((uint32_t)slot), b, (uint32_t)n + 1u);
    if (psk[0] != '\0') {
        (void)settings_put_secret(SETTING_NET_PSK((uint32_t)slot), psk, (uint32_t)strlen(psk));
    } else {
        settings_delete(SETTING_NET_PSK((uint32_t)slot));
    }
}

/* ---- requests ---- */
static bool post(const req_t *r) { return g_q != NULL && os_queue_send(g_q, r, OS_NO_WAIT); }

bool wifi_scan_request(void) {
    lock();
    g_scan_wanted = true;
    g_scan_active = false;
    unlock();
    return post(&(req_t){.kind = REQ_SCAN});
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
    if (*done) {
        lock();
        g_scan_wanted = g_scan_active = false;
        unlock();
    }
    return got;
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
    lock();
    *out = g_test;
    unlock();
}
bool wifi_service_stack_up(void) { return g_stack_up; }

void wifi_get_info(wifi_info_t *out) {
    lock();
    *out = g_info;
    unlock();
}

/* ---- the task ---- */
static void on_time(uint32_t unix_s) {
    clock_set_utc(unix_s, true);
    printf("wifi: time set from NTP\n");
}

static void radio_on(void) {
    if (hal_wifi_start() == HAL_OK) {
        hal_wifi_sntp_stop();
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
    lock();
    g_test.bytes += n; /* the target is /dev/null: only count */
    unlock();
}
static void test_done(void *ctx, int err, uint32_t total) {
    (void)ctx;
    lock();
    g_test.ms = os_tick_ms() - g_test_t0;
    g_test.bytes = total;
    g_test.kbps = g_test.ms != 0 ? (uint32_t)((uint64_t)total * 1000u / 1024u / g_test.ms) : 0;
    g_test.err = err;
    g_test.state = err == 0 ? WT_DONE : WT_FAILED;
    unlock();
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
    lock();
    memset(&g_test, 0, sizeof g_test);
    g_test.state = WT_RUNNING;
    unlock();
    g_test_t0 = os_tick_ms();
    if (hal_wifi_http_get(host, port, path, test_data, test_done, NULL) != HAL_OK) {
        lock();
        g_test.state = WT_FAILED;
        g_test.err = -2;
        unlock();
    }
}

static void update_info(const wifi_policy_in_t *pin, wifi_power_t pw, hal_wifi_status st) {
    lock();
    g_info.state = st;
    g_info.radio_on = st != HAL_WIFI_DOWN;
    g_info.paused_for_playback = pin->playing && !pin->during_playback;
    g_info.rssi = st == HAL_WIFI_UP ? hal_wifi_rssi() : 0;
    g_info.ip[0] = '\0';
    g_info.ssid[0] = '\0';
    if (st == HAL_WIFI_UP) {
        (void)hal_wifi_ip(g_info.ip);
        (void)hal_wifi_ssid(g_info.ssid);
    }
    g_info.ntp_synced = clock_ntp_synced();
    g_info.reconnects = g_backoff.reconnects;
    g_info.failures = g_backoff.failures;
    g_info.saved = wifi_saved_count();
    unlock();
    (void)pw;
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

        /* requests */
        req_t r;
        while (os_queue_recv(g_q, &r, OS_NO_WAIT)) {
            g_online_until = now + ONLINE_GRACE_MS;
            g_idle_since = now;
            radio_on();
            switch (r.kind) {
            case REQ_SCAN:
                lock();
                g_scan_active = hal_wifi_scan_start() == HAL_OK;
                g_scan_wanted = g_scan_active;
                unlock();
                break;
            case REQ_JOIN: {
                bool needs;
                char psk[HAL_WIFI_PSK_MAX + 1];
                if (r.use_saved) {
                    saved_psk(r.ssid, psk, &needs);
                    snprintf(r.psk, sizeof r.psk, "%s", psk);
                }
                g_user_left = false;
                for (uint32_t i = 0; i < WIFI_MAX_SAVED; i++) { /* a fresh try clears the "wrong password" mark */
                    if (strcmp(g_bad_auth[i], r.ssid) == 0) {
                        g_bad_auth[i][0] = '\0';
                    }
                }
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

        /* power policy */
        player_info_t pi;
        player_get(&pi);
        bool playing = pi.state == PLAYER_PLAYING;
        wifi_settings_t s;
        wifi_get_settings(&s);
        hal_wifi_status st = hal_wifi_state();
        bool want = (int32_t)(g_online_until - now) > 0 || st == HAL_WIFI_JOINING || st == HAL_WIFI_NO_IP || g_test.state == WT_RUNNING;
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
                if (g_save_on_success && g_joining_user) {
                    save_network(g_cur_ssid, g_cur_psk);
                    g_save_on_success = false;
                }
                (void)hal_wifi_sntp_start(g_set.ntp_server, on_time);
                char ip[16] = "-";
                (void)hal_wifi_ip(ip);
                printf("wifi: joined '%s' ip=%s rssi=%d\n", g_cur_ssid, ip, hal_wifi_rssi());
            } else if (st == HAL_WIFI_BAD_AUTH || st == HAL_WIFI_FAILED) {
                printf("wifi: join '%s' %s\n", g_cur_ssid, st == HAL_WIFI_BAD_AUTH ? "failed: wrong password" : "failed");
                if (st == HAL_WIFI_BAD_AUTH) {
                    snprintf(g_bad_auth[g_next_auto++ % WIFI_MAX_SAVED], sizeof g_bad_auth[0], "%s", g_cur_ssid);
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
            wifi_backoff_due(&g_backoff, now) && !(playing && !s.during_playback)) {
            uint32_t n = wifi_saved_count();
            for (uint32_t k = 0; k < n; k++) {
                char ssid[HAL_WIFI_SSID_MAX + 1], psk[HAL_WIFI_PSK_MAX + 1];
                bool has, needs;
                uint32_t idx = (g_next_auto + k) % n;
                if (!wifi_saved_get(idx, ssid, &has) || wifi_saved_needs_password(ssid)) {
                    continue;
                }
                saved_psk(ssid, psk, &needs);
                g_next_auto = idx + 1u;
                printf("wifi: joining saved network '%s'\n", ssid);
                start_join(ssid, psk, false, false);
                break;
            }
            if (n == 0) {
                wifi_backoff_fail(&g_backoff, now); /* nothing saved: look again later */
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

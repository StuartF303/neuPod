// SPDX-License-Identifier: MPL-2.0
/*
 * Bluetooth classic through BTstack: the platform-neutral adapter (docs/bluetooth.md, ADR-0019). It implements the Bluetooth half
 * of hal/include/hal_radio.h and is built by every platform that uses BTstack; what differs per platform (the controller transport,
 * the run loop and the thread it runs on, the hand-over between tasks) is behind bt_port.h.
 *
 * Threading. BTstack is single-threaded: every BTstack call below runs on the port's BTstack thread, either inside a BTstack callback,
 * or because it was handed over with bt_port_run_sync() (blocking, used for start-up) or bt_port_post() (non-blocking, used for scan
 * start/stop and the link requests so the UI task never waits for the radio). The public hal_bt_* functions can be called from any
 * task. Events reach the listener through bt_port_deliver().
 *
 * Pairing and the connection (stage 2a). Pair: discoverable + connectable + bondable ON, gap_dedicated_bonding() (outgoing ACL,
 * SSP "just works", link key into the SD-card TLV store), the ACL is kept (ENABLE_EXPLICIT_DEDICATED_BONDING_DISCONNECT: BTstack neither disconnects after a good bonding nor waits for a disconnect to report it) and an SDP
 * query for an A2DP sink (0x110B) runs on it; then all three go OFF again. Connect (a device paired before): the SDP query first
 * (it creates the ACL), then gap_request_security_level(LEVEL_2) to authenticate with the stored key; never discoverable,
 * connectable or bondable, so a headset that lost its key cannot silently start a new pairing. Every step runs on the BTstack thread;
 * the public calls only hand a request over.
 *
 * An unanswered page is ended by our own timer (HAL_BT_PAIR_TIMEOUT_MS / HAL_BT_CONNECT_TIMEOUT_MS) with Create_Connection_Cancel, never by
 * the controller's page timeout: the MT6582 (Y2) does not report its page timeout until the connection is cancelled. A Connection_Complete
 * with an error that arrives after the attempt has ended is ignored (link_packet, HCI_EVENT_CONNECTION_COMPLETE: g_lphase is IDLE).
 */
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "btstack_version.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "hal_radio.h"
#include "bt_port.h"

#define LOCAL_NAME "neuPod"
/* Class of Device: service class Audio (bit 21), major Audio/Video, minor Portable audio. */
#define LOCAL_COD 0x20041Cu
#define MAX_FOUND 12u        /* devices kept per scan */
#define MAX_NAME_REQUESTS 6u /* remote name requests after a scan (each can take seconds) */

typedef struct {
    hal_bt_device_t dev;
    uint8_t psrm;           /* page scan repetition mode, for the name request */
    uint16_t clock_offset;
    bool name_known;
    bool name_tried;
} found_t;

typedef enum { SCAN_IDLE = 0, SCAN_INQUIRY, SCAN_NAMES } scan_phase_t;

static hal_bt_event_fn g_cb;
static void *g_cb_ctx;
static const hal_bt_storage_t *g_storage;
static volatile int g_state = HAL_BT_OFF; /* hal_bt_state_t; written with atomics, read anywhere */
static bd_addr_t g_local;
static volatile bool g_have_addr;
static bool g_registered;

/* Only touched on the BTstack thread. */
static found_t g_found[MAX_FOUND];
static uint32_t g_nfound;
static scan_phase_t g_phase;
static uint32_t g_name_requests;
static bd_addr_t g_name_pending;
static bool g_name_in_flight;
static bool g_skip_names; /* the scan was stopped by the user: no name requests */
static uint32_t g_raw_results; /* inquiry results BTstack delivered this scan (before any filtering): for the log */
static bool g_version_logged;  /* BTstack reads the controller version itself during start-up: print it once */

/* Non-blocking hand-over of scan start/stop from the caller's task to the worker. */
static btstack_context_callback_registration_t g_start_reg, g_stop_reg;
static volatile bool g_start_queued, g_stop_queued;
static volatile uint8_t g_start_units;

static btstack_packet_callback_registration_t g_hci_reg;

/* ---- the connection (stage 2a) ---- */

typedef enum { LP_IDLE = 0, LP_BONDING, LP_SDP, LP_AUTH, LP_UP } link_phase_t;

static volatile int g_link = HAL_BT_LINK_DISCONNECTED; /* hal_bt_link_state_t; written with atomics, read anywhere */
static volatile int g_reason = HAL_BT_FAIL_NONE;
static volatile bool g_accepting;
static hal_bt_device_t g_peer;       /* written by the caller that wins the idle -> PAIRING / CONNECTING transition, then read by the worker */
static link_phase_t g_lphase;        /* worker only (the scan has its own g_phase) */
static void link_packet(uint8_t *packet);
static void link_reset(void);
static bool g_pairing;               /* this attempt is a Pair (bondable, discoverable) */
static hci_con_handle_t g_acl = HCI_CON_HANDLE_INVALID;
static bool g_sink_seen;
static btstack_timer_source_t g_link_timer;
static btstack_context_callback_registration_t g_link_reg, g_disc_reg, g_forget_reg;
static volatile bool g_disc_queued, g_forget_queued;
static uint32_t g_attempt;            /* bumped by every Pair / Connect request (any task, atomically) */
static volatile uint32_t g_disc_tag;  /* the attempt a queued disconnect request was made for */
static uint32_t g_sdp_gen;            /* the attempt that started the SDP query now running */
static bool g_sdp_live, g_sdp_wait;   /* a query is running / the current attempt waits for the SDP client to be free */
static btstack_timer_source_t g_cancel_timer;
static uint8_t g_cancel_addr[6];
static uint8_t g_cancel_tries;
static uint32_t g_cancel_attempt;     /* the attempt a pending create-connection cancel belongs to */
#define FORGET_SLOTS 2u
static uint8_t g_forget_addr[FORGET_SLOTS][6];
static volatile int g_forget_state[FORGET_SLOTS]; /* 0 free, 1 being filled, 2 ready */

/* ---- events out ---- */

static void emit(const hal_bt_event_t *ev) {
    if (g_cb != NULL) {
        bt_port_deliver(g_cb, g_cb_ctx, ev);
    }
}

static void set_state(hal_bt_state_t s) {
    int prev = __atomic_exchange_n(&g_state, (int)s, __ATOMIC_SEQ_CST);
    if (prev != (int)s) {
        hal_bt_event_t ev = {.type = HAL_BT_EVT_STATE, .state = s};
        emit(&ev);
    }
}

static void log_device(const found_t *f, const char *what) {
    const uint8_t *a = f->dev.addr;
    char rssi[12];
    if (f->dev.rssi == HAL_BT_RSSI_UNKNOWN) {
        snprintf(rssi, sizeof rssi, "?");
    } else {
        snprintf(rssi, sizeof rssi, "%d", (int)f->dev.rssi);
    }
    char name[HAL_BT_NAME_MAX + 1];
    hal_log_text(name, sizeof name, f->dev.name); /* a remote device chooses its own name */
    printf("bt: %s %02X:%02X:%02X:%02X:%02X:%02X cod=0x%06lX rssi=%s name='%s' audio=%s\n", what, a[0], a[1], a[2], a[3], a[4], a[5],
           (unsigned long)f->dev.cod, rssi, name, f->dev.audio_sink ? "yes" : "no");
}

static void emit_device(const found_t *f) {
    hal_bt_event_t ev = {.type = HAL_BT_EVT_DEVICE, .state = (hal_bt_state_t)g_state, .dev = f->dev};
    emit(&ev);
}

static void emit_scan_done(void) {
    hal_bt_event_t ev = {.type = HAL_BT_EVT_SCAN_DONE, .state = (hal_bt_state_t)g_state};
    emit(&ev);
}

/* ---- BTstack TLV on top of hal_bt_storage_t ---- */

static int tlv_get(void *ctx, uint32_t tag, uint8_t *buf, uint32_t size) {
    (void)ctx;
    return g_storage != NULL ? g_storage->get(g_storage->ctx, tag, buf, size) : 0;
}
static int tlv_store(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size) {
    (void)ctx;
    return g_storage != NULL ? g_storage->put(g_storage->ctx, tag, data, size) : -1;
}
static void tlv_delete(void *ctx, uint32_t tag) {
    (void)ctx;
    if (g_storage != NULL) {
        g_storage->del(g_storage->ctx, tag);
    }
}
static const btstack_tlv_t k_tlv = {.get_tag = tlv_get, .store_tag = tlv_store, .delete_tag = tlv_delete};

/* ---- scanning ---- */

static bool is_audio_sink(uint32_t cod) { return ((cod >> 8) & 0x1Fu) == HAL_BT_COD_MAJOR_AUDIO_VIDEO; }

/* Copy a remote name: at most HAL_BT_NAME_MAX bytes, cut at a UTF-8 character boundary, control bytes replaced. */
static void copy_name(char *dst, const uint8_t *src, size_t len) {
    size_t n = 0;
    while (n < len && n < HAL_BT_NAME_MAX && src[n] != 0) {
        n++;
    }
    if (n == HAL_BT_NAME_MAX && n < len) {
        while (n > 0 && (src[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i] < 0x20u ? '?' : (char)src[i];
    }
    dst[n] = '\0';
}

static found_t *find(const bd_addr_t a) {
    for (uint32_t i = 0; i < g_nfound; i++) {
        if (memcmp(g_found[i].dev.addr, a, 6) == 0) {
            return &g_found[i];
        }
    }
    return NULL;
}

static void on_inquiry_result(const uint8_t *packet) {
    bd_addr_t a;
    gap_event_inquiry_result_get_bd_addr(packet, a);
    found_t *f = find(a);
    bool is_new = f == NULL;
    if (f == NULL) {
        if (g_nfound >= MAX_FOUND) {
            return;
        }
        f = &g_found[g_nfound++];
        memset(f, 0, sizeof *f);
        memcpy(f->dev.addr, a, 6);
        f->dev.rssi = HAL_BT_RSSI_UNKNOWN;
    }
    f->dev.cod = gap_event_inquiry_result_get_class_of_device(packet);
    f->dev.audio_sink = is_audio_sink(f->dev.cod);
    f->psrm = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
    f->clock_offset = gap_event_inquiry_result_get_clock_offset(packet);
    if (gap_event_inquiry_result_get_rssi_available(packet)) {
        f->dev.rssi = gap_event_inquiry_result_get_rssi(packet);
    }
    if (gap_event_inquiry_result_get_name_available(packet)) {
        copy_name(f->dev.name, gap_event_inquiry_result_get_name(packet), gap_event_inquiry_result_get_name_len(packet));
        f->name_known = f->dev.name[0] != '\0';
    }
    if (is_new) {
        log_device(f, "found");
    }
    emit_device(f);
}

static void finish_scan(void) {
    printf("bt: scan done, %u devices\n", (unsigned)g_nfound);
    g_phase = SCAN_IDLE;
    g_name_in_flight = false;
    /* SCAN_DONE first: a listener that sees the state turn to ON may start the next scan at once (and flush its queue), and
     * must not then be handed the end of the previous one. */
    emit_scan_done();
    set_state(HAL_BT_ON);
}

/* After the inquiry: ask audio devices without a name for it, one at a time (a name request cannot overlap an inquiry). */
static void next_name_request(void) {
    while (g_name_requests < MAX_NAME_REQUESTS) {
        found_t *f = NULL;
        for (uint32_t i = 0; i < g_nfound && f == NULL; i++) {
            if (g_found[i].dev.audio_sink && !g_found[i].name_known && !g_found[i].name_tried) {
                f = &g_found[i];
            }
        }
        if (f == NULL) {
            break;
        }
        f->name_tried = true;
        g_name_requests++;
        memcpy(g_name_pending, f->dev.addr, 6);
        if (gap_remote_name_request(f->dev.addr, f->psrm, (uint16_t)(f->clock_offset | 0x8000u)) == 0) {
            g_name_in_flight = true;
            return;
        }
        /* could not send it: try the next device */
    }
    finish_scan();
}

static void on_name_complete(const uint8_t *packet) {
    if (g_phase != SCAN_NAMES || !g_name_in_flight) {
        return; /* a scan that was stopped, or somebody else's request */
    }
    bd_addr_t a;
    hci_event_remote_name_request_complete_get_bd_addr(packet, a);
    if (memcmp(a, g_name_pending, 6) != 0) {
        return;
    }
    g_name_in_flight = false;
    found_t *f = find(a);
    if (f != NULL && hci_event_remote_name_request_complete_get_status(packet) == 0) {
        const char *n = hci_event_remote_name_request_complete_get_remote_name(packet);
        copy_name(f->dev.name, (const uint8_t *)n, strnlen(n, 248));
        f->name_known = f->dev.name[0] != '\0';
        log_device(f, "named");
        emit_device(f);
    }
    next_name_request();
}

static void on_inquiry_complete(void) {
    printf("bt: inquiry complete (%u results delivered by BTstack, %u kept)\n", (unsigned)g_raw_results, (unsigned)g_nfound);
    if (g_phase != SCAN_INQUIRY) {
        return;
    }
    g_phase = SCAN_NAMES;
    if (g_skip_names) {
        finish_scan();
    } else {
        next_name_request();
    }
}

static const char *hci_version_name(uint8_t v) {
    static const char *const k[] = {"1.0b", "1.1", "1.2", "2.0", "2.1", "3.0", "4.0", "4.1", "4.2", "5.0", "5.1", "5.2", "5.3", "5.4"};
    return v < sizeof k / sizeof k[0] ? k[v] : "?";
}

static void on_working(void) {
    gap_local_bd_addr(g_local);
    g_have_addr = true;
    printf("bt: HCI up, local BD_ADDR %s\n", bd_addr_to_str(g_local));
    set_state(HAL_BT_ON);
}

static void packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET) {
        return;
    }
    link_packet(packet);
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        switch (btstack_event_state_get_state(packet)) {
        case HCI_STATE_WORKING:
            on_working();
            break;
        case HCI_STATE_OFF:
            link_reset();
            if (g_state == HAL_BT_STARTING) {
                printf("bt: HCI did not come up\n");
                set_state(HAL_BT_FAILED);
            }
            break;
        default:
            break;
        }
        break;
    case GAP_EVENT_INQUIRY_RESULT:
        g_raw_results++;
        if (g_phase == SCAN_INQUIRY) {
            on_inquiry_result(packet);
        }
        break;
    case HCI_EVENT_COMMAND_STATUS:
        /* BTstack gives no event when the controller refuses the Inquiry command: without this the scan would sit in
         * "scanning" for ever. */
        if (HCI_EVENT_IS_COMMAND_STATUS(packet, hci_inquiry) && hci_event_command_status_get_status(packet) != 0 && g_phase == SCAN_INQUIRY) {
            printf("bt: inquiry refused by the controller (status 0x%02X)\n", (unsigned)hci_event_command_status_get_status(packet));
            finish_scan();
        }
        break;
    case GAP_EVENT_INQUIRY_COMPLETE:
        on_inquiry_complete();
        break;
    case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
        on_name_complete(packet);
        break;
    case HCI_EVENT_COMMAND_COMPLETE:
        if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_version_information)) {
            const uint8_t *r = hci_event_command_complete_get_return_parameters(packet);
            if (r[0] == 0 && !g_version_logged) { /* status, hci_version, hci_revision(2), lmp_version, manufacturer(2), lmp_subversion(2) */
                printf("bt: BTstack %s; controller HCI version %u (Bluetooth %s) revision 0x%04X, LMP %u, "
                       "manufacturer 0x%04X, LMP subversion 0x%04X\n",
                       BTSTACK_VERSION_STRING, (unsigned)r[1], hci_version_name(r[1]), (unsigned)little_endian_read_16(r, 2),
                       (unsigned)r[4], (unsigned)little_endian_read_16(r, 5), (unsigned)little_endian_read_16(r, 7));
                g_version_logged = true;
            }
        }
        break;
    default:
        break;
    }
}

static void worker_scan_start(void *arg) {
    (void)arg;
    g_start_queued = false;
    if (g_phase != SCAN_IDLE) {
        return;
    }
    g_nfound = 0;
    g_name_requests = 0;
    g_name_in_flight = false;
    g_skip_names = false;
    g_raw_results = 0;
    g_phase = SCAN_INQUIRY;
    printf("bt: scan start (%u s)\n", (unsigned)(((uint32_t)g_start_units * 1280u + 500u) / 1000u));
    /* hal_bt_inquiry_start() already moved the state to SCANNING (so a second start is refused at once): announce it. */
    hal_bt_event_t ev = {.type = HAL_BT_EVT_STATE, .state = HAL_BT_SCANNING};
    emit(&ev);
    if (gap_inquiry_start(g_start_units) != 0) {
        printf("bt: inquiry could not start\n");
        finish_scan();
    }
}

static void worker_scan_stop(void *arg) {
    (void)arg;
    g_stop_queued = false;
    if (g_phase == SCAN_INQUIRY) {
        g_skip_names = true;
        (void)gap_inquiry_stop(); /* GAP_EVENT_INQUIRY_COMPLETE follows and finishes the scan */
    } else if (g_phase == SCAN_NAMES) {
        finish_scan();
    }
}

/* ---- pairing and the connection ---- */

/* The key record layout hal_bt_storage_delete_key() relies on (btstack_link_key_db_tlv.c, link_key_nvm_t: uint32 seq, bd_addr_t, link_key_t,
 * link_key_type_t): size = round_up(4 + 6 + 16 + sizeof(link_key_type_t), 4). The struct is private to BTstack, so the assert is on its parts. */
_Static_assert(sizeof(bd_addr_t) == 6 && sizeof(link_key_t) == 16, "link key record layout");
_Static_assert(HAL_BT_KEY_RECORD_LEN == ((4u + 6u + 16u + sizeof(link_key_type_t) + 3u) & ~3u), "HAL_BT_KEY_RECORD_LEN must match BTstack's link_key_nvm_t");
_Static_assert(HAL_BT_KEY_SLOTS == NVM_NUM_LINK_KEYS, "HAL_BT_KEY_SLOTS must match NVM_NUM_LINK_KEYS");

/* HCI / SDP / L2CAP status to our reason. During a Pair, "the remote ended it" and "no resources" mean a headset that is busy or not in
 * pairing mode (not "out of range"). */
static hal_bt_fail_t fail_from_status(uint8_t st) {
    switch (st) {
    case ERROR_CODE_AUTHENTICATION_FAILURE:
    case ERROR_CODE_PIN_OR_KEY_MISSING:
    case ERROR_CODE_PAIRING_NOT_ALLOWED:
    case ERROR_CODE_INSUFFICIENT_SECURITY:
    case ERROR_CODE_CONNECTION_REJECTED_DUE_TO_SECURITY_REASONS:
    case ERROR_CODE_PAIRING_WITH_UNIT_KEY_NOT_SUPPORTED:
        return HAL_BT_FAIL_AUTH;
    case ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION:
    case ERROR_CODE_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES:
        return g_pairing ? HAL_BT_FAIL_BUSY : HAL_BT_FAIL_NOT_FOUND;
    case ERROR_CODE_PAGE_TIMEOUT:
    case ERROR_CODE_CONNECTION_TIMEOUT:
    case ERROR_CODE_CONNECTION_ACCEPT_TIMEOUT_EXCEEDED:
    case ERROR_CODE_CONNECTION_REJECTED_DUE_TO_UNACCEPTABLE_BD_ADDR:
        return HAL_BT_FAIL_NOT_FOUND;
    default:
        return HAL_BT_FAIL_INTERNAL;
    }
}

/* Never discoverable. During a Pair: connectable (for the chosen headset only, see accept_filter), bondable, and "just works" with
 * auto-accept. At rest and in Connect: none of them, and the IO-capability request is refused (IO capability UNKNOWN), so no transient
 * pairing can authenticate a link. */
static void set_accepting(bool on) {
    gap_discoverable_control(0);
    gap_connectable_control(on ? 1 : 0);
    gap_set_bondable_mode(on ? 1 : 0);
    gap_ssp_set_auto_accept(on ? 1 : 0);
    gap_ssp_set_io_capability(on ? SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT : SSP_IO_CAPABILITY_UNKNOWN);
    g_accepting = on;
}

/* BTstack asks this for every incoming connection request: only the headset being paired. */
static int accept_filter(bd_addr_t addr, hci_link_type_t type) {
    return g_accepting && type == HCI_LINK_TYPE_ACL && memcmp(addr, g_peer.addr, 6) == 0;
}

static void emit_link(hal_bt_link_state_t s, hal_bt_fail_t reason) {
    hal_bt_event_t ev = {.type = HAL_BT_EVT_LINK, .state = (hal_bt_state_t)g_state, .dev = g_peer, .link = s, .reason = reason, .attempt = g_attempt};
    emit(&ev);
}

/* Publish the new link state, then tell the listener. (The event is built first: once an idle state is visible a caller may start
 * the next attempt and overwrite g_peer.) */
static void set_link(hal_bt_link_state_t s, hal_bt_fail_t reason) {
    hal_bt_event_t ev = {.type = HAL_BT_EVT_LINK, .state = (hal_bt_state_t)g_state, .dev = g_peer, .link = s, .reason = reason, .attempt = g_attempt};
    g_reason = reason;
    int prev = __atomic_exchange_n(&g_link, (int)s, __ATOMIC_SEQ_CST);
    if (prev != (int)s) {
        emit(&ev);
    }
}

/* Cancel a connection that is still being created. The controller may not take a command yet: retry every 200 ms (at most 10 times). */
static void cancel_create_try(btstack_timer_source_t *ts) {
    (void)ts;
    if (g_cancel_attempt != g_attempt) {
        return; /* a newer attempt has started: do not cancel its page to the same headset */
    }
    if (hci_can_send_command_packet_now()) {
        hci_send_cmd(&hci_create_connection_cancel, g_cancel_addr);
    } else if (++g_cancel_tries < 10u) {
        btstack_run_loop_set_timer(&g_cancel_timer, 200);
        btstack_run_loop_add_timer(&g_cancel_timer);
    }
}

static void cancel_pending_acl(void) {
    if (g_acl != HCI_CON_HANDLE_INVALID) {
        (void)gap_disconnect(g_acl);
    } else if (g_lphase != LP_IDLE) {
        memcpy(g_cancel_addr, g_peer.addr, 6); /* g_peer may belong to the next attempt by the time the retry runs */
        g_cancel_tries = 0;
        g_cancel_attempt = g_attempt;
        btstack_run_loop_remove_timer(&g_cancel_timer);
        btstack_run_loop_set_timer_handler(&g_cancel_timer, cancel_create_try);
        cancel_create_try(&g_cancel_timer);
    }
}

/* The end of an attempt (or of the link): everything off, hang up unless it is CONNECTED, publish the state. */
static void link_end(hal_bt_link_state_t s, hal_bt_fail_t reason) {
    btstack_run_loop_remove_timer(&g_link_timer);
    if (g_accepting || g_pairing) {
        set_accepting(false);
    }
    g_pairing = false;
    if (s != HAL_BT_LINK_CONNECTED) {
        cancel_pending_acl();
        g_lphase = LP_IDLE;
    }
    if (s == HAL_BT_LINK_FAILED) {
        printf("bt: %s failed: %s\n", bd_addr_to_str(g_peer.addr), hal_bt_fail_text(reason));
    }
    set_link(s, reason);
}

static void link_timeout(btstack_timer_source_t *ts) {
    (void)ts;
    if (g_lphase != LP_IDLE && g_lphase != LP_UP) {
        link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_TIMEOUT);
    }
}

static void link_up(void) {
    g_lphase = LP_UP;
    printf("bt: connected %s\n", bd_addr_to_str(g_peer.addr));
    link_end(HAL_BT_LINK_CONNECTED, HAL_BT_FAIL_NONE);
}

static bool have_link_key(void) {
    link_key_t k;
    link_key_type_t t;
    return gap_get_link_key_for_bd_addr(g_peer.addr, k, &t);
}

static void start_sdp(void);

static void sdp_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET) {
        return;
    }
    switch (hci_event_packet_get_type(packet)) {
    case SDP_EVENT_QUERY_ATTRIBUTE_VALUE:
        g_sink_seen = true; /* the query asked for the Audio Sink service class: any record that came back is one */
        break;
    case SDP_EVENT_QUERY_COMPLETE: {
        uint32_t gen = g_sdp_gen;
        g_sdp_live = false; /* the SDP client is free again, whichever attempt started this query */
        if (gen != g_attempt) {
            /* A query of an attempt that was cancelled. If the current attempt was waiting for the client, start its query now. */
            if (g_sdp_wait && g_lphase == LP_SDP) {
                g_sdp_wait = false;
                start_sdp();
            }
            return;
        }
        if (g_lphase != LP_SDP) {
            return; /* the attempt ended while the query ran */
        }
        uint8_t st = sdp_event_query_complete_get_status(packet);
        if (st != 0) {
            printf("bt: SDP 0x%02X\n", (unsigned)st);
            link_end(HAL_BT_LINK_FAILED, fail_from_status(st));
        } else if (!g_sink_seen) {
            link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_NO_SINK);
        } else if (g_pairing) {
            link_up(); /* authenticated by the bonding that came first */
        } else if (g_acl == HCI_CON_HANDLE_INVALID) {
            link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_LINK_LOST);
        } else if (!have_link_key()) {
            link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_AUTH); /* never authenticate a Connect without a stored key */
        } else {
            g_lphase = LP_AUTH; /* Connect: now authenticate with the stored key */
            gap_request_security_level(g_acl, LEVEL_2);
        }
        break;
    }
    default:
        break;
    }
}

static void start_sdp(void) {
    g_lphase = LP_SDP;
    g_sink_seen = false;
    if (g_sdp_live) {
        g_sdp_wait = true; /* the query of a cancelled attempt is still ending: its completion starts ours (the attempt timer is the backstop) */
        return;
    }
    g_sdp_gen = g_attempt;
    g_sdp_live = true;
    uint8_t rc = sdp_client_query_uuid16(sdp_handler, g_peer.addr, BLUETOOTH_SERVICE_CLASS_AUDIO_SINK);
    if (rc != 0) {
        g_sdp_live = false;
        printf("bt: SDP start 0x%02X\n", (unsigned)rc);
        link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_INTERNAL);
    }
}

/* Worker: begin the attempt that hal_bt_pair() / hal_bt_connect() reserved (g_link is already PAIRING / CONNECTING). */
static void worker_link_start(void *arg) {
    (void)arg;
    g_acl = HCI_CON_HANDLE_INVALID;
    g_sink_seen = false;
    g_sdp_wait = false;
    g_pairing = g_link == HAL_BT_LINK_PAIRING;
    printf("bt: %s %s\n", g_pairing ? "pair" : "connect", bd_addr_to_str(g_peer.addr));
    emit_link((hal_bt_link_state_t)g_link, HAL_BT_FAIL_NONE);
    btstack_run_loop_set_timer_handler(&g_link_timer, link_timeout);
    btstack_run_loop_set_timer(&g_link_timer, g_pairing ? HAL_BT_PAIR_TIMEOUT_MS : HAL_BT_CONNECT_TIMEOUT_MS);
    btstack_run_loop_add_timer(&g_link_timer);
    if (g_pairing) {
        set_accepting(true);
        g_lphase = LP_BONDING;
        int rc = gap_dedicated_bonding(g_peer.addr, 0);
        if (rc != 0) {
            printf("bt: bonding start %d\n", rc);
            link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_INTERNAL);
        }
    } else if (!have_link_key()) {
        link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_AUTH); /* no stored key: Connect never pairs */
    } else {
        start_sdp();
    }
}

/* Worker: a user cancel / disconnect, or Forget of the current peer. */
static void drop_link(void) {
    if (g_link == HAL_BT_LINK_DISCONNECTED || g_link == HAL_BT_LINK_FAILED) {
        return;
    }
    if (g_lphase == LP_UP) {
        btstack_run_loop_remove_timer(&g_link_timer);
        if (g_acl != HCI_CON_HANDLE_INVALID) {
            (void)gap_disconnect(g_acl);
        }
        g_lphase = LP_IDLE;
        set_link(HAL_BT_LINK_DISCONNECTED, HAL_BT_FAIL_NONE);
        return;
    }
    link_end(HAL_BT_LINK_DISCONNECTED, HAL_BT_FAIL_NONE);
}

static void worker_disconnect(void *arg) {
    (void)arg;
    g_disc_queued = false;
    if (g_disc_tag == g_attempt) { /* a request made for an attempt that has since ended must not kill the next one */
        drop_link();
    }
}

static void worker_forget(void *arg) {
    (void)arg;
    g_forget_queued = false;
    for (uint32_t i = 0; i < FORGET_SLOTS; i++) {
        if (g_forget_state[i] != 2) {
            continue;
        }
        bd_addr_t a;
        memcpy(a, g_forget_addr[i], 6);
        __atomic_store_n(&g_forget_state[i], 0, __ATOMIC_SEQ_CST);
        if (memcmp(a, g_peer.addr, 6) == 0) {
            drop_link();
        }
        gap_drop_link_key_for_bd_addr(a);
        printf("bt: forgot %s\n", bd_addr_to_str(a));
    }
}

/* The stack went down (HCI_STATE_OFF) under a link or an attempt: nothing is left of it. */
static void link_reset(void) {
    g_acl = HCI_CON_HANDLE_INVALID;
    g_sdp_live = g_sdp_wait = false;
    btstack_run_loop_remove_timer(&g_cancel_timer);
    if (g_link == HAL_BT_LINK_DISCONNECTED || g_link == HAL_BT_LINK_FAILED) {
        g_lphase = LP_IDLE;
        return;
    }
    g_lphase = LP_IDLE; /* no connection to cancel, no command to send */
    link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_INTERNAL);
}

static void link_packet(uint8_t *packet) {
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_LINK_KEY_NOTIFICATION: {
        /* A key may only come from the headset being paired, during its Pair. Any other key is deleted at once. */
        bd_addr_t a;
        hci_event_link_key_request_get_bd_addr(packet, a);
        if (!(g_lphase != LP_IDLE && memcmp(a, g_peer.addr, 6) == 0)) {
            gap_drop_link_key_for_bd_addr(a);
            printf("bt: dropped stray key %s\n", bd_addr_to_str(a));
        }
        break;
    }
    case HCI_EVENT_PIN_CODE_REQUEST: {
        /* A pre-2.1 headset (no Secure Simple Pairing). In bondable mode BTstack forwards the request and nothing else answers it, so the
         * headset would wait for the timer. There is no PIN entry: refuse now and end the attempt. */
        bd_addr_t a;
        hci_event_pin_code_request_get_bd_addr(packet, a);
        (void)gap_pin_code_negative(a);
        if (g_lphase != LP_IDLE && memcmp(a, g_peer.addr, 6) == 0) {
            g_lphase = LP_IDLE; /* link_end() drops the connection; the bonding failure that follows is then ignored */
            link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_PIN);
        }
        break;
    }
    case HCI_EVENT_CONNECTION_COMPLETE: {
        bd_addr_t a;
        hci_event_connection_complete_get_bd_addr(packet, a);
        if (g_lphase == LP_IDLE || memcmp(a, g_peer.addr, 6) != 0 || hci_event_connection_complete_get_link_type(packet) != 1) {
            break;
        }
        if (hci_event_connection_complete_get_status(packet) == 0) {
            g_acl = hci_event_connection_complete_get_connection_handle(packet);
        }
        break;
    }
    case GAP_EVENT_DEDICATED_BONDING_COMPLETED: {
        if (g_lphase != LP_BONDING) {
            break;
        }
        uint8_t st = gap_event_dedicated_bonding_completed_get_status(packet);
        if (st == 0) {
            start_sdp();
        } else {
            printf("bt: bonding 0x%02X\n", (unsigned)st);
            link_end(HAL_BT_LINK_FAILED, fail_from_status(st));
        }
        break;
    }
    case GAP_EVENT_SECURITY_LEVEL: {
        if (g_lphase != LP_AUTH || gap_event_security_level_get_handle(packet) != g_acl) {
            break;
        }
        if (gap_event_security_level_get_status(packet) == 0 && gap_event_security_level_get_security_level(packet) >= LEVEL_2 && have_link_key()) {
            link_up();
        } else {
            link_end(HAL_BT_LINK_FAILED, HAL_BT_FAIL_AUTH);
        }
        break;
    }
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        if (g_acl == HCI_CON_HANDLE_INVALID || hci_event_disconnection_complete_get_connection_handle(packet) != g_acl) {
            break;
        }
        uint8_t why = hci_event_disconnection_complete_get_reason(packet);
        g_acl = HCI_CON_HANDLE_INVALID;
        if (g_lphase == LP_UP) {
            /* Stage 2a has no profile open, so an idle link is closed: by us (BTstack's idle timeout, local host) or by the headset (0x13,
             * remote user terminated: a headset with nothing to do hangs up after a second or two). Neither is a failure. Anything else
             * (supervision timeout 0x08, ...) is a lost link. */
            g_lphase = LP_IDLE;
            if (why == ERROR_CODE_CONNECTION_TERMINATED_BY_LOCAL_HOST || why == ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION) {
                printf("bt: link closed %s reason 0x%02X (idle, no profile yet)\n", bd_addr_to_str(g_peer.addr), (unsigned)why);
                set_link(HAL_BT_LINK_DISCONNECTED, HAL_BT_FAIL_NONE);
            } else {
                printf("bt: link lost 0x%02X\n", (unsigned)why);
                set_link(HAL_BT_LINK_FAILED, HAL_BT_FAIL_LINK_LOST);
            }
        } else if (g_lphase != LP_IDLE && !(why == ERROR_CODE_CONNECTION_TERMINATED_BY_LOCAL_HOST && g_lphase == LP_BONDING)) {
            /* The link dropped during set-up (bonding, SDP, authentication) from the other side or by timeout: end the attempt now, so the
             * Pair window does not stay open until the timer. Only while bonding is a local disconnect ignored: BTstack sends one itself after
             * a failed bonding, and the bonding result, with the real reason, follows it. In AUTH / SDP nothing follows, so end here. The phase
             * is cleared first: there is no connection left to cancel. */
            bool authenticating = g_lphase == LP_AUTH;
            g_lphase = LP_IDLE;
            printf("bt: link dropped in set-up 0x%02X\n", (unsigned)why);
            link_end(HAL_BT_LINK_FAILED, authenticating ? HAL_BT_FAIL_AUTH : fail_from_status(why));
        }
        break;
    }
    default:
        break;
    }
}

/* Reserve the link for a new attempt: idle (DISCONNECTED or FAILED) -> @p to, atomically. */
static hal_status_t link_reserve(hal_bt_link_state_t to) {
    if (g_state == HAL_BT_SCANNING) {
        return HAL_EBUSY;
    }
    if (g_state != HAL_BT_ON) {
        return HAL_ENODEV;
    }
    int cur = g_link;
    if (cur != HAL_BT_LINK_DISCONNECTED && cur != HAL_BT_LINK_FAILED) {
        return HAL_EBUSY;
    }
    if (!__atomic_compare_exchange_n(&g_link, &cur, (int)to, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        return HAL_EBUSY;
    }
    (void)__atomic_add_fetch(&g_attempt, 1u, __ATOMIC_SEQ_CST); /* the attempt number that disconnect requests and SDP callbacks are checked against */
    return HAL_OK;
}

static bool addr_is_zero(const uint8_t a[6]) {
    static const uint8_t zero[6];
    return memcmp(a, zero, 6) == 0;
}

hal_status_t hal_bt_pair(const hal_bt_device_t *dev) {
    if (dev == NULL || addr_is_zero(dev->addr)) {
        return HAL_EINVAL;
    }
    hal_status_t st = link_reserve(HAL_BT_LINK_PAIRING);
    if (st != HAL_OK) {
        return st;
    }
    g_peer = *dev;
    g_reason = HAL_BT_FAIL_NONE;
    bt_port_post(&g_link_reg);
    return HAL_OK;
}

hal_status_t hal_bt_connect(const uint8_t addr[6]) {
    if (addr == NULL || addr_is_zero(addr)) {
        return HAL_EINVAL;
    }
    hal_status_t st = link_reserve(HAL_BT_LINK_CONNECTING);
    if (st != HAL_OK) {
        return st;
    }
    memset(&g_peer, 0, sizeof g_peer);
    memcpy(g_peer.addr, addr, 6);
    g_peer.rssi = HAL_BT_RSSI_UNKNOWN;
    g_reason = HAL_BT_FAIL_NONE;
    bt_port_post(&g_link_reg);
    return HAL_OK;
}

void hal_bt_disconnect(void) {
    if (g_link == HAL_BT_LINK_DISCONNECTED || g_link == HAL_BT_LINK_FAILED) {
        return;
    }
    g_disc_tag = g_attempt; /* the attempt this request is about */
    if (!__atomic_exchange_n(&g_disc_queued, true, __ATOMIC_SEQ_CST)) {
        bt_port_post(&g_disc_reg);
    }
}

hal_status_t hal_bt_forget(const uint8_t addr[6]) {
    if (addr == NULL) {
        return HAL_EINVAL;
    }
    if (g_state == HAL_BT_OFF || g_state == HAL_BT_STARTING || g_state == HAL_BT_FAILED) {
        return HAL_ENODEV; /* no stack to ask: the caller deletes the key from the storage (hal_bt_storage_delete_key) */
    }
    for (uint32_t i = 0; i < FORGET_SLOTS; i++) {
        int free_state = 0;
        if (__atomic_compare_exchange_n(&g_forget_state[i], &free_state, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            memcpy(g_forget_addr[i], addr, 6);
            __atomic_store_n(&g_forget_state[i], 2, __ATOMIC_SEQ_CST);
            if (!__atomic_exchange_n(&g_forget_queued, true, __ATOMIC_SEQ_CST)) {
                bt_port_post(&g_forget_reg);
            }
            return HAL_OK;
        }
    }
    return HAL_EBUSY; /* both slots busy: the caller retries */
}

hal_bt_link_state_t hal_bt_link_state(void) { return (hal_bt_link_state_t)g_link; }
uint32_t hal_bt_link_attempt(void) { return __atomic_load_n(&g_attempt, __ATOMIC_SEQ_CST); }
hal_bt_fail_t hal_bt_link_reason(void) { return g_link == HAL_BT_LINK_FAILED ? (hal_bt_fail_t)g_reason : HAL_BT_FAIL_NONE; }

bool hal_bt_link_device(hal_bt_device_t *out) {
    if (g_link == HAL_BT_LINK_DISCONNECTED) {
        return false;
    }
    *out = g_peer;
    return true;
}

bool hal_bt_accepting(void) { return g_accepting; }

/* ---- start-up ---- */

typedef struct {
    hal_bt_event_fn cb;
    void *ctx;
    const hal_bt_storage_t *storage;
    int rc;
} init_args_t;
static init_args_t g_init; /* static: a port that gives up waiting must not leave the BTstack thread with a dangling pointer */

static uint32_t worker_init(void *param) {
    init_args_t *a = param;
    g_cb = a->cb;
    g_cb_ctx = a->ctx;
    g_storage = a->storage;
    if (!g_registered) {
        g_registered = true;
        g_hci_reg.callback = &packet_handler;
        hci_add_event_handler(&g_hci_reg);
        g_start_reg.callback = &worker_scan_start;
        g_stop_reg.callback = &worker_scan_stop;
        g_link_reg.callback = &worker_link_start;
        g_disc_reg.callback = &worker_disconnect;
        g_forget_reg.callback = &worker_forget;
        l2cap_init(); /* the SDP client runs on it; no service is registered, so nothing can connect to us */
        sdp_client_init();
        if (g_storage != NULL) {
            btstack_tlv_set_instance(&k_tlv, NULL);
            hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(&k_tlv, NULL));
        }
        /* Results with the signal strength and the name from the extended inquiry response. The default is the plain mode,
         * which carries neither (so the list never had a dBm value). Sent during start-up, before the first inquiry. */
        hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
        gap_set_local_name(LOCAL_NAME);
        gap_set_class_of_device(LOCAL_COD);
        /* Not discoverable, not connectable, not bondable until a Pair action says otherwise (set_accepting). SSP "just works":
         * we have no display and no keys. */
        gap_register_classic_connection_filter(accept_filter);
        set_accepting(false);
    }
    set_state(HAL_BT_STARTING);
    a->rc = bt_port_power(true);
    if (a->rc != 0) {
        printf("bt: hci_power_control(ON) failed (%d)\n", a->rc);
        set_state(HAL_BT_FAILED);
    }
    return 0;
}

hal_status_t hal_bt_init(hal_bt_event_fn cb, void *ctx, const hal_bt_storage_t *storage) {
    if (!bt_port_present()) {
        return HAL_ENODEV; /* the radio driver / the controller device is not there */
    }
    if (g_state != HAL_BT_OFF && g_state != HAL_BT_FAILED) {
        return HAL_EBUSY;
    }
    if (!bt_port_start()) {
        return HAL_ERR;
    }
    g_init = (init_args_t){.cb = cb, .ctx = ctx, .storage = storage, .rc = 0};
    if (!bt_port_run_sync(worker_init, &g_init)) {
        return HAL_ERR;
    }
    return g_init.rc == 0 ? HAL_OK : HAL_ERR;
}

hal_bt_state_t hal_bt_state(void) { return (hal_bt_state_t)g_state; }

bool hal_bt_local_address(uint8_t addr[6]) {
    if (!g_have_addr) {
        return false;
    }
    memcpy(addr, g_local, 6);
    return true;
}

void hal_bt_poll(void) { bt_port_poll(); }

hal_status_t hal_bt_inquiry_start(uint8_t duration_s) {
    int expect = HAL_BT_ON;
    if (g_state == HAL_BT_SCANNING) {
        return HAL_EBUSY;
    }
    if (g_state != HAL_BT_ON) {
        return HAL_ENODEV;
    }
    if (g_link == HAL_BT_LINK_PAIRING || g_link == HAL_BT_LINK_CONNECTING) {
        return HAL_EBUSY; /* an inquiry would starve the link set-up */
    }
    if (!__atomic_compare_exchange_n(&g_state, &expect, (int)HAL_BT_SCANNING, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        return HAL_EBUSY;
    }
    uint32_t units = ((uint32_t)duration_s * 1000u + 1279u) / 1280u; /* 1.28 s units */
    g_start_units = (uint8_t)(units < 1u ? 1u : units > 48u ? 48u : units);
    g_start_queued = true;
    bt_port_post(&g_start_reg);
    return HAL_OK;
}

void hal_bt_inquiry_stop(void) {
    if (g_state != HAL_BT_SCANNING || g_stop_queued) {
        return;
    }
    g_stop_queued = true;
    bt_port_post(&g_stop_reg);
}

hal_status_t hal_radio_a2dp_connect(const uint8_t addr[6]) {
    (void)addr;
    return HAL_ENOTSUP; /* stage 2 (docs/bluetooth.md) */
}

void hal_radio_a2dp_disconnect(void) {}

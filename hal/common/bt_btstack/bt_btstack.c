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
 * The audio stream (stage 2b). Once the link is CONNECTED the adapter opens an A2DP source stream by itself (a2dp_source_establish_stream) with one
 * SBC endpoint: 44.1 kHz stereo, joint stereo, 16 blocks, 8 subbands, bitpool up to the headset's maximum but at most NEUPOD_BT_SBC_BITPOOL
 * (53; with NEUPOD_BT_SBC_XQ 76, the "SBC-XQ" setting). The audio comes from the PCM source (hal_bt_set_pcm_source: the consumer side of the
 * core 1 ring), encoded on BTstack's thread by a run-loop timer that works like a source clock steered by the ring's fill level (see media_tick).
 * No RTOS call is made on this path: on the Y2 BTstack's thread is not an RTOS task.
 *
 * AVRCP (stage 2c). We are the AVRCP target of the headset's buttons and metadata (play / pause / next ... become HAL_BT_EVT_KEY; hal_bt_track_info() feeds
 * the track-changed / play-status notifications and the metadata the headset asks for) and its AVRCP controller for the volume: the headset is the
 * target of absolute volume (category 2), so its volume reaches us as a notification (HAL_BT_EVT_VOLUME) and ours goes out with
 * avrcp_controller_set_absolute_volume(). The headset usually opens the AVRCP channel itself; if it has not within AVRCP_CONNECT_DELAY_MS of the A2DP
 * signalling channel we open it. "Absolute volume supported" means the headset has reported a volume on this channel (hal_bt_volume_supported()).
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
#include "classic/btstack_sbc.h"
#include "classic/btstack_sbc_bluedroid.h"
#include "hal_radio.h"
#include "bt_port.h"

#define LOCAL_NAME "neuPod"
/* Class of Device: service class Audio (bit 21), major Audio/Video, minor Portable audio. */
#define LOCAL_COD 0x20041Cu
#define MAX_FOUND 12u        /* devices kept per scan */
#define MAX_NAME_REQUESTS 6u /* remote name requests after a scan (each can take seconds) */

/* ---- A2DP configuration: the constants to flip ---- */
#ifndef NEUPOD_BT_SBC_XQ
#define NEUPOD_BT_SBC_XQ 0 /* 1: accept the headset's high bitpools (up to 76, "SBC-XQ", about 455 kbit/s). 0: at most the standard 53 (328 kbit/s). */
#endif
#define SBC_BITPOOL_STD 53u
#define SBC_BITPOOL_XQ 76u
#define NEUPOD_BT_SBC_BITPOOL (NEUPOD_BT_SBC_XQ ? SBC_BITPOOL_XQ : SBC_BITPOOL_STD)
#define A2DP_RATE 44100u   /* the one sample rate offered: every sink supports it; 48 kHz material is not played on Bluetooth yet (docs/bluetooth.md) */
#define SBC_SAMPLES_PER_FRAME 128u /* 16 blocks x 8 subbands */
#ifdef NEUPOD_RP2350
#define SBC_STORAGE_BYTES 680u  /* header byte + SBC frames of one media packet; a typical headset's L2CAP MTU is 672 */
#else
#define SBC_STORAGE_BYTES 1030u
#endif
#define PACE_TICK_MS 8u         /* the encoder timer */
#define PACE_BACKLOG_MS 40u     /* the most audio the encoder will owe after a stall: no unbounded backlog */
#define PACE_PACKET_MAX_AGE_MS 20u /* a partly filled packet is sent after this long */
#define PACE_TRIM_UNIT_PPM 500  /* one step of the fill-level trim */
#define PACE_TRIM_MAX 6         /* +-3000 ppm */
#define RSSI_PERIOD_MS 10000u

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

static volatile int g_st_rssi = HAL_BT_RSSI_UNKNOWN; /* link quality (stream statistics) */

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
    case GAP_EVENT_RSSI_MEASUREMENT:
        g_st_rssi = (int8_t)gap_event_rssi_measurement_get_rssi(packet);
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
/* The RP2350 toolchain uses short enums (1-byte key type: the record is exactly HAL_BT_KEY_RECORD_LEN = 28 bytes). The Y2's has 4-byte enums
 * (a 32-byte record): HAL_BT_KEY_RECORD_LEN is then only the read buffer of hal_bt_storage_delete_key(), which needs the address at offset 4. */
#define LINK_KEY_RECORD_LEN ((4u + 6u + 16u + sizeof(link_key_type_t) + 3u) & ~3u)
_Static_assert(sizeof(link_key_type_t) != 1 || HAL_BT_KEY_RECORD_LEN == LINK_KEY_RECORD_LEN, "HAL_BT_KEY_RECORD_LEN must match BTstack's link_key_nvm_t");
_Static_assert(LINK_KEY_RECORD_LEN >= HAL_BT_KEY_ADDR_OFFSET + 6u && LINK_KEY_RECORD_LEN <= 48u, "link key record size (y2_bt_tlv.c caches up to 48 bytes)");
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

static void a2dp_open(void);
static void emit_key(hal_bt_key_t k);
static void a2dp_gone(hal_bt_fail_t why_failed);
static void avrcp_gone(void);
static void avrcp_arm_connect(void);

static void link_up(void) {
    g_lphase = LP_UP;
    printf("bt: connected %s\n", bd_addr_to_str(g_peer.addr));
    link_end(HAL_BT_LINK_CONNECTED, HAL_BT_FAIL_NONE);
    a2dp_open(); /* the audio stream opens by itself once the link is up (stage 2b) */
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
        a2dp_gone(HAL_BT_FAIL_NONE); /* the audio connection goes first, then the link */
        avrcp_gone();
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
    a2dp_gone(HAL_BT_FAIL_NONE);
    avrcp_gone();
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
        a2dp_gone(HAL_BT_FAIL_NONE); /* a stream does not outlive its link; its state is announced before the link's */
        avrcp_gone();
        if (g_lphase == LP_UP) {
            /* An idle link is closed: by us (BTstack's idle timeout, local host) or by the headset (0x13,
             * remote user terminated: a headset with nothing to do hangs up after a second or two). Neither is a failure. Anything else
             * (supervision timeout 0x08, ...) is a lost link. */
            g_lphase = LP_IDLE;
            if (why == ERROR_CODE_CONNECTION_TERMINATED_BY_LOCAL_HOST || why == ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION) {
                printf("bt: link closed %s reason 0x%02X (idle)\n", bd_addr_to_str(g_peer.addr), (unsigned)why);
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


/* ---- the audio stream (stage 2b) ---- */

static volatile int g_stream = HAL_BT_STREAM_CLOSED; /* hal_bt_stream_state_t */
static volatile int g_stream_reason = HAL_BT_FAIL_NONE;
static hal_bt_codec_t g_codec;              /* written by BTstack's thread under g_codec_seq, read by anyone (hal_bt_stream_codec) */
static volatile uint32_t g_codec_seq;
static const hal_bt_pcm_source_t *volatile g_pcm; /* must stay valid for the life of the program */
static volatile uint32_t g_st_encoded, g_st_packets, g_st_underruns, g_st_drift;

static uint16_t g_a2dp_cid;
static volatile bool g_start_ours, g_suspend_ours; /* a START / SUSPEND we asked for is outstanding: its event is not the headset's */
static uint8_t g_local_seid, g_remote_seid;
static bool g_a2dp_inited;
static uint8_t g_sbc_caps[] = {(AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO | AVDTP_SBC_JOINT_STEREO, 0xFF, 2, (uint8_t)NEUPOD_BT_SBC_BITPOOL};
static uint8_t g_sbc_config[4];
static uint8_t g_sdp_a2dp_record[150];
static const btstack_sbc_encoder_t *g_enc;
static btstack_sbc_encoder_bluedroid_t g_enc_state;
static bool g_enc_ready;
static uint16_t g_sbc_frame_len;

/* Media packet being built. Byte 0 is the SBC payload header (frame count), the frames follow. */
static uint8_t g_sbc_store[SBC_STORAGE_BYTES];
static uint16_t g_sbc_count;       /* frame bytes in g_sbc_store[1..] */
static uint16_t g_max_payload;     /* bound: min(what the link takes, our storage) minus the header byte */
static bool g_ready_to_send;       /* a packet is complete and waits for "can send now" */
static uint32_t g_first_frame_ms;  /* when the first frame of the current packet was encoded */
static uint32_t g_rtp_ts;
static int16_t g_pcm_buf[SBC_SAMPLES_PER_FRAME * 2u];

/* Pacing: the encoder is a source clock (a timer) that owes the headset rate x elapsed time of audio, steered by the ring's fill level. */
static btstack_timer_source_t g_media_timer, g_rssi_timer;
static uint32_t g_pace_last_ms;
static uint32_t g_pace_frac;       /* sub-frame remainder, in 1/1000 frame */
static uint32_t g_owed;            /* frames the encoder may produce now */
static int g_trim;                 /* the fill-level trim, in PACE_TRIM_UNIT_PPM steps: >0 consumes faster than the nominal rate */
static bool g_in_underrun;
static bool g_streaming_timer;

typedef enum { REQ_NONE = 0, REQ_OPEN, REQ_START, REQ_SUSPEND, REQ_CLOSE } stream_req_t;
static volatile int g_req;
static volatile bool g_req_queued;
static btstack_context_callback_registration_t g_req_reg;

static void emit_stream(hal_bt_stream_state_t st, hal_bt_fail_t reason) {
    hal_bt_event_t ev = {.type = HAL_BT_EVT_STREAM, .state = (hal_bt_state_t)g_state, .dev = g_peer, .stream = st, .reason = reason};
    if (st == HAL_BT_STREAM_OPEN || st == HAL_BT_STREAM_STREAMING || st == HAL_BT_STREAM_SUSPENDED) {
        ev.codec = g_codec;
    }
    emit(&ev);
}

static void set_stream(hal_bt_stream_state_t st, hal_bt_fail_t reason) {
    g_stream_reason = st == HAL_BT_STREAM_FAILED ? reason : HAL_BT_FAIL_NONE;
    int prev = __atomic_exchange_n(&g_stream, (int)st, __ATOMIC_SEQ_CST);
    if (prev != (int)st) {
        emit_stream(st, g_stream_reason);
    }
}

static void codec_publish(const hal_bt_codec_t *c) {
    __atomic_add_fetch(&g_codec_seq, 1u, __ATOMIC_SEQ_CST); /* odd: being written */
    g_codec = *c;
    __atomic_add_fetch(&g_codec_seq, 1u, __ATOMIC_SEQ_CST);
}

static void media_stop(void) {
    if (g_streaming_timer) {
        btstack_run_loop_remove_timer(&g_media_timer);
        btstack_run_loop_remove_timer(&g_rssi_timer);
        g_streaming_timer = false;
    }
    g_sbc_count = 0;
    g_ready_to_send = false;
    g_owed = 0;
    g_pace_frac = 0;
    g_trim = 0;
    g_in_underrun = false;
}

/* The stream is gone (released, signalling lost, link down): stop the clock, forget the connection, say so. The ring is emptied so a later
 * stream never starts with audio from before the loss. */
static void a2dp_gone(hal_bt_fail_t why_failed) {
    media_stop();
    g_a2dp_cid = 0;
    g_enc_ready = false;
    if (g_stream == HAL_BT_STREAM_CLOSED && why_failed == HAL_BT_FAIL_NONE) {
        return;
    }
    const hal_bt_pcm_source_t *src = g_pcm;
    if (src != NULL && src->flush != NULL) {
        src->flush(src->ctx);
    }
    if (why_failed != HAL_BT_FAIL_NONE) {
        set_stream(HAL_BT_STREAM_FAILED, why_failed);
    } else {
        set_stream(HAL_BT_STREAM_CLOSED, HAL_BT_FAIL_NONE);
    }
}

static uint32_t g_dbg_sendfail, g_dbg_peak;
#ifdef NEUPOD_BT_PCM_TAP
#include <stdlib.h>
static FILE *g_dbg_tap; /* bench aid (Y2): NEUPOD_BT_PCM_DUMP=<file> records the encoder's input PCM */
static bool g_dbg_tap_tried;
#endif
static uint8_t g_dbg_lastrc;

static void send_packet(void) {
    uint16_t frames = (uint16_t)(g_sbc_count / g_sbc_frame_len);
    g_sbc_store[0] = (uint8_t)frames; /* no fragmentation */
    uint8_t rc = a2dp_source_stream_send_media_payload_rtp(g_a2dp_cid, g_local_seid, 0, g_rtp_ts, g_sbc_store, (uint16_t)(g_sbc_count + 1u));
    if (rc != ERROR_CODE_SUCCESS) {
        g_dbg_sendfail++;
        g_dbg_lastrc = rc;
    }
    g_rtp_ts += (uint32_t)frames * SBC_SAMPLES_PER_FRAME;
    g_sbc_count = 0;
    g_ready_to_send = false;
    g_st_packets++;
}

static int trim_wanted(uint32_t fill, uint32_t cap) {
    int32_t half = (int32_t)(cap / 2u);
    if (half <= 0) {
        return 0;
    }
    int32_t err = (int32_t)fill - half; /* > 0: the ring is above the target: consume faster */
    int32_t t = err * PACE_TRIM_MAX / half;
    return t < -PACE_TRIM_MAX ? -PACE_TRIM_MAX : t > PACE_TRIM_MAX ? PACE_TRIM_MAX : (int)t;
}

static void media_tick(btstack_timer_source_t *ts) {
    btstack_run_loop_set_timer(ts, PACE_TICK_MS);
    btstack_run_loop_add_timer(ts);
    const uint32_t now = btstack_run_loop_get_time_ms();
    const uint32_t dt = now - g_pace_last_ms;
    g_pace_last_ms = now;
    const hal_bt_pcm_source_t *src = g_pcm;
    if (src == NULL || !g_enc_ready) {
        return;
    }
    const uint32_t rate = g_codec.rate;
    const uint32_t fill = src->available(src->ctx);

    /* Steer: the ring above half full means the encoder is too slow for the producer, below means too fast. One step per tick, and each
     * step counts as a drift correction. The producer (the audio core's virtual sink) stalls when the ring is full, so the link paces it. */
    int want = trim_wanted(fill, src->capacity(src->ctx));
    if (want != g_trim) {
        g_trim += want > g_trim ? 1 : -1;
        g_st_drift++;
    }
    int64_t milli = (int64_t)dt * rate + g_pace_frac; /* frames x 1000 */
    milli += milli * g_trim * PACE_TRIM_UNIT_PPM / 1000000;
    if (milli < 0) {
        milli = 0;
    }
    g_owed += (uint32_t)(milli / 1000);
    g_pace_frac = (uint32_t)(milli % 1000);
    const uint32_t cap_owed = rate * PACE_BACKLOG_MS / 1000u;
    if (g_owed > cap_owed) {
        g_owed = cap_owed; /* a stall (the link did not take packets) is not made up with a burst */
    }

    if (g_ready_to_send) {
        return; /* the finished packet still waits for "can send now": the link is the limit */
    }
    while (g_owed >= SBC_SAMPLES_PER_FRAME && (uint32_t)(g_max_payload - g_sbc_count) >= g_sbc_frame_len) {
        if (src->read(src->ctx, g_pcm_buf, SBC_SAMPLES_PER_FRAME) < SBC_SAMPLES_PER_FRAME) {
            bool active = src->active == NULL || src->active(src->ctx);
            if (active && !g_in_underrun) {
                g_st_underruns++; /* one count per dry spell, not per tick */
            }
            g_in_underrun = active;
            g_owed = 0; /* what was owed is lost: no burst when the audio returns */
            break;
        }
        g_in_underrun = false;
        if (g_sbc_count == 0) {
            g_first_frame_ms = now;
        }
        for (unsigned i = 0; i < SBC_SAMPLES_PER_FRAME * 2u; i++) {
            uint32_t a = (uint32_t)(g_pcm_buf[i] < 0 ? -(int32_t)g_pcm_buf[i] : g_pcm_buf[i]);
            g_dbg_peak = a > g_dbg_peak ? a : g_dbg_peak;
        }
#ifdef NEUPOD_BT_PCM_TAP
        if (!g_dbg_tap_tried) {
            g_dbg_tap_tried = true;
            const char *tp = getenv("NEUPOD_BT_PCM_DUMP");
            g_dbg_tap = tp != NULL && tp[0] != '\0' ? fopen(tp, "wb") : NULL;
        }
        if (g_dbg_tap != NULL) {
            fwrite(g_pcm_buf, sizeof g_pcm_buf, 1, g_dbg_tap);
        }
#endif
        g_enc->encode_signed_16(&g_enc_state, g_pcm_buf, &g_sbc_store[1u + g_sbc_count]);
        g_sbc_count = (uint16_t)(g_sbc_count + g_sbc_frame_len);
        g_owed -= SBC_SAMPLES_PER_FRAME;
        g_st_encoded++;
    }
    bool full = (uint32_t)(g_sbc_count + g_sbc_frame_len) > g_max_payload;
    bool aged = g_sbc_count > 0 && now - g_first_frame_ms >= PACE_PACKET_MAX_AGE_MS;
    if (g_sbc_count > 0 && (full || aged)) {
        g_ready_to_send = true;
        a2dp_source_stream_endpoint_request_can_send_now(g_a2dp_cid, g_local_seid);
    }
}

static void rssi_tick(btstack_timer_source_t *ts) {
    btstack_run_loop_set_timer(ts, RSSI_PERIOD_MS);
    btstack_run_loop_add_timer(ts);
    if (g_acl != HCI_CON_HANDLE_INVALID) {
        (void)gap_read_rssi(g_acl);
    }
    printf("bt: a2dp: packets %lu encoded %lu sendfail %lu (rc 0x%02X) peak %lu underruns %lu max_payload %u\n",
           (unsigned long)g_st_packets, (unsigned long)g_st_encoded, (unsigned long)g_dbg_sendfail, (unsigned)g_dbg_lastrc,
           (unsigned long)g_dbg_peak, (unsigned long)g_st_underruns, (unsigned)g_max_payload);
    g_dbg_peak = 0;
}

static void media_start(void) {
    media_stop(); /* a second STARTED without a stop in between must not put the timers in BTstack's list twice (it does not check) */
    const hal_bt_pcm_source_t *src = g_pcm;
    int room = a2dp_max_media_payload_size(g_a2dp_cid, g_local_seid) - 1; /* minus the SBC payload header byte */
    g_max_payload = (uint16_t)btstack_min(room > 0 ? room : 0, (int)SBC_STORAGE_BYTES - 1);
    g_sbc_count = 0;
    g_ready_to_send = false;
    g_pace_last_ms = btstack_run_loop_get_time_ms();
    g_pace_frac = 0;
    g_trim = 0;
    g_in_underrun = false;
    /* The ring is usually full when a stream starts (it filled while the stream was being set up): send the excess above the target at once,
     * within the backlog bound, instead of draining it at a 0.3 % trim over many seconds. */
    g_owed = 0;
    if (src != NULL) {
        uint32_t fill = src->available(src->ctx), half = src->capacity(src->ctx) / 2u;
        g_owed = fill > half ? fill - half : 0;
        uint32_t cap_owed = g_codec.rate * PACE_BACKLOG_MS / 1000u;
        g_owed = g_owed > cap_owed ? cap_owed : g_owed;
    }
    btstack_run_loop_set_timer_handler(&g_media_timer, media_tick);
    btstack_run_loop_set_timer(&g_media_timer, PACE_TICK_MS);
    btstack_run_loop_add_timer(&g_media_timer);
    btstack_run_loop_set_timer_handler(&g_rssi_timer, rssi_tick);
    btstack_run_loop_set_timer(&g_rssi_timer, 1000);
    btstack_run_loop_add_timer(&g_rssi_timer);
    g_streaming_timer = true;
}

/** Bytes in one SBC frame, two channels (stereo or joint stereo). */
static uint16_t sbc_frame_bytes(int blocks, int subbands, int bitpool, bool joint) {
    int bits = (joint ? subbands : 0) + blocks * bitpool;
    return (uint16_t)(4 + (4 * subbands * 2) / 8 + (bits + 7) / 8);
}

static bool configure_encoder(const uint8_t *packet, hal_bt_codec_t *out) {
    int rate = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
    int blocks = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet);
    int subbands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet);
    int min_bp = a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet);
    int max_bp = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);
    int alloc = (int)a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet);
    avdtp_channel_mode_t mode = (avdtp_channel_mode_t)a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet);
    btstack_sbc_channel_mode_t cm;
    hal_bt_chmode_t hm;
    switch (mode) {
    case AVDTP_CHANNEL_MODE_JOINT_STEREO: cm = SBC_CHANNEL_MODE_JOINT_STEREO; hm = HAL_BT_MODE_JOINT_STEREO; break;
    case AVDTP_CHANNEL_MODE_STEREO: cm = SBC_CHANNEL_MODE_STEREO; hm = HAL_BT_MODE_STEREO; break;
    default:
        printf("bt: a2dp: headset channel mode %d (stereo or joint stereo needed)\n", (int)mode);
        return false; /* we send stereo only */
    }
    printf("bt: a2dp: headset config %d Hz, mode %d, blocks %d, subbands %d, alloc %d, bitpool %d..%d\n", rate, (int)mode, blocks, subbands,
           alloc, min_bp, max_bp);
    if ((uint32_t)rate != A2DP_RATE || blocks != 16 || subbands != 8) {
        return false; /* the frame size (128 samples) and the rate are fixed in this version */
    }
    int bitpool = max_bp < (int)NEUPOD_BT_SBC_BITPOOL ? max_bp : (int)NEUPOD_BT_SBC_BITPOOL;
    if (bitpool < min_bp) {
        bitpool = min_bp;
    }
    g_enc = btstack_sbc_encoder_bluedroid_init_instance(&g_enc_state);
    /* BTstack's allocation constants start at 1 (the Bluetooth spec: loudness 1, SNR 2 as configured); the encoder's start at 0. */
    g_enc->configure(&g_enc_state, SBC_MODE_STANDARD, blocks, subbands, (btstack_sbc_allocation_method_t)(alloc - 1), rate, bitpool, cm);
    /* The encoder only knows its frame length after the first encode (sbc_buffer_length() is 0 until then), so compute it from the
     * A2DP spec (12.9): header 4 + scale factors 4*subbands*channels/8 + ceil((join*subbands + blocks*channels*bitpool)/8) for stereo and
     * joint stereo; for both stereo modes the bitpool covers both channels, so blocks*bitpool. */
    g_sbc_frame_len = sbc_frame_bytes(blocks, subbands, bitpool, mode == AVDTP_CHANNEL_MODE_JOINT_STEREO);
    if (g_enc->num_audio_frames(&g_enc_state) != SBC_SAMPLES_PER_FRAME || g_sbc_frame_len == 0 || g_sbc_frame_len + 1u > SBC_STORAGE_BYTES) {
        printf("bt: a2dp: encoder frame %u samples, %u bytes (storage %u)\n", (unsigned)g_enc->num_audio_frames(&g_enc_state),
               (unsigned)g_sbc_frame_len, (unsigned)SBC_STORAGE_BYTES);
        return false;
    }
    memset(out, 0, sizeof *out);
    out->codec = bitpool > (int)SBC_BITPOOL_STD ? HAL_BT_CODEC_SBC_XQ : HAL_BT_CODEC_SBC;
    out->rate = (uint32_t)rate;
    out->channels = 2;
    out->mode = hm;
    out->bitpool_min = (uint8_t)min_bp;
    out->bitpool_max = (uint8_t)max_bp;
    out->bitpool = (uint8_t)bitpool;
    out->kbps = (uint16_t)((uint32_t)g_sbc_frame_len * 8u * (uint32_t)rate / SBC_SAMPLES_PER_FRAME / 1000u);
    return true;
}

static void a2dp_packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) {
        return;
    }
    switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
    case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED: {
        uint8_t st = a2dp_subevent_signaling_connection_established_get_status(packet);
        if (st != ERROR_CODE_SUCCESS) {
            printf("bt: a2dp signalling failed 0x%02X\n", (unsigned)st);
            g_a2dp_cid = 0;
            if (g_stream == HAL_BT_STREAM_OPENING) {
                set_stream(HAL_BT_STREAM_FAILED, HAL_BT_FAIL_STREAM);
            }
        } else {
            g_a2dp_cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
            avrcp_arm_connect();
        }
        break;
    }
    case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION: {
        if (a2dp_subevent_signaling_media_codec_sbc_configuration_get_a2dp_cid(packet) != g_a2dp_cid) {
            break;
        }
        g_remote_seid = a2dp_subevent_signaling_media_codec_sbc_configuration_get_remote_seid(packet);
        hal_bt_codec_t c;
        if (!configure_encoder(packet, &c)) {
            printf("bt: a2dp: headset configuration not usable\n");
            g_enc_ready = false;
            set_stream(HAL_BT_STREAM_FAILED, HAL_BT_FAIL_CODEC);
            if (g_a2dp_cid != 0) {
                (void)a2dp_source_disconnect(g_a2dp_cid);
            }
            break;
        }
        codec_publish(&c);
        g_enc_ready = true;
        printf("bt: a2dp: SBC %u Hz, bitpool %u (headset %u..%u), %u kbit/s, frame %u bytes\n", (unsigned)c.rate, (unsigned)c.bitpool, (unsigned)c.bitpool_min,
               (unsigned)c.bitpool_max, (unsigned)c.kbps, (unsigned)g_sbc_frame_len);
        break;
    }
    case A2DP_SUBEVENT_STREAM_ESTABLISHED: {
        uint8_t st = a2dp_subevent_stream_established_get_status(packet);
        if (st != ERROR_CODE_SUCCESS || !g_enc_ready) {
            printf("bt: a2dp stream failed 0x%02X\n", (unsigned)st);
            if (g_stream == HAL_BT_STREAM_OPENING) {
                set_stream(HAL_BT_STREAM_FAILED, g_enc_ready ? HAL_BT_FAIL_STREAM : HAL_BT_FAIL_CODEC);
            }
            break;
        }
        g_st_encoded = g_st_packets = g_st_underruns = g_st_drift = 0;
        g_st_rssi = HAL_BT_RSSI_UNKNOWN;
        printf("bt: a2dp stream open\n");
        set_stream(HAL_BT_STREAM_OPEN, HAL_BT_FAIL_NONE);
        break;
    }
    case A2DP_SUBEVENT_STREAM_STARTED: {
        media_start();
        /* Started by the headset (an AVDTP START it sent: a stem click on a headset whose stream we had suspended) and not by our own request: the click means
         * "play". Some headsets send this instead of, or before, an AVRCP PLAY. The app resumes a paused player (bt_key_action: PLAY does nothing while playing). */
        bool remote = !__atomic_exchange_n(&g_start_ours, false, __ATOMIC_SEQ_CST);
        printf("bt: a2dp streaming%s\n", remote ? " (started by the headset)" : "");
        set_stream(HAL_BT_STREAM_STREAMING, HAL_BT_FAIL_NONE);
        if (remote) {
            emit_key(HAL_BT_KEY_PLAY);
        }
        break;
    }
    case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
        if (g_streaming_timer && g_ready_to_send && g_sbc_count > 0) {
            send_packet();
        }
        break;
    case A2DP_SUBEVENT_STREAM_SUSPENDED:
        media_stop();
        printf("bt: a2dp suspended%s\n", __atomic_exchange_n(&g_suspend_ours, false, __ATOMIC_SEQ_CST) ? "" : " (by the headset)");
        if (g_stream == HAL_BT_STREAM_STREAMING) {
            set_stream(HAL_BT_STREAM_SUSPENDED, HAL_BT_FAIL_NONE);
        }
        break;
    case A2DP_SUBEVENT_STREAM_RELEASED:
        printf("bt: a2dp stream released\n");
        media_stop();
        if (g_stream != HAL_BT_STREAM_FAILED) {
            set_stream(HAL_BT_STREAM_CLOSED, HAL_BT_FAIL_NONE);
        }
        break;
    case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        printf("bt: a2dp signalling released\n");
        if (a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet) == g_a2dp_cid) {
            bool failed = g_stream == HAL_BT_STREAM_FAILED;
            a2dp_gone(failed ? (hal_bt_fail_t)g_stream_reason : HAL_BT_FAIL_NONE);
        }
        break;
    default:
        break;
    }
}

/* Worker: open the stream on the CONNECTED link. */
static void a2dp_open(void) {
    if (g_link != HAL_BT_LINK_CONNECTED || g_acl == HCI_CON_HANDLE_INVALID) {
        return;
    }
    if (g_stream != HAL_BT_STREAM_CLOSED && g_stream != HAL_BT_STREAM_FAILED) {
        return;
    }
    g_enc_ready = false;
    set_stream(HAL_BT_STREAM_OPENING, HAL_BT_FAIL_NONE);
    bd_addr_t a;
    memcpy(a, g_peer.addr, 6);
    uint8_t rc = a2dp_source_establish_stream(a, &g_a2dp_cid);
    if (rc != ERROR_CODE_SUCCESS) {
        printf("bt: a2dp_source_establish_stream 0x%02X\n", (unsigned)rc);
        g_a2dp_cid = 0;
        set_stream(HAL_BT_STREAM_FAILED, HAL_BT_FAIL_STREAM);
    }
}

static void worker_stream_req(void *arg) {
    (void)arg;
    g_req_queued = false;
    int req = __atomic_exchange_n(&g_req, (int)REQ_NONE, __ATOMIC_SEQ_CST);
    switch (req) {
    case REQ_OPEN:
        a2dp_open();
        break;
    case REQ_START:
        if ((g_stream == HAL_BT_STREAM_OPEN || g_stream == HAL_BT_STREAM_SUSPENDED) && g_a2dp_cid != 0) {
            __atomic_store_n(&g_start_ours, true, __ATOMIC_SEQ_CST);
            uint8_t rc = a2dp_source_start_stream(g_a2dp_cid, g_local_seid);
            if (rc != ERROR_CODE_SUCCESS) {
                __atomic_store_n(&g_start_ours, false, __ATOMIC_SEQ_CST);
                printf("bt: a2dp start 0x%02X\n", (unsigned)rc);
            }
        }
        break;
    case REQ_SUSPEND:
        if (g_stream == HAL_BT_STREAM_STREAMING && g_a2dp_cid != 0) {
            __atomic_store_n(&g_suspend_ours, true, __ATOMIC_SEQ_CST);
            (void)a2dp_source_pause_stream(g_a2dp_cid, g_local_seid);
        }
        break;
    case REQ_CLOSE:
        if (g_a2dp_cid != 0 && g_stream != HAL_BT_STREAM_CLOSED) {
            if (a2dp_source_stop_stream(g_a2dp_cid, g_local_seid) != ERROR_CODE_SUCCESS) {
                (void)a2dp_source_disconnect(g_a2dp_cid);
            }
        }
        break;
    default:
        break;
    }
}

static void post_req(stream_req_t r) {
    __atomic_store_n(&g_req, (int)r, __ATOMIC_SEQ_CST);
    if (!__atomic_exchange_n(&g_req_queued, true, __ATOMIC_SEQ_CST)) {
        bt_port_post(&g_req_reg);
    }
}

static void a2dp_init_once(void) {
    if (g_a2dp_inited) {
        return;
    }
    g_a2dp_inited = true;
    g_req_reg.callback = &worker_stream_req;
    a2dp_source_init();
    a2dp_source_register_packet_handler(&a2dp_packet_handler);
    avdtp_stream_endpoint_t *ep = a2dp_source_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC, g_sbc_caps, sizeof g_sbc_caps, g_sbc_config, sizeof g_sbc_config);
    if (ep == NULL) {
        printf("bt: no memory for the stream endpoint\n");
        g_a2dp_inited = false;
        return;
    }
    avdtp_set_preferred_sampling_frequency(ep, A2DP_RATE);
    g_local_seid = avdtp_local_seid(ep);
    avdtp_source_register_delay_reporting_category(g_local_seid);
    /* Our own SDP record (an A2DP source), as the verified bench tool registers it: some headsets read it before they accept the stream. */
    sdp_init();
    memset(g_sdp_a2dp_record, 0, sizeof g_sdp_a2dp_record);
    a2dp_source_create_sdp_record(g_sdp_a2dp_record, sdp_create_service_record_handle(), AVDTP_SOURCE_FEATURE_MASK_PLAYER, NULL, NULL);
    btstack_assert(de_get_len(g_sdp_a2dp_record) <= sizeof g_sdp_a2dp_record);
    sdp_register_service(g_sdp_a2dp_record);
}

void hal_bt_set_pcm_source(const hal_bt_pcm_source_t *src) { g_pcm = src; }

hal_status_t hal_bt_stream_open(void) {
    if (g_link != HAL_BT_LINK_CONNECTED) {
        return HAL_ENODEV;
    }
    if (g_stream != HAL_BT_STREAM_CLOSED && g_stream != HAL_BT_STREAM_FAILED) {
        return HAL_EBUSY;
    }
    post_req(REQ_OPEN);
    return HAL_OK;
}

hal_status_t hal_bt_stream_start(void) {
    if (g_stream == HAL_BT_STREAM_STREAMING) {
        return HAL_OK;
    }
    if (g_stream != HAL_BT_STREAM_OPEN && g_stream != HAL_BT_STREAM_SUSPENDED) {
        return HAL_ENODEV;
    }
    post_req(REQ_START);
    return HAL_OK;
}

void hal_bt_stream_suspend(void) {
    if (g_stream == HAL_BT_STREAM_STREAMING) {
        post_req(REQ_SUSPEND);
    }
}

void hal_bt_stream_close(void) {
    if (g_stream != HAL_BT_STREAM_CLOSED) {
        post_req(REQ_CLOSE);
    }
}

hal_bt_stream_state_t hal_bt_stream_state(void) { return (hal_bt_stream_state_t)g_stream; }
hal_bt_fail_t hal_bt_stream_reason(void) { return g_stream == HAL_BT_STREAM_FAILED ? (hal_bt_fail_t)g_stream_reason : HAL_BT_FAIL_NONE; }

bool hal_bt_stream_codec(hal_bt_codec_t *out) {
    int st = g_stream;
    if (st != HAL_BT_STREAM_OPEN && st != HAL_BT_STREAM_STREAMING && st != HAL_BT_STREAM_SUSPENDED) {
        return false;
    }
    for (int tries = 0; tries < 8; tries++) {
        uint32_t q = __atomic_load_n(&g_codec_seq, __ATOMIC_SEQ_CST);
        if (q & 1u) {
            continue;
        }
        *out = g_codec;
        if (__atomic_load_n(&g_codec_seq, __ATOMIC_SEQ_CST) == q) {
            return true;
        }
    }
    return false;
}

void hal_bt_stats(hal_bt_stats_t *out) {
    out->frames_encoded = g_st_encoded;
    out->packets_sent = g_st_packets;
    out->underruns = g_st_underruns;
    out->drift_corrections = g_st_drift;
    out->link_quality = (int8_t)g_st_rssi;
}

/* ---- AVRCP: headset keys, volume, track information (stage 2c) ---- */

#define AVRCP_CONNECT_DELAY_MS 2000u /* the headset's own AVRCP connection is awaited this long after the A2DP signalling channel */
#define VOL_RETRY_MS 20u             /* a Set Absolute Volume the stack could not take yet is tried again */
#define VOL_RETRIES 10u
#define VOL_INFLIGHT_MAX 4u          /* unanswered Set Absolute Volume commands (BTstack accepts several: a fast wheel is not throttled to one per round trip) */
#define VOL_ANSWER_TIMEOUT_MS 1000u  /* an answer that does not come stops suppressing the headset's own reports after this */
#define NP_LOCK_SPINS 2000u          /* hal_bt_track_info() gives up (the app asks again) after this many tries for the track information */

static uint16_t g_avrcp_cid;
static bool g_avrcp_inited;
static uint8_t g_sdp_avrcp_tg[112]; /* de_get_len() of the records (no service name): 107 / 114 bytes, asserted at start-up */
static uint8_t g_sdp_avrcp_ct[120];
static btstack_timer_source_t g_avrcp_timer, g_vol_timer, g_np_timer;
static btstack_context_callback_registration_t g_vol_reg, g_np_reg;

/* Volume. g_vol is what hal_bt_volume_get() answers: -1 until the headset has reported one on this AVRCP channel. */
static volatile int g_vol = -1;
static volatile int g_vol_want = -1; /* the latest hal_bt_volume_set() */
static volatile bool g_vol_queued;
static uint8_t g_vol_inflight, g_vol_retry;
static int g_vol_sent = -1;
static uint32_t g_vol_sent_ms;

/* Track information from hal_bt_track_info(): one copy, shared with the app under a try-lock (neither side ever waits for the other: the app's
 * writer gives up and is asked again, BTstack's thread tries again from a timer). The values under "mirror" belong to BTstack's thread. */
static hal_bt_track_t g_np;
static volatile uint8_t g_np_lock;
static bool g_np_valid;              /* something is playing (the app gave information, not NULL) */
static uint32_t g_np_ms;             /* BTstack's clock when g_np was stored */
static volatile bool g_np_queued;
static uint32_t g_pl_len = 0xFFFFFFFFu, g_pl_pos = 0xFFFFFFFFu, g_pl_ms; /* mirror: what a play status query is answered from */
static avrcp_playback_status_t g_pl_status = AVRCP_PLAYBACK_STATUS_STOPPED;
static bool g_np_announced;          /* the headset was told about a track on this AVRCP channel */
static uint32_t g_np_announced_id;

static bool np_try_lock(uint32_t spins) {
    while (spins-- > 0u) {
        if (!__atomic_exchange_n(&g_np_lock, 1, __ATOMIC_SEQ_CST)) {
            return true;
        }
    }
    return false;
}
static void np_unlock(void) { __atomic_store_n(&g_np_lock, 0, __ATOMIC_SEQ_CST); }

static void emit_volume(uint8_t v) {
    hal_bt_event_t ev = {.type = HAL_BT_EVT_VOLUME, .state = (hal_bt_state_t)g_state, .dev = g_peer, .volume = v};
    emit(&ev);
}

static const char *key_name(hal_bt_key_t k) {
    static const char *const n[] = {"?", "PLAY", "PAUSE", "PLAY_PAUSE", "NEXT", "PREV", "STOP", "FF", "RW"};
    return (unsigned)k < sizeof n / sizeof n[0] ? n[k] : "?";
}

static void emit_key(hal_bt_key_t k) {
    hal_bt_event_t ev = {.type = HAL_BT_EVT_KEY, .state = (hal_bt_state_t)g_state, .dev = g_peer, .key = k};
    emit(&ev);
}

/* The headset's volume (a notification, or the answer to our Set Absolute Volume). The headset's own reports that cross our commands, and the answers to
 * all but the last of them, are old news: they would only make the bar jump back for a moment. */
static void volume_report(uint8_t v, bool answer) {
    uint32_t now = btstack_run_loop_get_time_ms();
    if (answer && g_vol_inflight > 0u) {
        g_vol_inflight--;
    }
    if (g_vol_inflight > 0u && now - g_vol_sent_ms < VOL_ANSWER_TIMEOUT_MS) {
        return;
    }
    g_vol_inflight = 0;
    if (v > HAL_BT_VOLUME_MAX) {
        v = (uint8_t)HAL_BT_VOLUME_MAX;
    }
    int prev = g_vol;
    g_vol = v;
    if (g_vol_want == g_vol_sent) {
        g_vol_want = g_vol_sent = v; /* nothing of ours is waiting: the headset's value is the one to move on from (it may have rounded ours) */
    }
    if (prev != (int)v) { /* the first report (prev == -1) always differs: it also says the headset has absolute volume */
        printf("bt: avrcp: headset volume %u%%\n", (unsigned)v * 100u / HAL_BT_VOLUME_MAX);
        emit_volume(v);
    }
}

static void vol_send(void);
static void vol_retry(btstack_timer_source_t *ts) {
    (void)ts;
    vol_send();
}

static void vol_send(void) {
    int want = g_vol_want;
    if (want < 0 || g_avrcp_cid == 0 || want == g_vol_sent) {
        return;
    }
    uint32_t now = btstack_run_loop_get_time_ms();
    if (g_vol_inflight > 0u && now - g_vol_sent_ms >= VOL_ANSWER_TIMEOUT_MS) {
        g_vol_inflight = 0;
    }
    if (g_vol_inflight < VOL_INFLIGHT_MAX) {
        uint8_t rc = avrcp_controller_set_absolute_volume(g_avrcp_cid, (uint8_t)want);
        if (rc == ERROR_CODE_SUCCESS) {
            g_vol_inflight++;
            g_vol_sent = want;
            g_vol_sent_ms = now;
            g_vol_retry = 0;
            return;
        }
        if (++g_vol_retry >= VOL_RETRIES) {
            g_vol_retry = 0;
            printf("bt: avrcp: set volume 0x%02X\n", (unsigned)rc);
            return;
        }
    }
    btstack_run_loop_remove_timer(&g_vol_timer);
    btstack_run_loop_set_timer_handler(&g_vol_timer, vol_retry);
    btstack_run_loop_set_timer(&g_vol_timer, VOL_RETRY_MS);
    btstack_run_loop_add_timer(&g_vol_timer);
}

static void worker_vol(void *arg) {
    (void)arg;
    g_vol_queued = false;
    vol_send();
}

static avrcp_playback_status_t map_play_status(hal_bt_play_status_t st) {
    return st == HAL_BT_PLAY_PLAYING ? AVRCP_PLAYBACK_STATUS_PLAYING : st == HAL_BT_PLAY_PAUSED ? AVRCP_PLAYBACK_STATUS_PAUSED : AVRCP_PLAYBACK_STATUS_STOPPED;
}

/* BTstack's thread: take what hal_bt_track_info() stored, answer from it, and tell the headset what changed. */
static void np_apply(void);
static void np_retry(btstack_timer_source_t *ts) {
    (void)ts;
    np_apply();
}

static void np_apply(void) {
    if (!np_try_lock(NP_LOCK_SPINS)) {
        btstack_run_loop_remove_timer(&g_np_timer);
        btstack_run_loop_set_timer_handler(&g_np_timer, np_retry);
        btstack_run_loop_set_timer(&g_np_timer, 5);
        btstack_run_loop_add_timer(&g_np_timer);
        return;
    }
    g_pl_len = g_np_valid && g_np.duration_ms != 0u ? g_np.duration_ms : 0xFFFFFFFFu;
    g_pl_pos = g_np_valid ? g_np.position_ms : 0xFFFFFFFFu;
    g_pl_ms = g_np_ms;
    g_pl_status = g_np_valid ? map_play_status(g_np.status) : AVRCP_PLAYBACK_STATUS_STOPPED;
    if (g_avrcp_cid != 0) {
        if (g_np_valid && (!g_np_announced || g_np_announced_id != g_np.track_id)) {
            avrcp_track_t tr = {.track_nr = 1, .title = g_np.title, .artist = g_np.artist, .album = g_np.album, .genre = (char *)"", .song_length_ms = g_pl_len, .song_position_ms = 0};
            big_endian_store_32(tr.track_id, 4, g_np.track_id); /* bytes 0..3 stay 0: an id of all 0xFF would mean "no track" */
            (void)avrcp_target_set_now_playing_info(g_avrcp_cid, &tr, 1); /* copies the strings; also sends "track changed" when the headset registered for it */
            g_np_announced = true;
            g_np_announced_id = g_np.track_id;
        } else if (!g_np_valid && g_np_announced) {
            (void)avrcp_target_track_changed(g_avrcp_cid, NULL);
            g_np_announced = false;
        }
        (void)avrcp_target_set_playback_status(g_avrcp_cid, g_pl_status);
    }
    np_unlock();
}

static void worker_np(void *arg) {
    (void)arg;
    g_np_queued = false;
    np_apply();
}

static void avrcp_connect_tick(btstack_timer_source_t *ts) {
    (void)ts;
    if (g_avrcp_cid != 0 || g_link != HAL_BT_LINK_CONNECTED) {
        return;
    }
    bd_addr_t a;
    memcpy(a, g_peer.addr, 6);
    uint16_t cid = 0;
    uint8_t rc = avrcp_connect(a, &cid);
    printf("bt: avrcp: opening the channel (the headset did not) 0x%02X\n", (unsigned)rc);
}

/* The A2DP signalling channel is up: give the headset a moment to open AVRCP itself, then open it. */
static void avrcp_arm_connect(void) {
    btstack_run_loop_remove_timer(&g_avrcp_timer);
    btstack_run_loop_set_timer_handler(&g_avrcp_timer, avrcp_connect_tick);
    btstack_run_loop_set_timer(&g_avrcp_timer, AVRCP_CONNECT_DELAY_MS);
    btstack_run_loop_add_timer(&g_avrcp_timer);
}

/* The link is gone (or going): the channel and everything learned on it with it. Nothing is announced: the link event says it. */
static void avrcp_gone(void) {
    btstack_run_loop_remove_timer(&g_avrcp_timer);
    btstack_run_loop_remove_timer(&g_vol_timer);
    g_avrcp_cid = 0;
    g_vol = -1;
    g_vol_want = -1;
    g_vol_sent = -1;
    g_vol_inflight = g_vol_retry = 0;
    g_np_announced = false;
}

static void avrcp_packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) {
        return;
    }
    switch (packet[2]) {
    case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
        uint8_t st = avrcp_subevent_connection_established_get_status(packet);
        if (st != ERROR_CODE_SUCCESS) {
            printf("bt: avrcp: channel failed 0x%02X\n", (unsigned)st);
            return;
        }
        if (g_link != HAL_BT_LINK_CONNECTED) {
            (void)avrcp_disconnect(avrcp_subevent_connection_established_get_avrcp_cid(packet)); /* not ours: a Pair or Connect is still being set up */
            return;
        }
        g_avrcp_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
        btstack_run_loop_remove_timer(&g_avrcp_timer);
        printf("bt: avrcp: channel open\n");
        (void)avrcp_target_support_event(g_avrcp_cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
        (void)avrcp_target_support_event(g_avrcp_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
        (void)avrcp_target_support_event(g_avrcp_cid, AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
        g_np_announced = false;
        np_apply(); /* what is playing now, if anything */
        (void)avrcp_controller_enable_notification(g_avrcp_cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED); /* the answer carries the headset's volume now */
        break;
    }
    case AVRCP_SUBEVENT_CONNECTION_RELEASED:
        printf("bt: avrcp: channel closed\n");
        if (avrcp_subevent_connection_released_get_avrcp_cid(packet) == g_avrcp_cid) {
            avrcp_gone();
        }
        break;
    default:
        break;
    }
}

/* Headset buttons: only presses are reported (FF / RW are held buttons: the headset repeats them as presses while held). */
static void avrcp_target_packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) {
        return;
    }
    switch (packet[2]) {
    case AVRCP_SUBEVENT_PLAY_STATUS_QUERY: {
        uint32_t pos = g_pl_pos;
        if (pos != 0xFFFFFFFFu && g_pl_status == AVRCP_PLAYBACK_STATUS_PLAYING) {
            pos += btstack_run_loop_get_time_ms() - g_pl_ms;
            if (g_pl_len != 0xFFFFFFFFu && pos > g_pl_len) {
                pos = g_pl_len;
            }
        }
        (void)avrcp_target_play_status(g_avrcp_cid, g_pl_len, pos, g_pl_status);
        break;
    }
    case AVRCP_SUBEVENT_OPERATION: {
        printf("bt: avrcp: key 0x%02X %s (target)\n", (unsigned)avrcp_subevent_operation_get_operation_id(packet),
               avrcp_subevent_operation_get_button_pressed(packet) ? "pressed" : "released");
        if (avrcp_subevent_operation_get_button_pressed(packet) == 0) {
            break;
        }
        hal_bt_key_t k = (hal_bt_key_t)0;
        switch ((avrcp_operation_id_t)avrcp_subevent_operation_get_operation_id(packet)) {
        case AVRCP_OPERATION_ID_PLAY: k = HAL_BT_KEY_PLAY; break;
        case AVRCP_OPERATION_ID_PAUSE: k = HAL_BT_KEY_PAUSE; break;
        case AVRCP_OPERATION_ID_STOP: k = HAL_BT_KEY_STOP; break;
        case AVRCP_OPERATION_ID_FORWARD: k = HAL_BT_KEY_NEXT; break;
        case AVRCP_OPERATION_ID_BACKWARD: k = HAL_BT_KEY_PREV; break;
        case AVRCP_OPERATION_ID_FAST_FORWARD: k = HAL_BT_KEY_FF; break;
        case AVRCP_OPERATION_ID_REWIND: k = HAL_BT_KEY_RW; break;
        default: break;
        }
        if (k != (hal_bt_key_t)0) {
            printf("bt: avrcp: key %s\n", key_name(k));
            emit_key(k);
        }
        break;
    }
    default:
        printf("bt: avrcp: target event 0x%02X\n", (unsigned)packet[2]);
        break;
    }
}

static void avrcp_controller_packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META || g_avrcp_cid == 0) {
        return;
    }
    switch (packet[2]) {
    case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
        if (avrcp_subevent_notification_volume_changed_get_avrcp_cid(packet) == g_avrcp_cid) {
            volume_report(avrcp_subevent_notification_volume_changed_get_absolute_volume(packet), false);
        }
        break;
    case AVRCP_SUBEVENT_SET_ABSOLUTE_VOLUME_RESPONSE:
        if (avrcp_subevent_set_absolute_volume_response_get_avrcp_cid(packet) == g_avrcp_cid) {
            volume_report(avrcp_subevent_set_absolute_volume_response_get_absolute_volume(packet), true);
            if (g_vol_inflight == 0u && g_vol_want >= 0 && g_vol_want != g_vol_sent) {
                vol_send(); /* the wheel moved on while we waited */
            }
        }
        break;
    case AVRCP_SUBEVENT_NOTIFICATION_STATE:
        if (avrcp_subevent_notification_state_get_event_id(packet) == AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED && avrcp_subevent_notification_state_get_status(packet) != ERROR_CODE_SUCCESS) {
            printf("bt: avrcp: the headset has no absolute volume\n"); /* no report will come: hal_bt_volume_supported() stays false */
        }
        break;
    default:
        break;
    }
}

static void avrcp_init_once(void) {
    if (g_avrcp_inited) {
        return;
    }
    g_avrcp_inited = true;
    g_vol_reg.callback = &worker_vol;
    g_np_reg.callback = &worker_np;
    avrcp_init();
    avrcp_register_packet_handler(&avrcp_packet_handler);
    avrcp_target_init();
    avrcp_target_register_packet_handler(&avrcp_target_packet_handler);
    avrcp_controller_init();
    avrcp_controller_register_packet_handler(&avrcp_controller_packet_handler);
    /* Our own SDP records, as the verified bench tool registers them: the target with the category-1 (player) features, the controller as category 2
     * (monitor / amplifier) because it is the volume it sends. */
    memset(g_sdp_avrcp_tg, 0, sizeof g_sdp_avrcp_tg);
    avrcp_target_create_sdp_record(g_sdp_avrcp_tg, sdp_create_service_record_handle(), AVRCP_FEATURE_MASK_CATEGORY_PLAYER_OR_RECORDER, NULL, NULL);
    btstack_assert(de_get_len(g_sdp_avrcp_tg) <= sizeof g_sdp_avrcp_tg);
    sdp_register_service(g_sdp_avrcp_tg);
    memset(g_sdp_avrcp_ct, 0, sizeof g_sdp_avrcp_ct);
    avrcp_controller_create_sdp_record(g_sdp_avrcp_ct, sdp_create_service_record_handle(), AVRCP_FEATURE_MASK_CATEGORY_MONITOR_OR_AMPLIFIER, NULL, NULL);
    btstack_assert(de_get_len(g_sdp_avrcp_ct) <= sizeof g_sdp_avrcp_ct);
    sdp_register_service(g_sdp_avrcp_ct);
}

bool hal_bt_volume_supported(void) { return g_link == HAL_BT_LINK_CONNECTED && g_vol >= 0; }

int hal_bt_volume_get(void) {
    int v = g_vol;
    return g_link == HAL_BT_LINK_CONNECTED ? v : -1;
}

hal_status_t hal_bt_volume_set(uint8_t volume) {
    if (!hal_bt_volume_supported()) {
        return HAL_ENODEV;
    }
    if (volume > HAL_BT_VOLUME_MAX) {
        volume = (uint8_t)HAL_BT_VOLUME_MAX;
    }
    g_vol = volume; /* the bar does not wait for the headset */
    g_vol_want = volume;
    if (!__atomic_exchange_n(&g_vol_queued, true, __ATOMIC_SEQ_CST)) {
        bt_port_post(&g_vol_reg);
    }
    return HAL_OK;
}

bool hal_bt_track_info(const hal_bt_track_t *info) {
    if (g_state != HAL_BT_ON && g_state != HAL_BT_SCANNING) {
        return true; /* the stack is not up: nothing to tell, and the app tells again */
    }
    if (!np_try_lock(NP_LOCK_SPINS)) {
        return false;
    }
    if (info != NULL) {
        g_np = *info;
        g_np_ms = btstack_run_loop_get_time_ms();
    }
    g_np_valid = info != NULL;
    np_unlock();
    if (!__atomic_exchange_n(&g_np_queued, true, __ATOMIC_SEQ_CST)) {
        bt_port_post(&g_np_reg);
    }
    return true;
}

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
        l2cap_init(); /* the SDP client and AVDTP run on it; we are never connectable outside a Pair, so only our own link can open channels */
        sdp_client_init();
        a2dp_init_once();
        avrcp_init_once();
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
    g_init = (init_args_t){.cb = cb, .ctx = ctx, .storage = bt_port_storage(storage), .rc = 0};
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

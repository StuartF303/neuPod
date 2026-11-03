// SPDX-License-Identifier: MPL-2.0
/*
 * Bluetooth classic on the CYW43439 through BTstack (docs/bluetooth.md). RTOS build only.
 *
 * Threading. BTstack is single-threaded: it runs on the CYW43 driver's async context, i.e. the worker task the SDK's
 * pico_cyw43_arch_sys_freertos creates on core 0. Every BTstack call below therefore runs on that worker: either
 * inside a BTstack callback, or because it was handed over with async_context_execute_sync() (blocking, used for
 * start-up) or btstack_run_loop_execute_on_main_thread() (non-blocking, used for scan start/stop so the UI task never
 * waits for the radio). The public hal_bt_* functions can be called from any core-0 task. Nothing here runs on core 1.
 *
 * NO FLASH. The SDK's own btstack_cyw43_init() installs a TLV store in the internal flash (pico_flash,
 * flash_safe_execute). Core 1 executes from the same XIP flash and is not lockout-aware, so that would crash the audio
 * core: this file provides btstack_cyw43_init()/btstack_cyw43_deinit() itself (the SDK's cyw43_arch_init() calls them)
 * and the SDK's btstack_cyw43.c, pico_btstack_flash_bank and pico_flash are NOT linked. Link keys go to the SD-card
 * settings store through hal_bt_storage_t.
 */
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "btstack_version.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "hal_radio.h"
#include "pico/async_context.h"
#include "pico/btstack_cyw43.h"
#include "pico/btstack_hci_transport_cyw43.h"
#include "pico/btstack_run_loop_async_context.h"
#include "pico/cyw43_arch.h"

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

/* Only touched on the BTstack worker. */
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

/* ---- events out ---- */

static void emit(const hal_bt_event_t *ev) {
    if (g_cb != NULL) {
        g_cb(ev, g_cb_ctx);
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

/* ---- the SDK's cyw43_arch_init() hooks (see the header comment) ---- */

bool btstack_cyw43_init(async_context_t *context) {
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_async_context_get_instance(context));
    hci_init(hci_transport_cyw43_instance(), NULL);
    return true;
}

void btstack_cyw43_deinit(__unused async_context_t *context) {
    hci_power_control(HCI_POWER_OFF);
    hci_close();
    btstack_run_loop_async_context_deinit();
    btstack_run_loop_deinit();
    btstack_memory_deinit();
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
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        switch (btstack_event_state_get_state(packet)) {
        case HCI_STATE_WORKING:
            on_working();
            break;
        case HCI_STATE_OFF:
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

/* ---- start-up ---- */

typedef struct {
    hal_bt_event_fn cb;
    void *ctx;
    const hal_bt_storage_t *storage;
    int rc;
} init_args_t;

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
        if (g_storage != NULL) {
            btstack_tlv_set_instance(&k_tlv, NULL);
            hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(&k_tlv, NULL));
        }
        /* Results with the signal strength and the name from the extended inquiry response. The default is the plain mode,
         * which carries neither (so the list never had a dBm value). Sent during start-up, before the first inquiry. */
        hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
        gap_set_local_name(LOCAL_NAME);
        gap_set_class_of_device(LOCAL_COD);
        gap_discoverable_control(0); /* stage 1: not discoverable, not connectable; nobody can pair with us yet */
        gap_connectable_control(0);
    }
    set_state(HAL_BT_STARTING);
    a->rc = hci_power_control(HCI_POWER_ON);
    if (a->rc != 0) {
        printf("bt: hci_power_control(ON) failed (%d)\n", a->rc);
        set_state(HAL_BT_FAILED);
    }
    return 0;
}

hal_status_t hal_bt_init(hal_bt_event_fn cb, void *ctx, const hal_bt_storage_t *storage) {
    async_context_t *ac = cyw43_arch_async_context();
    if (ac == NULL) {
        return HAL_ENODEV; /* the radio driver is not up */
    }
    if (g_state != HAL_BT_OFF && g_state != HAL_BT_FAILED) {
        return HAL_EBUSY;
    }
    init_args_t args = {.cb = cb, .ctx = ctx, .storage = storage};
    (void)async_context_execute_sync(ac, worker_init, &args);
    return args.rc == 0 ? HAL_OK : HAL_ERR;
}

hal_bt_state_t hal_bt_state(void) { return (hal_bt_state_t)g_state; }

bool hal_bt_local_address(uint8_t addr[6]) {
    if (!g_have_addr) {
        return false;
    }
    memcpy(addr, g_local, 6);
    return true;
}

void hal_bt_poll(void) {}

hal_status_t hal_bt_inquiry_start(uint8_t duration_s) {
    int expect = HAL_BT_ON;
    if (g_state == HAL_BT_SCANNING) {
        return HAL_EBUSY;
    }
    if (g_state != HAL_BT_ON) {
        return HAL_ENODEV;
    }
    if (!__atomic_compare_exchange_n(&g_state, &expect, (int)HAL_BT_SCANNING, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        return HAL_EBUSY;
    }
    uint32_t units = ((uint32_t)duration_s * 1000u + 1279u) / 1280u; /* 1.28 s units */
    g_start_units = (uint8_t)(units < 1u ? 1u : units > 48u ? 48u : units);
    g_start_queued = true;
    btstack_run_loop_execute_on_main_thread(&g_start_reg);
    return HAL_OK;
}

void hal_bt_inquiry_stop(void) {
    if (g_state != HAL_BT_SCANNING || g_stop_queued) {
        return;
    }
    g_stop_queued = true;
    btstack_run_loop_execute_on_main_thread(&g_stop_reg);
}

hal_status_t hal_radio_a2dp_connect(const uint8_t addr[6]) {
    (void)addr;
    return HAL_ENOTSUP; /* stage 2 (docs/bluetooth.md) */
}

void hal_radio_a2dp_disconnect(void) {}

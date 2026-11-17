// SPDX-License-Identifier: MPL-2.0
/*
 * bt_port.h on the Innioasis Y2: BTstack over the MediaTek combo chip's /dev/stpbt, on the POSIX run loop in its own pthread
 * (docs/bluetooth.md, ADR-0019). The init script S80mtk-bt has already loaded the chip's firmware and made the device node.
 *
 * Why a thread of its own, and what that means under the FreeRTOS POSIX port. RTOS tasks are pthreads that the port runs one at a
 * time, and it keeps its critical sections in global state: a thread that is not an RTOS task must never call any FreeRTOS function
 * (queues, mutexes, event groups, the delay). The BTstack thread therefore
 *   - blocks every signal (the port's 250 Hz tick is a SIGALRM, sent to the running task's thread only, but nothing may interrupt
 *     BTstack's poll() loop or a read/write on the chip),
 *   - never calls into the app: events go through a lock-free ring and are delivered by bt_port_poll() from an RTOS task,
 *   - never touches the settings store: link keys go through y2_bt_tlv (a RAM copy, written back by the same poll),
 * and RTOS tasks never block natively on it: they hand work over with btstack_run_loop_execute_on_main_thread() (a short native
 * lock that only BTstack's thread and one other task contend for at a time, see bt_port_post) and wait by sleeping through the scheduler.
 *
 * Start-up. y2_bt_probe() (hal_system_late_init) reads the address and opens /dev/stpbt, which powers the chip and takes about 1.3 s,
 * on a helper thread so the tick cannot interrupt the open. The descriptor is kept and given to btstack_uart_stpbt, so the
 * device is opened once. Then hal_bt_init() -> bt_port_start() sets BTstack up and starts the run loop thread.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bt_port.h"
#include "btstack.h"
#include "btstack_chipset_mtk_consys.h"
#include "btstack_run_loop_posix.h"
#include "hci_dump_posix_fs.h"
#include <stdlib.h>
#include "btstack_uart_stpbt.h"
#include "hal_system.h"
#include "hci_transport_h4.h"
#include "y2_bt_addr.h"
#include "y2_bt_hciup.h"
#include "y2_bt_tlv.h"
#include "y2_hal.h"

#define STPBT_DEVICE "/dev/stpbt"
#define PROBE_TIMEOUT_MS 8000u /* the open takes about 1.3 s */
#define SYNC_TIMEOUT_MS 15000u
#define RING_LEN 64u /* events: the UI polls every frame; a scan is at most about 30 events */
#define THREAD_STACK (512u * 1024u)

enum { PR_NONE = 0, PR_PENDING, PR_DONE, PR_ABANDONED };

static volatile int g_probe = PR_NONE;
static bool g_probe_ok;
static int g_dev_fd = -1;
static int g_dev_err;
static uint8_t g_addr[6];

static bool g_started;
static volatile bool g_running; /* poll() may work: the thread exists and the store is attached */

/* ---- threads ---- */

/* Start a detached thread with every signal blocked from its first instruction (it inherits the creator's mask). */
static bool start_thread(void *(*fn)(void *)) {
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, THREAD_STACK);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    int rc = pthread_create(&t, &at, fn, NULL);
    pthread_attr_destroy(&at);
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    return rc == 0;
}

/* Bench aid: the HCI trace is written with plain write() calls (no stdio buffer), so it is in the page cache at once but reaches the disk only when
 * the kernel gets to it (up to about 30 s later, and never on a crash or a power cut of the Y2). A thread that is not an RTOS task (it calls no
 * RTOS function) fsync()s it every DUMP_SYNC_S through a second descriptor of the same file, so a walk's trace survives whatever ended it. */
#define DUMP_SYNC_S 5
static int g_dump_fd = -1;

static void *dump_sync_thread(void *arg) {
    (void)arg;
    for (;;) {
        struct timespec ts = {.tv_sec = DUMP_SYNC_S, .tv_nsec = 0};
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        }
        (void)fsync(g_dump_fd);
    }
    return NULL;
}

static void *probe_thread(void *arg) {
    (void)arg;
    int fd = open(STPBT_DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK); // the vendor stack's flags; powers the chip on
    int err = errno;
    g_dev_fd = fd;
    g_dev_err = fd < 0 ? err : 0;
    int expect = PR_PENDING;
    if (!__atomic_compare_exchange_n(&g_probe, &expect, PR_DONE, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) && fd >= 0) {
        close(fd); // nobody waits for it any more
        g_dev_fd = -1;
    }
    return NULL;
}

bool y2_bt_probe(void) {
    if (g_probe != PR_NONE) {
        return g_probe_ok;
    }
    y2_bt_addr_result_t ar = y2_bt_addr_read(Y2_BT_ADDR_PATH, g_addr);
    if (ar != Y2_BT_ADDR_OK) {
        printf("bt: %s %s: Bluetooth unavailable\n", Y2_BT_ADDR_PATH, y2_bt_addr_result_text(ar));
        g_probe = PR_DONE;
        return false;
    }
    g_probe = PR_PENDING;
    if (!start_thread(probe_thread)) {
        printf("bt: cannot start the device probe\n");
        g_probe = PR_DONE;
        return false;
    }
    for (uint32_t waited = 0; g_probe == PR_PENDING && waited < PROBE_TIMEOUT_MS; waited += 10u) {
        hal_delay_ms(10);
    }
    int expect = PR_PENDING;
    if (__atomic_compare_exchange_n(&g_probe, &expect, PR_ABANDONED, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        printf("bt: %s did not open within %u s\n", STPBT_DEVICE, (unsigned)(PROBE_TIMEOUT_MS / 1000u));
        return false;
    }
    if (g_dev_fd < 0) {
        printf("bt: cannot open %s: %s (is S80mtk-bt done and /opt/mtk-wmt staged?)\n", STPBT_DEVICE, strerror(g_dev_err));
        return false;
    }
    g_probe_ok = true;
    return true;
}

/* ---- the event ring: BTstack's thread produces, one task at a time consumes ---- */

static hal_bt_event_t g_ring[RING_LEN];
static uint32_t g_head, g_tail; /* free-running; head written by BTstack's thread, tail by the consuming task */
static hal_bt_event_fn g_cb;
static void *g_cb_ctx;
static volatile uint32_t g_dropped;
static volatile int g_poll_busy;

void bt_port_deliver(hal_bt_event_fn cb, void *ctx, const hal_bt_event_t *ev) {
    g_cb = cb;
    g_cb_ctx = ctx;
    uint32_t head = g_head;
    if (head - __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE) >= RING_LEN) {
        g_dropped++; /* the UI has not polled for a long time: never hold BTstack up */
        return;
    }
    g_ring[head % RING_LEN] = *ev;
    __atomic_store_n(&g_head, head + 1u, __ATOMIC_RELEASE);
}

void bt_port_poll(void) {
    if (!g_running || __atomic_exchange_n(&g_poll_busy, 1, __ATOMIC_ACQUIRE)) {
        return; // not started, or another task is delivering: events stay in order
    }
    for (;;) {
        uint32_t tail = g_tail;
        if (tail == __atomic_load_n(&g_head, __ATOMIC_ACQUIRE)) {
            break;
        }
        hal_bt_event_t ev = g_ring[tail % RING_LEN];
        __atomic_store_n(&g_tail, tail + 1u, __ATOMIC_RELEASE);
        if (g_cb != NULL) {
            g_cb(&ev, g_cb_ctx);
        }
    }
    uint32_t dropped = __atomic_exchange_n(&g_dropped, 0u, __ATOMIC_RELAXED);
    if (dropped != 0u) {
        printf("bt: %u events dropped (not polled)\n", (unsigned)dropped);
    }
    y2_bt_tlv_flush(hal_millis());
    __atomic_store_n(&g_poll_busy, 0, __ATOMIC_RELEASE);
}

/* ---- hand-over to BTstack's thread ---- */

static volatile int g_post_lock;

/* The POSIX run loop guards its callback list with a native mutex. Two tasks must not contend for it: the RTOS could switch out the holder
 * while the other, higher-priority one waits natively, and the holder would never run again. So tasks take turns here, sleeping
 * through the scheduler, and only BTstack's thread (never switched out) and one task meet on the mutex. */
void bt_port_post(btstack_context_callback_registration_t *reg) {
    while (__atomic_exchange_n(&g_post_lock, 1, __ATOMIC_ACQUIRE)) {
        hal_delay_ms(1);
    }
    btstack_run_loop_execute_on_main_thread(reg);
    __atomic_store_n(&g_post_lock, 0, __ATOMIC_RELEASE);
}

static struct {
    btstack_context_callback_registration_t reg;
    bt_port_fn fn;
    void *arg;
    volatile int busy, done;
} g_sync;

static void sync_run(void *ctx) {
    (void)ctx;
    (void)g_sync.fn(g_sync.arg);
    __atomic_store_n(&g_sync.done, 1, __ATOMIC_RELEASE);
}

bool bt_port_run_sync(bt_port_fn fn, void *arg) {
    if (__atomic_exchange_n(&g_sync.busy, 1, __ATOMIC_ACQUIRE)) {
        return false; // an earlier request never finished: BTstack's thread is stuck
    }
    g_sync.fn = fn;
    g_sync.arg = arg;
    g_sync.done = 0;
    g_sync.reg.callback = sync_run;
    g_sync.reg.context = NULL;
    bt_port_post(&g_sync.reg);
    for (uint32_t waited = 0; !__atomic_load_n(&g_sync.done, __ATOMIC_ACQUIRE) && waited < SYNC_TIMEOUT_MS; waited += 2u) {
        hal_delay_ms(2); // power-on only queues the HCI start-up (the device is already open); the chip's script then runs on BTstack's thread
    }
    bool done = __atomic_load_n(&g_sync.done, __ATOMIC_ACQUIRE) != 0;
    if (done) {
        __atomic_store_n(&g_sync.busy, 0, __ATOMIC_RELEASE);
    }
    return done;
}

int bt_port_power(bool on) { return hci_power_control(on ? HCI_POWER_ON : HCI_POWER_OFF); }

/* ---- controller commands at HCI-up (stock parity, bench switches in y2_bt_hciup.h) ---- */

/* The stock MediaTek stack writes the inquiry transmit power (0x0C59) before every inquiry and sends the vendor command 0xFC66 once after
 * HCI_Reset; we sent neither, and the Y2 sometimes found no AirPods. The power is controller state that lasts until the controller is reset, so
 * writing it at every HCI-up (first start and each power-on after sleep or a phantom-link reset) is the same as writing it before each inquiry,
 * and it keeps the inquiry path free of extra commands. The commands go out one at a time with BTstack's own can-send check, from the HCI event
 * handler on BTstack's thread, before the adapter reports the radio ON. HCI_Reset itself was done by BTstack's init before it said WORKING, so
 * FC66 is "after HCI_Reset" as in stock.
 * Failure stance: fail open. A command that is refused (status != 0, including 0x01 unknown command) or never answered is logged and skipped. */
#define HCIUP_ANSWER_MS 2000u /* one command's answer */
#define HCIUP_TOTAL_MS 6000u  /* the whole sequence, including waiting for the controller to take a command */

static y2_hciup_plan_t g_up_plan;
static y2_hciup_cmd_t g_up_cmd;
static uint8_t g_up_buf[3 + sizeof g_up_cmd.params];
static void (*g_up_done)(void);
static bool g_up_active, g_up_inflight, g_up_env_logged, g_up_registered;
static uint32_t g_up_start;
static btstack_timer_source_t g_up_timer;
static btstack_packet_callback_registration_t g_up_reg;

static void up_finish(void) {
    btstack_run_loop_remove_timer(&g_up_timer);
    g_up_active = g_up_inflight = false;
    void (*done)(void) = g_up_done;
    g_up_done = NULL;
    if (done != NULL) {
        done();
    }
}

static void up_pump(void);

static void up_timer(btstack_timer_source_t *ts) {
    (void)ts;
    if (g_up_inflight) {
        printf("bt: controller set-up command 0x%04X: no answer within %u ms, carrying on\n", (unsigned)g_up_cmd.opcode, (unsigned)HCIUP_ANSWER_MS);
        g_up_inflight = false;
    }
    up_pump();
}

static void up_pump(void) {
    if (!g_up_active || g_up_inflight) {
        return;
    }
    if (btstack_run_loop_get_time_ms() - g_up_start > HCIUP_TOTAL_MS) {
        printf("bt: controller set-up: gave up waiting (%u ms), carrying on\n", (unsigned)HCIUP_TOTAL_MS);
        up_finish();
        return;
    }
    if (!hci_can_send_command_packet_now()) {
        btstack_run_loop_set_timer(&g_up_timer, 5);
        btstack_run_loop_add_timer(&g_up_timer);
        return;
    }
    if (!y2_hciup_next(&g_up_plan, &g_up_cmd)) {
        up_finish();
        return;
    }
    little_endian_store_16(g_up_buf, 0, g_up_cmd.opcode);
    g_up_buf[2] = g_up_cmd.len;
    memcpy(&g_up_buf[3], g_up_cmd.params, g_up_cmd.len);
    g_up_inflight = true;
    btstack_run_loop_remove_timer(&g_up_timer);
    btstack_run_loop_set_timer(&g_up_timer, HCIUP_ANSWER_MS);
    btstack_run_loop_add_timer(&g_up_timer);
    uint8_t rc = hci_send_cmd_packet(g_up_buf, 3 + g_up_cmd.len);
    if (rc != 0) {
        printf("bt: controller set-up command 0x%04X not sent (0x%02X), carrying on\n", (unsigned)g_up_cmd.opcode, (unsigned)rc);
        g_up_inflight = false;
        btstack_run_loop_remove_timer(&g_up_timer);
        up_pump();
    }
}

/* The answer to the command in flight: log it by what it was, then the next one. @p status / @p data: the status byte and what follows it. */
static void up_answer(uint8_t status, const uint8_t *data, uint16_t data_len) {
    bool ok = status == 0;
    switch (g_up_cmd.step) {
    case Y2_HCIUP_FC66:
        printf("bt: MTK vendor command FC66 (00 10 40 9C) -> status 0x%02X%s\n", (unsigned)status, ok ? "" : " (refused, carrying on)");
        break;
    case Y2_HCIUP_READ_TXPWR:
        if (ok && data_len >= 1) {
            printf("bt: inquiry tx power (controller default) %d dBm\n", (int)(int8_t)data[0]);
        } else {
            printf("bt: inquiry tx power read refused (status 0x%02X), carrying on\n", (unsigned)status);
        }
        break;
    case Y2_HCIUP_WRITE_TXPWR:
        printf("bt: inquiry tx power set to %d dBm -> status 0x%02X%s\n", (int)g_up_plan.txpwr, (unsigned)status, ok ? "" : " (refused, carrying on)");
        break;
    case Y2_HCIUP_READBACK:
        if (ok && data_len >= 1) {
            printf("bt: inquiry tx power (read back) %d dBm\n", (int)(int8_t)data[0]);
        } else {
            printf("bt: inquiry tx power read-back refused (status 0x%02X), carrying on\n", (unsigned)status);
        }
        break;
    }
    g_up_inflight = false;
    btstack_run_loop_remove_timer(&g_up_timer);
    up_pump();
}

static void up_event(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    if (type != HCI_EVENT_PACKET || !g_up_active || size < 3) {
        return;
    }
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
            /* the controller went down under the sequence (a power cycle): drop it; HCI-up runs it again */
            btstack_run_loop_remove_timer(&g_up_timer);
            g_up_active = g_up_inflight = false;
            g_up_done = NULL;
        }
        break;
    case HCI_EVENT_COMMAND_COMPLETE:
        if (g_up_inflight && size >= 6 && little_endian_read_16(packet, 3) == g_up_cmd.opcode) {
            up_answer(packet[5], &packet[6], (uint16_t)(size - 6));
        }
        break;
    case HCI_EVENT_COMMAND_STATUS:
        /* a refusal comes as Command Status (an unknown command often does); an accepted one is followed by its own event, which none of ours has */
        if (g_up_inflight && size >= 6 && little_endian_read_16(packet, 4) == g_up_cmd.opcode) {
            up_answer(packet[2] != 0 ? packet[2] : 0xFF, NULL, 0);
        }
        break;
    default:
        break;
    }
}

bool bt_port_hci_up(void (*done)(void)) {
    const char *tx = getenv("NEUPOD_BT_INQ_TXPWR");
    int8_t pwr = 0;
    y2_inq_txpwr_result_t r = y2_bt_parse_inq_txpwr(tx, &pwr);
    bool fc66 = y2_bt_parse_fc66(getenv("NEUPOD_BT_MTK_FC66"));
    if (!g_up_env_logged) {
        g_up_env_logged = true;
        if (r == Y2_INQ_TXPWR_UNSET) {
            printf("bt: NEUPOD_BT_INQ_TXPWR not set: the inquiry transmit power is left as the controller has it\n");
        } else if (r == Y2_INQ_TXPWR_INVALID) {
            printf("bt: NEUPOD_BT_INQ_TXPWR='%s' is not an integer from %d to %d: ignored\n", tx, Y2_INQ_TXPWR_MIN, Y2_INQ_TXPWR_MAX);
        }
        if (fc66) {
            printf("bt: NEUPOD_BT_MTK_FC66=1: the MediaTek vendor command FC66 follows every HCI reset\n");
        }
    }
    if (!g_up_registered) {
        g_up_registered = true;
        g_up_reg.callback = up_event;
        hci_add_event_handler(&g_up_reg);
    }
    y2_hciup_plan_init(&g_up_plan, fc66, r == Y2_INQ_TXPWR_OK, pwr);
    g_up_done = done;
    g_up_active = true;
    g_up_inflight = false;
    g_up_start = btstack_run_loop_get_time_ms();
    btstack_run_loop_set_timer_handler(&g_up_timer, up_timer);
    up_pump();
    return true;
}

/* ---- start-up ---- */

static void transport_error(const char *message) {
    printf("bt: transport error: %s\n", message); // the controller went away; a restart of the firmware (after S80mtk-bt) brings it back
}

static void *run_loop_thread(void *arg) {
    (void)arg;
    btstack_run_loop_execute(); // returns only if the run loop is told to exit, which nothing does
    return NULL;
}

bool bt_port_present(void) { return g_probe_ok; }

const hal_bt_storage_t *bt_port_storage(const hal_bt_storage_t *backing) { return y2_bt_tlv_attach(backing); }

bool bt_port_start(void) {
    if (g_started) {
        return true;
    }
    static hci_transport_config_uart_t transport_config = {
        .type = HCI_TRANSPORT_CONFIG_UART,
        .device_name = STPBT_DEVICE,
        .baudrate_init = 115200, // ignored by the stpbt driver
        .baudrate_main = 0,
        .flowcontrol = BTSTACK_UART_FLOWCONTROL_OFF,
        .parity = BTSTACK_UART_PARITY_OFF,
    };
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_posix_get_instance());
    /* Bench aid: NEUPOD_BT_HCI_DUMP=<file> writes an HCI trace (PacketLogger format) of the whole session. */
    const char *dump = getenv("NEUPOD_BT_HCI_DUMP");
    if (dump != NULL && dump[0] != '\0') {
        hci_dump_posix_fs_open(dump, HCI_DUMP_PACKETLOGGER);
        hci_dump_init(hci_dump_posix_fs_get_instance());
        printf("bt: HCI trace -> %s (synced to the disk every %d s)\n", dump, DUMP_SYNC_S);
        g_dump_fd = open(dump, O_RDONLY | O_CLOEXEC); // fsync() needs no write access; the trace file now exists
        if (g_dump_fd < 0 || !start_thread(dump_sync_thread)) {
            printf("bt: HCI trace: cannot sync it (%s): is the directory there and writable?\n", strerror(errno));
        }
    }
    btstack_uart_stpbt_set_fd(g_dev_fd); // opened by y2_bt_probe(); the driver owns it from here
    g_dev_fd = -1;
    btstack_uart_stpbt_set_error_handler(transport_error);
    hci_init(hci_transport_h4_instance_for_uart(btstack_uart_stpbt_instance()), &transport_config);
    // The MediaTek vendor script (address, radio, TX power, sleep) runs before HCI_Reset. The address is the one in /data/BT_Addr.
    bd_addr_t a;
    memcpy(a, g_addr, 6);
    btstack_chipset_mtk_consys_set_bd_addr(a);
    hci_set_chipset(btstack_chipset_mtk_consys_instance());
    hci_enable_custom_pre_init();
    if (!start_thread(run_loop_thread)) {
        printf("bt: cannot start the BTstack thread\n");
        return false;
    }
    g_started = true;
    g_running = true;
    return true;
}

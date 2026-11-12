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
#include <unistd.h>

#include "bt_port.h"
#include "btstack.h"
#include "btstack_chipset_mtk_consys.h"
#include "btstack_run_loop_posix.h"
#include "btstack_uart_stpbt.h"
#include "hal_system.h"
#include "hci_transport_h4.h"
#include "y2_bt_addr.h"
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

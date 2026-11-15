// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 power: external power from the power_supply nodes, and system sleep (suspend to RAM) on the stock 3.4.67 kernel's Android-style power management.
 * The rules, the order of the steps and the race reasoning are in y2_suspend.h; the decisions are in y2_suspend.c (tested on the PC). This file does the I/O.
 *
 * The sleep runs on a helper thread that is not an RTOS task (the POSIX port rule, as in bt_port_stpbt.c): every signal blocked, no RTOS call. It has to be
 * its own thread for two reasons: the write that requests the suspend may (on a kernel without early suspend) not return until the system has woken, and the
 * thaw must be answered within microseconds (wake lock back, state "on") without waiting for the calling task's next turn. The calling task (the UI task)
 * waits for it through the scheduler, so the other tasks keep running until the system actually stops.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "hal_power.h"
#include "hal_system.h"
#include "y2_hal.h"
#include "y2_input.h"
#include "y2_power.h"
#include "y2_suspend.h"

#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7 /* counts while the system is suspended; the kernel has it since 2.6.39 */
#endif

#define POLL_MS 20
#define MAX_NODES 3

static bool g_inited;
static bool g_can_sleep;

static void unlock_at_exit(void) { (void)y2_susp_write(Y2_POWER_DIR, "wake_unlock", Y2_WAKELOCK_NAME); }

static bool read_text(const char *path, char *out, size_t cap) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    ssize_t n;
    do {
        n = read(fd, out, cap - 1);
    } while (n < 0 && errno == EINTR);
    close(fd);
    out[n > 0 ? n : 0] = '\0';
    return n > 0;
}

hal_status_t hal_power_init(void) {
    if (g_inited) {
        return HAL_OK;
    }
    g_inited = true;
    if (!y2_susp_env_enabled(getenv(Y2_SUSP_ENV))) {
        printf("power: sleep switched off (%s=0): the player powers off when idle instead\n", Y2_SUSP_ENV);
        return HAL_OK;
    }
    char state[64];
    if (!read_text(Y2_POWER_DIR "/state", state, sizeof state) || !y2_susp_state_has_mem(state)) {
        printf("power: the kernel offers no suspend to RAM (%s/state): the player powers off when idle instead\n", Y2_POWER_DIR);
        return HAL_OK;
    }
    int afd = open(Y2_ALARM_DEV, O_RDWR | O_CLOEXEC);
    if (afd < 0) {
        printf("power: no alarm device (%s: %s), so no way to wake for the 24 h power-off: the player powers off when idle instead\n", Y2_ALARM_DEV, strerror(errno));
        return HAL_OK;
    }
    close(afd);
    g_can_sleep = true;
    // Awake means holding this lock: nothing else may suspend the system under the player. It is dropped only to sleep (and at exit, so a stopped
    // firmware does not leave the lock behind). Without /sys/power/wake_lock (a kernel with plain suspend) there is nothing to hold.
    if (y2_susp_write(Y2_POWER_DIR, "wake_lock", Y2_WAKELOCK_NAME)) {
        atexit(unlock_at_exit); /* registered after main()'s own, so it runs before main()'s poweroff hook (last in, first out) */
    }
    printf("power: sleep available (suspend to RAM, wake lock \"%s\", alarm for the 24 h limit)\n", Y2_WAKELOCK_NAME);
    return HAL_OK;
}

bool hal_power_can_sleep(void) { return g_can_sleep; }

bool hal_power_external(void) { return y2_power_external_online(Y2_POWER_SUPPLY); }

// ---- the sleep ----

typedef struct {
    uint32_t max_ms;
    volatile int done; /* written last, by the helper */
    hal_status_t status;
    hal_wake_reason_t why;
} job_t;
static job_t g_job;

static int64_t clock_ms(clockid_t id) {
    struct timespec t;
    clock_gettime(id, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// Open our own descriptors on the input nodes: they only see what happens from now on, and the input task's own (opened at start-up) are not disturbed.
static int open_nodes(int fds[MAX_NODES]) {
    static const char *const k_names[MAX_NODES] = {"mtk-kpd", "mtk-tpd", "mtk-tpd-kpd"};
    int n = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        char path[64];
        if (y2_input_find_node(Y2_INPUT_CLASS, Y2_DEV_INPUT, k_names[i], path, sizeof path)) {
            int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                fds[n++] = fd;
            }
        }
    }
    return n;
}

// Read what the nodes hold; true if any of it is a key or button press.
static bool drain_presses(const int *fds, int n, const struct pollfd *p) {
    bool press = false;
    for (int i = 0; i < n; i++) {
        if ((p[i].revents & POLLIN) == 0) {
            continue;
        }
        uint8_t buf[Y2_EVENT_SIZE * 32];
        ssize_t r;
        do {
            r = read(fds[i], buf, sizeof buf);
            if (r > 0 && y2_susp_has_press(buf, (size_t)r)) {
                press = true;
            }
        } while (r > 0);
    }
    return press;
}

static void *sleep_thread(void *arg) {
    job_t *j = arg;
    j->status = HAL_ERR;
    j->why = HAL_WAKE_NONE;
    int fds[MAX_NODES];
    const int n = open_nodes(fds);
    const int afd = open(Y2_ALARM_DEV, O_RDWR | O_CLOEXEC);
    bool requested = false;
    if (afd >= 0) {
        y2_alarm_ts_t now;
        if (ioctl(afd, Y2_ALARM_GET_TIME(Y2_ALARM_RTC_WAKEUP), &now) != 0) {
            struct timespec rt;
            clock_gettime(CLOCK_REALTIME, &rt);
            now = (y2_alarm_ts_t){.sec = (int32_t)rt.tv_sec, .nsec = (int32_t)rt.tv_nsec};
        }
        const y2_alarm_ts_t at = y2_alarm_after(now, j->max_ms);
        if (ioctl(afd, Y2_ALARM_SET(Y2_ALARM_RTC_WAKEUP), &at) == 0) {
            y2_susp_t s;
            y2_susp_begin(&s, j->max_ms, clock_ms(CLOCK_MONOTONIC), clock_ms(CLOCK_BOOTTIME), hal_power_external());
            (void)y2_susp_write(Y2_POWER_DIR, "wake_unlock", Y2_WAKELOCK_NAME); /* nothing of ours holds the system awake any more ... */
            requested = y2_susp_write(Y2_POWER_DIR, "state", "mem");            /* ... and the suspend is asked for last. On a kernel where this write
                                                                                   suspends, it returns after the wake and the loop below ends at once. */
            if (!requested) {
                j->status = HAL_EBUSY; /* the kernel refused the request (EBUSY: a wake lock or pending wake-up): awake, nothing lost */
            }
            struct pollfd p[MAX_NODES];
            for (int i = 0; i < n; i++) {
                p[i] = (struct pollfd){.fd = fds[i], .events = POLLIN};
            }
            while (requested) {
                (void)poll(p, (nfds_t)n, POLL_MS); /* interrupted or not: the clocks decide */
                const bool press = drain_presses(fds, n, p);
                const y2_susp_verdict_t v = y2_susp_step(&s, clock_ms(CLOCK_MONOTONIC), clock_ms(CLOCK_BOOTTIME), press, hal_power_external());
                if (v.state == Y2_SUSP_WAIT) {
                    continue;
                }
                // The thaw: the lock first (it is what keeps the kernel from suspending again), then "on" (cancels the request, late-resumes the panel).
                (void)y2_susp_write(Y2_POWER_DIR, "wake_lock", Y2_WAKELOCK_NAME);
                (void)y2_susp_write(Y2_POWER_DIR, "state", "on");
                if (v.state == Y2_SUSP_WOKE) {
                    j->status = HAL_OK;
                    j->why = v.why;
                } else {
                    j->status = HAL_EBUSY; /* it never suspended */
                }
                printf("power: %s after %u ms asleep (%s)\n", v.slept ? "woke" : "did not sleep", (unsigned)v.slept_ms,
                       v.state == Y2_SUSP_REFUSED ? "something held the system awake" : v.why == HAL_WAKE_TIMER ? "timer" : v.why == HAL_WAKE_KEY ? "key" : v.why == HAL_WAKE_EXTERNAL_POWER ? "external power" : "other");
                break;
            }
            if (!requested) { /* the request failed before anything stopped: put the lock back */
                (void)y2_susp_write(Y2_POWER_DIR, "wake_lock", Y2_WAKELOCK_NAME);
            }
        } else {
            printf("power: cannot arm the alarm: %s\n", strerror(errno));
        }
        (void)ioctl(afd, Y2_ALARM_CLEAR(Y2_ALARM_RTC_WAKEUP));
        close(afd);
    } else {
        printf("power: cannot open %s: %s\n", Y2_ALARM_DEV, strerror(errno));
    }
    for (int i = 0; i < n; i++) {
        close(fds[i]);
    }
    __atomic_store_n(&j->done, 1, __ATOMIC_RELEASE);
    return NULL;
}

hal_status_t hal_power_sleep(uint32_t max_ms, hal_wake_reason_t *why) {
    hal_wake_reason_t dummy;
    if (why == NULL) {
        why = &dummy;
    }
    *why = HAL_WAKE_NONE;
    if (!g_can_sleep) {
        return HAL_ENOTSUP;
    }
    if (g_job.done == 0 && g_job.max_ms != 0u) {
        return HAL_EBUSY; /* one sleep at a time (the previous helper has not finished) */
    }
    g_job = (job_t){.max_ms = max_ms != 0u ? max_ms : 1u, .done = 0, .status = HAL_ERR, .why = HAL_WAKE_NONE};

    // A thread with every signal blocked from its first instruction (the port's tick and the console's signals must not interrupt it), detached.
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64u * 1024u);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    const int rc = pthread_create(&t, &at, sleep_thread, &g_job);
    pthread_attr_destroy(&at);
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (rc != 0) {
        g_job.max_ms = 0u;
        return HAL_ERR;
    }
    while (__atomic_load_n(&g_job.done, __ATOMIC_ACQUIRE) == 0) {
        hal_delay_ms(POLL_MS); /* through the scheduler: the other tasks run until the system stops */
    }
    *why = g_job.why;
    const hal_status_t st = g_job.status;
    g_job.max_ms = 0u;
    return st;
}

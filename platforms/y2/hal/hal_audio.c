// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 audio. Two outputs behind the one hal_audio contract:
 *  - the wired headphone jack: the CS43131 DAC through /dev/cs43131_dac and the MT6582 AFE through /dev/eac (the logic, the
 *    safety allow-lists and the state machine are in y2_audio.c, host-tested). The block clock follows a ring that a plain
 *    pthread drains with blocking write() on /dev/eac, so the DAC's own clock paces the player. That thread never calls the RTOS.
 *    Used only while /sys/class/switch/h2w reports headphones, and only for 44.1 kHz.
 *  - otherwise a timed null sink (no speaker on the Y2): a timer thread calls the block callback on absolute deadlines, like the
 *    RP2350's alarm interrupt, so the player, gapless hand-off, progress and underrun accounting behave normally. This is also the
 *    Bluetooth route's virtual clock.
 * hal_audio_backend_name() is "cs43131" when both device nodes open (the boot check then reports audio OK), "null" otherwise.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "hal_audio.h"
#include "hal_system.h"
#include "y2_audio.h"

static hal_audio_block_cb g_cb;
static uint32_t g_rate;
static uint64_t g_t0_us;
static volatile uint32_t g_underruns; // blocks the null clock served late
static volatile bool g_open;
static volatile bool g_stop;
static volatile bool g_muted = true;
static pthread_t g_thread;
static bool g_logged;

// hal_audio_idle() waits for the next block "interrupt" (the clock thread signals after every block), like the
// RP2350's core 1 sleeping until its alarm. A timeout of one block keeps it prompt when the clock is not running.
// The condition variable (monotonic clock, so a wrong or stepped wall clock cannot stall it) is initialised by a
// constructor, before main() and before any thread exists: hal_audio_idle() can be called by core 1 before
// hal_audio_open(), so lazy initialisation there would race the clock thread.
static pthread_mutex_t g_idle_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_idle_cv;
static uint32_t g_block_seq;

__attribute__((constructor)) static void idle_init(void) {
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&g_idle_cv, &a);
    pthread_condattr_destroy(&a);
}

// ---- The wired output: device nodes, the writer thread, the glue to y2_audio.c ----

static y2_audio_t g_y2;
static pthread_once_t g_probe_once = PTHREAD_ONCE_INIT;
static bool g_dev_ready; // both nodes opened: the CS43131 path exists (the backend is "cs43131")
static int g_fd_eac = -1;
static int g_fd_dac = -1;
static pthread_t g_worker;
static volatile bool g_worker_done;

static uint64_t mono_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static void nap_us(uint64_t us) {
    struct timespec ts = {.tv_sec = (time_t)(us / 1000000u), .tv_nsec = (long)(us % 1000000u) * 1000L};
    nanosleep(&ts, NULL);
}

// The device operations. They run on the writer thread only, and use plain system calls: never the RTOS.
static int op_dac_ioctl(void *ctx, unsigned long cmd, unsigned long arg) {
    (void)ctx;
    return ioctl(g_fd_dac, cmd, arg);
}

static int op_eac_ioctl(void *ctx, unsigned long cmd, unsigned long arg) {
    (void)ctx;
    return ioctl(g_fd_eac, cmd, arg);
}

static long op_eac_write(void *ctx, const void *buf, size_t bytes) {
    (void)ctx;
    for (int tries = 0; tries < 3; tries++) {
        ssize_t w = write(g_fd_eac, buf, bytes);
        if (w >= 0 || errno != EINTR) {
            return (long)w;
        }
    }
    return -1;
}

static int op_headphones(void *ctx) {
    (void)ctx;
    int fd = open(Y2_H2W_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    char buf[16];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';
    return y2_audio_h2w_parse(buf);
}

static uint64_t op_now_us(void *ctx) {
    (void)ctx;
    return mono_us();
}

static void op_sleep_ms(void *ctx, uint32_t ms) {
    (void)ctx;
    nap_us((uint64_t)ms * 1000u);
}

static void op_log(void *ctx, const char *line) {
    (void)ctx;
    printf("%s\n", line);
}

static void *worker_thread(void *arg) {
    (void)arg;
    while (!(atomic_load(&g_y2.shutdown) != 0u && y2_audio_state(&g_y2) == Y2_DAC_OFF)) {
        if (!y2_audio_work(&g_y2)) {
            nap_us(15000); // idle: the jack is polled every 200 ms, the stream follows the clock thread
        }
    }
    g_worker_done = true;
    return NULL;
}

// Process exit (a clean stop, the power key): power the DAC down with its volume muted first, then let go of the nodes. Registered after
// main()'s own handlers, so it runs before them (atexit is last-in first-out).
static void shutdown_hook(void) {
    if (!g_dev_ready) {
        return;
    }
    y2_audio_request_shutdown(&g_y2);
    for (int i = 0; i < 300 && !g_worker_done; i++) { // up to 3 s: a write() in flight returns within an interrupt period
        nap_us(10000);
    }
    if (g_worker_done) {
        close(g_fd_eac);
        close(g_fd_dac);
    }
}

static void probe(void) {
    g_fd_dac = open(Y2_DAC_PATH, O_RDWR | O_CLOEXEC);
    g_fd_eac = open(Y2_EAC_PATH, O_RDWR | O_CLOEXEC);
    if (g_fd_dac < 0 || g_fd_eac < 0) {
        printf("audio: no wired output (%s: %s, %s: %s), null sink\n", Y2_DAC_PATH, g_fd_dac < 0 ? "cannot open" : "ok", Y2_EAC_PATH,
               g_fd_eac < 0 ? "cannot open" : "ok");
        if (g_fd_dac >= 0) {
            close(g_fd_dac);
        }
        if (g_fd_eac >= 0) {
            close(g_fd_eac);
        }
        g_fd_dac = g_fd_eac = -1;
        return;
    }
    y2_audio_ops_t ops = {.dac_ioctl = op_dac_ioctl,
                          .eac_ioctl = op_eac_ioctl,
                          .eac_write = op_eac_write,
                          .headphones = op_headphones,
                          .now_us = op_now_us,
                          .sleep_ms = op_sleep_ms,
                          .log = op_log,
                          .ctx = NULL};
    y2_audio_init(&g_y2, &ops, getenv(Y2_AUDIO_DAC_INDEX_ENV));
    if (pthread_create(&g_worker, NULL, worker_thread, NULL) != 0) {
        printf("audio: cannot start the writer thread, null sink\n");
        close(g_fd_dac);
        close(g_fd_eac);
        g_fd_dac = g_fd_eac = -1;
        return;
    }
    pthread_detach(g_worker);
    g_dev_ready = true;
    atexit(shutdown_hook);
    printf("audio: CS43131 wired output available (DAC volume index %u, reg 0x%02x), used while headphones are plugged in\n",
           (unsigned)g_y2.dac_index, (unsigned)y2_audio_dac_volume_reg(g_y2.dac_index));
}

static void ensure_probe(void) { pthread_once(&g_probe_once, probe); }

hal_status_t hal_audio_init(void) {
    ensure_probe();
    return HAL_OK;
}

static bool null_rate_supported(uint32_t r) { return r == 44100u || r == 48000u || r == 88200u || r == 96000u; }

bool hal_audio_rate_supported(uint32_t sample_rate) {
    ensure_probe();
    // The DAC path is 44.1 kHz / 16 bit only: other rates are the player's to resample (or refuse) as with any fixed-rate sink.
    return g_dev_ready ? sample_rate == Y2_AUDIO_RATE : null_rate_supported(sample_rate);
}

void hal_audio_set_block_callback(hal_audio_block_cb cb) { g_cb = cb; }

void hal_audio_set_virtual(bool virtual_sink) {
    ensure_probe();
    if (g_dev_ready) {
        y2_audio_set_virtual(&g_y2, virtual_sink); // Bluetooth: only a clock; the DAC is left alone (it powers down when idle)
    }
}

static void *clock_thread(void *arg) {
    (void)arg;
    uint64_t served = 0;
    bool following = false;
    while (!g_stop) {
        if (g_dev_ready && y2_audio_clock_follows_device(&g_y2)) {
            // The DAC paces: ask for a block when the ring has room for one. The ring is a few blocks deep, so a 2 ms poll is plenty.
            following = true;
            if (y2_audio_space(&g_y2) < HAL_AUDIO_BLOCK_FRAMES) {
                nap_us(2000);
                continue;
            }
            if (g_cb != NULL) {
                g_cb(0);
            }
        } else {
            if (following) { // back on the timer: restart its time base
                following = false;
                g_t0_us = hal_micros();
                served = 0;
            }
            uint64_t next = g_t0_us + (served + 1) * HAL_AUDIO_BLOCK_FRAMES * 1000000ull / g_rate;
            uint64_t now = hal_micros();
            if (now < next) {
                nap_us(next - now > 2000u ? 2000u : next - now); // at most 2 ms, so a switch to the DAC and a close are prompt
                continue;
            }
            uint64_t elapsed = (now - g_t0_us) * g_rate / (1000000ull * HAL_AUDIO_BLOCK_FRAMES);
            uint32_t n = (uint32_t)(elapsed > served ? elapsed - served : 1);
            served += n;
            g_underruns += n - 1;
            if (g_cb != NULL) {
                g_cb(n - 1);
            }
        }
        pthread_mutex_lock(&g_idle_mu);
        g_block_seq++;
        pthread_cond_broadcast(&g_idle_cv);
        pthread_mutex_unlock(&g_idle_mu);
    }
    return NULL;
}

hal_status_t hal_audio_open(const hal_audio_format *fmt) {
    if (fmt == NULL) {
        return HAL_EINVAL;
    }
    if (g_open) {
        return HAL_EBUSY;
    }
    ensure_probe();
    const bool virt = g_dev_ready && atomic_load(&g_y2.virt) != 0u;
    const bool ok_rate = (g_dev_ready && !virt) ? fmt->sample_rate == Y2_AUDIO_RATE : null_rate_supported(fmt->sample_rate);
    if (!ok_rate || fmt->channels < 1 || fmt->channels > 2) {
        return HAL_ENOTSUP;
    }
    if (!g_logged) {
        g_logged = true;
        printf("audio: %s\n", g_dev_ready ? "CS43131 when headphones are in, otherwise a null sink (no speaker)" : "null sink (no wired output)");
    }
    g_rate = fmt->sample_rate;
    g_t0_us = hal_micros();
    g_underruns = 0;
    g_stop = false;
    if (g_dev_ready) {
        atomic_store(&g_y2.underruns, 0u);
        y2_audio_set_open(&g_y2, true);
    }
    if (pthread_create(&g_thread, NULL, clock_thread, NULL) != 0) {
        if (g_dev_ready) {
            y2_audio_set_open(&g_y2, false);
        }
        return HAL_ERR;
    }
    g_open = true;
    return HAL_OK;
}

uint32_t hal_audio_underruns(void) { return g_underruns + (g_dev_ready ? atomic_load(&g_y2.underruns) : 0u); }

void hal_audio_idle(void) {
    struct timespec until;
    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_nsec += 6 * 1000 * 1000; // one block
    if (until.tv_nsec >= 1000000000L) {
        until.tv_nsec -= 1000000000L;
        until.tv_sec++;
    }
    pthread_mutex_lock(&g_idle_mu);
    uint32_t seen = g_block_seq;
    while (g_block_seq == seen && pthread_cond_timedwait(&g_idle_cv, &g_idle_mu, &until) == 0) {
    }
    pthread_mutex_unlock(&g_idle_mu);
}

const char *hal_audio_backend_name(void) {
    ensure_probe();
    return g_dev_ready ? "cs43131" : "null";
}

int32_t hal_audio_write(const int32_t *samples, uint32_t frames) {
    if (!g_dev_ready) {
        return (int32_t)frames; // discarded; the pacing is the clock thread's
    }
    return (int32_t)y2_audio_produce(&g_y2, samples, frames);
}

uint32_t hal_audio_free_frames(void) {
    return g_dev_ready && y2_audio_clock_follows_device(&g_y2) ? y2_audio_space(&g_y2) : HAL_AUDIO_BLOCK_FRAMES;
}

// The audio core mutes after two silent blocks and unmutes before audio ramps in; the DAC has its own mute (volume index 0), which the
// state machine handles around starting and stopping the stream, so this only keeps the flag for the statistics.
void hal_audio_mute(bool mute) { g_muted = mute; }

void hal_audio_force_mute(void) {
    if (g_dev_ready) {
        y2_audio_request_off(&g_y2);
    }
}

bool hal_audio_wired_attached(void) { return !g_dev_ready || y2_audio_headphones(&g_y2); }

void hal_audio_get_info(hal_audio_info_t *out) {
    out->rate_hz = g_open ? g_rate : 0;
    out->rate_actual_mhz = g_open ? g_rate * 1000u : 0;
    out->clkdiv_x256 = 0;
    out->muted = g_muted;
}

void hal_audio_close(void) {
    if (!g_open) {
        return;
    }
    g_stop = true;
    pthread_join(g_thread, NULL);
    g_open = false;
    if (g_dev_ready) {
        y2_audio_set_open(&g_y2, false); // the DAC stays on across tracks; it stops itself after a few seconds of silence
    }
}

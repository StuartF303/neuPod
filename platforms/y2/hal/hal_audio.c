// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 audio: a timed null sink. /proc/asound has no cards on this kernel, so nothing can make sound yet; the sink
 * consumes PCM at the real sample rate (a timer thread calls the block callback on absolute deadlines, like the
 * RP2350's alarm interrupt) so the player, gapless hand-off, progress and underrun accounting behave normally.
 * The backend name is "null", which the boot check reports as audio degraded ("no DAC output").
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "hal_audio.h"
#include "hal_system.h"

static hal_audio_block_cb g_cb;
static uint32_t g_rate;
static uint64_t g_t0_us;
static volatile uint32_t g_underruns;
static volatile bool g_open;
static volatile bool g_stop;
static pthread_t g_thread;
static bool g_logged;

// hal_audio_idle() waits for the next block "interrupt" (the clock thread signals after every block), like the
// RP2350's core 1 sleeping until its alarm. A timeout of one block keeps it prompt when the clock is not running.
static pthread_mutex_t g_idle_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_idle_cv;
static pthread_once_t g_idle_once = PTHREAD_ONCE_INIT;
static uint32_t g_block_seq;

static void idle_init(void) {
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&g_idle_cv, &a);
    pthread_condattr_destroy(&a);
}

hal_status_t hal_audio_init(void) { return HAL_OK; }

bool hal_audio_rate_supported(uint32_t sample_rate) {
    return sample_rate == 44100u || sample_rate == 48000u || sample_rate == 88200u || sample_rate == 96000u;
}

void hal_audio_set_block_callback(hal_audio_block_cb cb) { g_cb = cb; }

static void nap_us(uint64_t us) {
    struct timespec ts = {.tv_sec = (time_t)(us / 1000000u), .tv_nsec = (long)(us % 1000000u) * 1000L};
    nanosleep(&ts, NULL);
}

static void *clock_thread(void *arg) {
    (void)arg;
    uint64_t served = 0;
    while (!g_stop) {
        uint64_t next = g_t0_us + (served + 1) * HAL_AUDIO_BLOCK_FRAMES * 1000000ull / g_rate;
        uint64_t now = hal_micros();
        if (now < next) {
            nap_us(next - now); // at most one block (5.8 ms at 44.1 kHz), so close is still prompt
            continue;
        }
        uint64_t elapsed = (now - g_t0_us) * g_rate / (1000000ull * HAL_AUDIO_BLOCK_FRAMES);
        uint32_t n = (uint32_t)(elapsed > served ? elapsed - served : 1);
        served += n;
        g_underruns += n - 1;
        if (g_cb != NULL) {
            g_cb(n - 1);
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
    if (!hal_audio_rate_supported(fmt->sample_rate) || fmt->channels < 1 || fmt->channels > 2) {
        return HAL_ENOTSUP;
    }
    if (!g_logged) {
        g_logged = true;
        printf("audio: null sink (no ALSA card)\n");
    }
    g_rate = fmt->sample_rate;
    g_t0_us = hal_micros();
    g_underruns = 0;
    g_stop = false;
    if (pthread_create(&g_thread, NULL, clock_thread, NULL) != 0) {
        return HAL_ERR;
    }
    g_open = true;
    return HAL_OK;
}

uint32_t hal_audio_underruns(void) { return g_underruns; }

void hal_audio_idle(void) {
    pthread_once(&g_idle_once, idle_init);
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

const char *hal_audio_backend_name(void) { return "null"; }

int32_t hal_audio_write(const int32_t *samples, uint32_t frames) {
    (void)samples; // discarded; the pacing is the clock thread's
    return (int32_t)frames;
}

uint32_t hal_audio_free_frames(void) { return HAL_AUDIO_BLOCK_FRAMES; }

void hal_audio_mute(bool mute) { (void)mute; }

void hal_audio_force_mute(void) {}

void hal_audio_get_info(hal_audio_info_t *out) {
    out->rate_hz = g_open ? g_rate : 0;
    out->rate_actual_mhz = g_open ? g_rate * 1000u : 0;
    out->clkdiv_x256 = 0;
    out->muted = false;
}

void hal_audio_close(void) {
    if (!g_open) {
        return;
    }
    g_stop = true;
    pthread_join(g_thread, NULL);
    g_open = false;
}

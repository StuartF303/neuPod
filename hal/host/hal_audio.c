// SPDX-License-Identifier: MPL-2.0
/*
 * Host null audio sink with a real-time block clock. Emulates the RP2350 alarm interrupt with a
 * timer thread that calls the block callback on absolute deadlines (see
 * platforms/rp2350/hal/hal_audio.c). The thread plays the role of the interrupt; the "core 1" loop
 * (hal_core.c) is another thread. Timing is only as good as the PC's scheduler.
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
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

hal_status_t hal_audio_init(void) { return HAL_OK; }

bool hal_audio_rate_supported(uint32_t sample_rate) {
    return sample_rate == 44100u || sample_rate == 48000u || sample_rate == 88200u ||
           sample_rate == 96000u;
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
            nap_us(next - now > 2000u ? 2000u : next - now); // short slices so close is prompt
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

void hal_audio_idle(void) { nap_us(500); }

const char *hal_audio_backend_name(void) { return "null"; }

int32_t hal_audio_write(const int32_t *samples, uint32_t frames) {
    (void)samples;
    return (int32_t)frames;
}

uint32_t hal_audio_free_frames(void) { return HAL_AUDIO_BLOCK_FRAMES; }

void hal_audio_mute(bool mute) { (void)mute; }

void hal_audio_close(void) {
    if (!g_open) {
        return;
    }
    g_stop = true;
    pthread_join(g_thread, NULL);
    g_open = false;
}

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

#ifdef NEUPOD_HOST_SDL
#include <SDL.h>
#include <stdlib.h>
// Audible output on the PC (32-bit samples, as the DAC gets them). Set NEUPOD_HOST_AUDIO=0 to stay silent.
static SDL_AudioDeviceID g_dev;
#endif

static hal_audio_block_cb g_cb;
static uint32_t g_rate;
static uint64_t g_t0_us;
static volatile uint32_t g_underruns;
static volatile bool g_open;
static uint32_t g_actual_mhz;
static volatile bool g_stop;
static pthread_t g_thread;

hal_status_t hal_audio_init(void) { return HAL_OK; }

bool hal_audio_rate_supported(uint32_t sample_rate) {
    return sample_rate == 44100u || sample_rate == 48000u || sample_rate == 88200u ||
           sample_rate == 96000u;
}

void hal_audio_set_block_callback(hal_audio_block_cb cb) { g_cb = cb; }

void hal_audio_set_virtual(bool virtual_sink) { (void)virtual_sink; } // the wired sink here is already only a clock

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
    g_actual_mhz = fmt->sample_rate * 1000u;
#ifdef NEUPOD_HOST_SDL
    const char *want = getenv("NEUPOD_HOST_AUDIO");
    if ((want == NULL || want[0] != '0') && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec spec = {0}, got;
        spec.freq = (int)fmt->sample_rate;
        spec.format = AUDIO_S32SYS;
        spec.channels = 2;
        spec.samples = 1024;
        g_dev = SDL_OpenAudioDevice(NULL, 0, &spec, &got, 0);
        if (g_dev != 0 && got.format == AUDIO_S32SYS && got.channels == 2) {
            SDL_PauseAudioDevice(g_dev, 0);
        } else {
            if (g_dev != 0) {
                SDL_CloseAudioDevice(g_dev);
                g_dev = 0;
            }
            // no usable device: stay on the null sink (backend name reports it)
        }
    }
#endif
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

const char *hal_audio_backend_name(void) {
#ifdef NEUPOD_HOST_SDL
    if (g_dev != 0) {
        return "sdl";
    }
#endif
    return "null";
}

int32_t hal_audio_write(const int32_t *samples, uint32_t frames) {
#ifdef NEUPOD_HOST_SDL
    // Keep the device's queue short: the timer thread and the audio device have independent clocks.
    if (g_dev != 0 && SDL_GetQueuedAudioSize(g_dev) < 6u * HAL_AUDIO_BLOCK_FRAMES * 8u) {
        SDL_QueueAudio(g_dev, samples, frames * 8u);
    }
#else
    (void)samples;
#endif
    return (int32_t)frames;
}

uint32_t hal_audio_free_frames(void) { return HAL_AUDIO_BLOCK_FRAMES; }

void hal_audio_mute(bool mute) { (void)mute; }

static volatile bool g_wired_attached = true; // the jack: headphones in
static volatile bool g_speaker_allowed;
static volatile bool g_speaker_present = true;
static volatile uint32_t g_speaker_sets;
void hal_host_set_wired_attached(bool attached) { g_wired_attached = attached; }
void hal_host_set_speaker_present(bool present) { g_speaker_present = present; }
bool hal_host_speaker_allowed(void) { return g_speaker_allowed; }
uint32_t hal_host_speaker_sets(void) { return g_speaker_sets; }
bool hal_audio_headphones_in(void) { return g_wired_attached; }
bool hal_audio_speaker_available(void) { return g_speaker_present; }
void hal_audio_set_speaker(bool allow) {
    g_speaker_allowed = allow && g_speaker_present;
    g_speaker_sets++;
}
static volatile bool g_line_allowed;
bool hal_audio_line_out_available(void) { return g_speaker_present; } /* the host's simulated Y2 hardware: one switch for both */
void hal_audio_set_line_out(bool allow) { g_line_allowed = allow && g_speaker_present; }
bool hal_host_line_out_allowed(void) { return g_line_allowed; }
bool hal_audio_wired_attached(void) { return g_wired_attached || g_speaker_allowed; }

void hal_audio_force_mute(void) {}

void hal_audio_get_info(hal_audio_info_t *out) {
    out->rate_hz = g_open ? g_rate : 0;
    out->rate_actual_mhz = g_open ? g_actual_mhz : 0;
    out->clkdiv_x256 = 0;
    out->muted = false;
}

void hal_audio_close(void) {
    if (!g_open) {
        return;
    }
    g_stop = true;
    pthread_join(g_thread, NULL);
#ifdef NEUPOD_HOST_SDL
    if (g_dev != 0) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
#endif
    g_open = false;
}

// SPDX-License-Identifier: MPL-2.0
/*
 * Null audio sink with a real-time block clock (RP2350): discards the samples but clocks its caller like the I2S DMA would
 * (hal_audio_vclk.c). Built with -DNEUPOD_AUDIO_SINK=null.
 *
 * CORE-1 RULES apply to this whole file (docs/adr/0011-bare-metal-audio-core.md): nothing that reaches the RTOS. Enforced by
 * cmake/check_core1_rules.cmake.
 */
#include "hal_audio.h"
#include "hardware/sync.h"
#include "vclk.h"

static uint32_t g_rate;
static bool g_open;

hal_status_t hal_audio_init(void) { return HAL_OK; }

bool hal_audio_rate_supported(uint32_t sample_rate) {
    return sample_rate == 44100u || sample_rate == 48000u || sample_rate == 88200u || sample_rate == 96000u;
}

void hal_audio_set_block_callback(hal_audio_block_cb cb) { vclk_set_callback(cb); }

void hal_audio_set_virtual(bool virtual_sink) { (void)virtual_sink; } /* the wired sink is already a clock only */

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
    hal_status_t st = vclk_open(fmt->sample_rate);
    if (st == HAL_OK) {
        g_rate = fmt->sample_rate;
        g_open = true;
    }
    return st;
}

uint32_t __not_in_flash_func(hal_audio_underruns)(void) { return vclk_underruns(); }

void hal_audio_idle(void) { __wfi(); }

const char *hal_audio_backend_name(void) { return "null"; }

int32_t __not_in_flash_func(hal_audio_write)(const int32_t *samples, uint32_t frames) {
    (void)samples; // discarded
    return (int32_t)frames;
}

uint32_t hal_audio_free_frames(void) { return HAL_AUDIO_BLOCK_FRAMES; }

void hal_audio_mute(bool mute) { (void)mute; }

void hal_audio_force_mute(void) {}

bool hal_audio_wired_attached(void) { return true; } // no jack detection

void hal_audio_get_info(hal_audio_info_t *out) {
    out->rate_hz = g_open ? g_rate : 0;
    out->rate_actual_mhz = 0;
    out->clkdiv_x256 = 0;
    out->muted = true;
}

void hal_audio_close(void) {
    if (!g_open) {
        return;
    }
    g_open = false;
    vclk_close();
}

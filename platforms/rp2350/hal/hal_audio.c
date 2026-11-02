// SPDX-License-Identifier: MPL-2.0
/*
 * Null audio sink with a real-time block clock (RP2350).
 *
 * There is no I2S/DAC driver yet. This backend discards the samples but clocks its caller like the
 * eventual I2S DMA would: a hardware alarm interrupt every HAL_AUDIO_BLOCK_FRAMES / rate seconds
 * on absolute deadlines of the 1 MHz timer (no drift), calling the registered block callback.
 * The alarm is armed from the core that calls hal_audio_open(), and its interrupt runs there: the
 * bare-metal audio core (core 1).
 *
 * CORE-1 RULES apply to this whole file (docs/adr/0011-bare-metal-audio-core.md): hardware
 * registers and spin-lock-only SDK calls only; nothing that reaches the RTOS (no FreeRTOS, no
 * mutex/semaphore/sleep, no stdio, no malloc). Enforced by cmake/check_core1_rules.cmake.
 */
#include "hal_audio.h"
#include "hal_system.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"

// The alarm is driven directly through the timer registers with our own interrupt handler, all in
// RAM: no SDK alarm code (flash, and a spin lock shared with core 0) on the 5.8 ms path.
static hal_audio_block_cb g_cb;
static int g_alarm = -1;
static uint32_t g_rate;
static uint64_t g_next_us;      // deadline (timer microseconds) of the block armed next
static uint32_t g_frac;         // remainder of the block period in 1/rate microsecond units
static volatile uint32_t g_underruns; // written only by the ISR
static volatile bool g_open;

hal_status_t hal_audio_init(void) { return HAL_OK; }

bool hal_audio_rate_supported(uint32_t sample_rate) {
    return sample_rate == 44100u || sample_rate == 48000u || sample_rate == 88200u ||
           sample_rate == 96000u;
}

void hal_audio_set_block_callback(hal_audio_block_cb cb) { g_cb = cb; }

// Advance g_next_us by one block period (256 * 1e6 / rate microseconds) without drift or 64-bit
// division: the fractional part is carried in g_frac. 256e6 + rate fits in 32 bits.
static inline void __not_in_flash_func(advance_deadline)(void) {
    g_frac += HAL_AUDIO_BLOCK_FRAMES * 1000000u;
    uint32_t whole = g_frac / g_rate;
    g_frac -= whole * g_rate;
    g_next_us += whole;
}

static void __not_in_flash_func(block_isr)(void) {
    timer_hw->intr = 1u << (uint)g_alarm; // acknowledge
    // Arm the next deadline. Write first, then check it is still in the future (the alarm fires on
    // an exact low-word match): if we were held off past it, count it as elapsed and try the next.
    uint32_t elapsed = 0;
    uint32_t target;
    do {
        advance_deadline();
        elapsed++;
        target = (uint32_t)g_next_us;
        timer_hw->alarm[g_alarm] = target;
    } while ((int32_t)(target - timer_hw->timerawl) <= 1);
    g_underruns += elapsed - 1;
    if (g_cb != NULL) {
        g_cb(elapsed - 1);
    }
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
    int alarm = hardware_alarm_claim_unused(false);
    if (alarm < 0) {
        return HAL_EBUSY;
    }
    g_alarm = alarm;
    g_rate = fmt->sample_rate;
    g_underruns = 0;
    g_frac = 0;
    g_next_us = hal_micros();
    advance_deadline(); // first block one period from now
    uint irq = timer_hardware_alarm_get_irq_num(PICO_DEFAULT_TIMER_INSTANCE(), (uint)alarm);
    irq_set_exclusive_handler(irq, block_isr); // enabled on THIS core (the audio core)
    g_open = true;
    timer_hw->intr = 1u << (uint)alarm;
    timer_hw->alarm[alarm] = (uint32_t)g_next_us;
    hw_set_bits(&timer_hw->inte, 1u << (uint)alarm);
    irq_set_enabled(irq, true);
    return HAL_OK;
}

uint32_t __not_in_flash_func(hal_audio_underruns)(void) { return g_underruns; }

void hal_audio_idle(void) { __wfi(); }

const char *hal_audio_backend_name(void) { return "null"; }

int32_t __not_in_flash_func(hal_audio_write)(const int32_t *samples, uint32_t frames) {
    (void)samples; // discarded
    return (int32_t)frames;
}

uint32_t hal_audio_free_frames(void) { return HAL_AUDIO_BLOCK_FRAMES; }

void hal_audio_mute(bool mute) { (void)mute; }

void hal_audio_close(void) {
    if (!g_open) {
        return;
    }
    g_open = false;
    uint irq = timer_hardware_alarm_get_irq_num(PICO_DEFAULT_TIMER_INSTANCE(), (uint)g_alarm);
    irq_set_enabled(irq, false);
    hw_clear_bits(&timer_hw->inte, 1u << (uint)g_alarm);
    irq_remove_handler(irq, block_isr);
    hardware_alarm_unclaim((uint)g_alarm);
    g_alarm = -1;
}

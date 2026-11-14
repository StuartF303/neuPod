// SPDX-License-Identifier: MPL-2.0
/*
 * Null audio sink with a real-time block clock (the board has no audio hardware): discards the samples but clocks its caller like
 * an I2S DMA would, as platforms/rp2350/hal/hal_audio_null.c does.
 *
 * The RP2350 uses a hardware alarm interrupt on core 1. Here a general-purpose timer (1 MHz) raises its interrupt on the CPU that
 * called hal_audio_open() (the audio core, CPU 1) at absolute deadlines (no drift: the 256-frame period is not a whole number of
 * microseconds, the remainder is carried), and the interrupt wakes a high-priority task pinned to that CPU, which calls the block
 * callback. The callback was written for interrupt context (no locks, no malloc, no stdio) so a task that preempts the decoder is a
 * safe place for it, and it keeps the interrupt itself to a few instructions.
 */
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_audio.h"

#define VCLK_TASK_PRIORITY 20 /* above everything of ours: the block callback must run on time */
#define VCLK_TASK_STACK 3072
#define MIN_LEAD_TICKS 20u /* an alarm closer than this to "now" is treated as already late */

static hal_audio_block_cb g_cb;
static uint32_t g_rate;
static bool g_open;

static TaskHandle_t g_task;
static gptimer_handle_t g_timer;
static uint64_t g_next;         /* deadline (timer ticks = microseconds) of the block armed next; ISR only */
static uint32_t g_frac;         /* remainder of the block period in 1/rate microsecond units; ISR only */
static volatile uint32_t g_late;      /* running total of whole blocks the ISR found already gone when re-arming (ISR writes) */
static uint32_t g_late_seen;          /* ... how much of it the clock task has already reported (task only) */
static volatile uint32_t g_underruns; /* blocks served late since hal_audio_open() (clock task writes) */
static volatile bool g_stop;

hal_status_t hal_audio_init(void) { return HAL_OK; }

bool hal_audio_rate_supported(uint32_t sample_rate) {
    return sample_rate == 44100u || sample_rate == 48000u || sample_rate == 88200u || sample_rate == 96000u;
}

void hal_audio_set_block_callback(hal_audio_block_cb cb) { g_cb = cb; }

void hal_audio_set_virtual(bool virtual_sink) { (void)virtual_sink; } /* the wired sink is already a clock only */

// Advance g_next by one block period (256 * 1e6 / rate microseconds) without drift or 64-bit division (see hal_audio_vclk.c).
static inline void IRAM_ATTR advance_deadline(void) {
    g_frac += HAL_AUDIO_BLOCK_FRAMES * 1000000u;
    uint32_t whole = g_frac / g_rate;
    g_frac -= whole * g_rate;
    g_next += whole;
}

static bool IRAM_ATTR on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *ev, void *user) {
    (void)ev;
    (void)user;
    uint64_t now = 0;
    gptimer_get_raw_count(timer, &now);
    uint32_t elapsed = 0;
    do {
        advance_deadline();
        elapsed++;
    } while (g_next <= now + MIN_LEAD_TICKS);
    gptimer_alarm_config_t a = {.alarm_count = g_next, .reload_count = 0, .flags.auto_reload_on_alarm = false};
    gptimer_set_alarm_action(timer, &a);
    if (elapsed > 1u) {
        g_late += elapsed - 1u; /* single writer: no atomic needed, and nothing outside IRAM is called from here */
    }
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(g_task, &woken);
    return woken == pdTRUE;
}

static void vclk_task(void *arg) {
    (void)arg;
    for (;;) {
        uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY); /* blocks that came due since the last wake-up (>= 1) */
        if (g_stop || n == 0u) {
            continue;
        }
        uint32_t late = g_late;
        uint32_t missed = (n - 1u) + (late - g_late_seen);
        g_late_seen = late;
        g_underruns += missed;
        if (g_cb != NULL) {
            g_cb(missed);
        }
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
    if (g_task == NULL && xTaskCreatePinnedToCore(vclk_task, "vclk", VCLK_TASK_STACK, NULL, VCLK_TASK_PRIORITY, &g_task, xPortGetCoreID()) != pdPASS) {
        g_task = NULL;
        return HAL_ERR;
    }
    gptimer_config_t tc = {.clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000};
    gptimer_event_callbacks_t cbs = {.on_alarm = on_alarm};
    if (gptimer_new_timer(&tc, &g_timer) != ESP_OK) {
        return HAL_ERR;
    }
    g_rate = fmt->sample_rate;
    g_frac = 0;
    g_next = 0;
    g_late = g_late_seen = 0;
    g_underruns = 0;
    g_stop = false;
    advance_deadline(); /* first block one period from now */
    gptimer_alarm_config_t a = {.alarm_count = g_next, .reload_count = 0, .flags.auto_reload_on_alarm = false};
    if (gptimer_register_event_callbacks(g_timer, &cbs, NULL) != ESP_OK || gptimer_set_raw_count(g_timer, 0) != ESP_OK ||
        gptimer_set_alarm_action(g_timer, &a) != ESP_OK || gptimer_enable(g_timer) != ESP_OK) { /* the interrupt is allocated on this CPU */
        gptimer_del_timer(g_timer);
        g_timer = NULL;
        return HAL_ERR;
    }
    if (gptimer_start(g_timer) != ESP_OK) {
        gptimer_disable(g_timer);
        gptimer_del_timer(g_timer);
        g_timer = NULL;
        return HAL_ERR;
    }
    g_open = true;
    return HAL_OK;
}

uint32_t hal_audio_underruns(void) { return g_underruns; }

void hal_audio_idle(void) { vTaskDelay(1); } /* the decoder sleeps a tick; the block clock task preempts it when a block is due */

const char *hal_audio_backend_name(void) { return "null"; }

int32_t hal_audio_write(const int32_t *samples, uint32_t frames) {
    (void)samples; /* discarded */
    return (int32_t)frames;
}

uint32_t hal_audio_free_frames(void) { return HAL_AUDIO_BLOCK_FRAMES; }

void hal_audio_mute(bool mute) { (void)mute; }

void hal_audio_force_mute(void) {}

bool hal_audio_wired_attached(void) { return true; } /* no jack detection */
bool hal_audio_headphones_in(void) { return false; }
bool hal_audio_speaker_available(void) { return false; }
void hal_audio_set_speaker(bool allow) { (void)allow; }
bool hal_audio_line_out_available(void) { return false; }
void hal_audio_set_line_out(bool allow) { (void)allow; }

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
    g_stop = true;
    gptimer_stop(g_timer);
    gptimer_disable(g_timer);
    gptimer_del_timer(g_timer);
    g_timer = NULL;
}

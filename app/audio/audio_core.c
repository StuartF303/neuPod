// SPDX-License-Identifier: MPL-2.0
/*
 * The bare-metal audio core (core 1). docs/adr/0011-bare-metal-audio-core.md.
 *
 * CORE-1 RULES for this file, spsc_ring.c, audio_shared.c and platforms/rp2350/hal/hal_audio.c
 * (checked by cmake/check_core1_rules.cmake, run as the ctest "audio_core_rules"):
 *   - no FreeRTOS / os.h call, no stdio, no malloc, no SDK primitive the RTOS interop reroutes
 *     (mutex, semaphore, sleep_*, busy_wait_*, critical_section, queue);
 *   - talk to core 0 only through the lock-free rings and the seqlock in audio_shared.h.
 *
 * Structure: the main loop handles commands and runs the decoder stub (fills the PCM ring); the
 * sink's block-clock INTERRUPT (on_block) serves each 256-frame block from the PCM ring and keeps the
 * statistics. The interrupt preempts the decoder, so block timing does not depend on how long the
 * decoder takes, which is the point of this design.
 */
#include <string.h>

#include "audio_shared.h"
#include "hal_audio.h"
#include "hal_system.h"
#include "ram_func.h"

#define PCM_FRAMES 2048u // 8 blocks = 46 ms between decoder and sink
#define FRAME_BYTES (2u * sizeof(int32_t)) // stereo, signed 32-bit (ADR-0003)

// Decoder cost model: percent of one block period the stub decoder busy-spends per block, so the
// interrupt-over-decoder arrangement is exercised. Real codecs are in this range on a 125 MHz M33.
// Override with -DNEUPOD_DECODER_LOAD_PCT=n (0 = idle decoder).
#ifndef NEUPOD_DECODER_LOAD_PCT
#define NEUPOD_DECODER_LOAD_PCT 25u
#endif

static int32_t g_pcm_store[PCM_FRAMES * 2u];
static spsc_ring_t g_pcm_ring; // loop produces, block interrupt consumes
static int32_t g_block[HAL_AUDIO_BLOCK_FRAMES * 2u];

// Written by the main loop, read by the interrupt (single writer each, plain 32-bit): no atomics.
static volatile uint32_t g_window_reset_req;
static volatile uint32_t g_decoded_blocks;
static volatile uint32_t g_data_bytes;
static volatile uint32_t g_evt_dropped_isr;  // one counter per context: no shared read-modify-write
static volatile uint32_t g_evt_dropped_loop;

// ---- Interrupt context: serve one block, keep statistics -----------------------------------------

static audio_stats_t g_isr_stats; // owned by the interrupt
static uint64_t g_period_sum;
static uint64_t g_last_us;
static bool g_have_last;
static uint32_t g_reset_seen;
static uint32_t g_last_underrun_evt_block;

static void NEUPOD_RAM_FUNC(push_event_isr)(uint32_t type, uint32_t value) {
    audio_event_t e = {.type = type, .value = value};
    if (spsc_ring_write(&g_audio_evt_isr_ring, &e, 1) != 1) {
        g_evt_dropped_isr++;
    }
}

static void NEUPOD_RAM_FUNC(on_block)(uint32_t missed) {
    (void)missed; // the HAL also counts these; sink_missed comes from hal_audio_underruns()
    const uint64_t now = hal_micros();
    const uint32_t fill = spsc_ring_available(&g_pcm_ring);

    // Serve the block first: the bookkeeping below must never delay the sink.
    uint32_t got = spsc_ring_read(&g_pcm_ring, g_block, HAL_AUDIO_BLOCK_FRAMES);
    bool starved = got < HAL_AUDIO_BLOCK_FRAMES;
    if (starved) {
        memset(g_block + got * 2u, 0, (HAL_AUDIO_BLOCK_FRAMES - got) * FRAME_BYTES);
        g_isr_stats.underruns++;
    }
    (void)hal_audio_write(g_block, HAL_AUDIO_BLOCK_FRAMES);

    audio_stats_t *s = &g_isr_stats;
    s->blocks++;
    if (g_reset_seen != g_window_reset_req) {
        g_reset_seen = g_window_reset_req;
        s->period_min_us = s->period_avg_us = s->period_max_us = 0;
        s->jitter_max_us = 0;
        s->ring_min_frames = fill;
        s->window_blocks = 0;
        g_period_sum = 0;
        g_have_last = false;
    }
    if (fill < s->ring_min_frames) {
        s->ring_min_frames = fill;
    }
    if (g_have_last) {
        uint32_t period = (uint32_t)(now - g_last_us);
        uint32_t nominal = s->period_nominal_us;
        uint32_t dev = period > nominal ? period - nominal : nominal - period;
        g_period_sum += period;
        s->window_blocks++;
        if (s->period_min_us == 0 || period < s->period_min_us) {
            s->period_min_us = period;
        }
        if (period > s->period_max_us) {
            s->period_max_us = period;
        }
        if (dev > s->jitter_max_us) {
            s->jitter_max_us = dev;
        }
        s->period_avg_us = (uint32_t)(g_period_sum / s->window_blocks);
    }
    g_last_us = now;
    g_have_last = true;
    s->sink_missed = hal_audio_underruns();
    s->decoded_blocks = g_decoded_blocks;
    s->data_bytes = g_data_bytes;
    s->events_dropped = g_evt_dropped_isr + g_evt_dropped_loop;

    if (starved && s->blocks - g_last_underrun_evt_block >= 172u) { // at most about one per second
        g_last_underrun_evt_block = s->blocks;
        push_event_isr(AUDIO_EVT_UNDERRUN, s->underruns);
    }

    seqlock_write_begin(&g_audio_stats_lock);
    g_audio_stats = *s;
    seqlock_write_end(&g_audio_stats_lock);
}

// ---- Main loop: commands and the decoder stub ----------------------------------------------------

static void push_event_loop(uint32_t type, uint32_t value) {
    audio_event_t e = {.type = type, .value = value};
    if (spsc_ring_write(&g_audio_evt_loop_ring, &e, 1) != 1) {
        g_evt_dropped_loop++;
    }
}

static void burn_us(uint32_t us) {
    uint64_t end = hal_micros() + us;
    while (hal_micros() < end) {
    }
}

static bool g_stream_open;

static void handle_command(const audio_cmd_t *c) {
    switch (c->type) {
    case AUDIO_CMD_OPEN: {
        if (g_stream_open) {
            push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
            break;
        }
        hal_audio_format fmt = {.sample_rate = c->a,
                                .channels = (uint8_t)(c->b & 0xFFu),
                                .valid_bits = (uint8_t)((c->b >> 8) & 0xFFu)};
        // Fresh stream: empty PCM ring, fresh counters; the interrupt owns g_isr_stats, but it is
        // not running yet (the clock starts inside hal_audio_open).
        while (spsc_ring_read(&g_pcm_ring, g_block, HAL_AUDIO_BLOCK_FRAMES) != 0) {
        } // drop stale PCM (the clock is stopped, so we are the only reader)
        memset(&g_isr_stats, 0, sizeof g_isr_stats);
        g_isr_stats.period_nominal_us =
            (uint32_t)((uint64_t)HAL_AUDIO_BLOCK_FRAMES * 1000000u / (fmt.sample_rate ? fmt.sample_rate : 1u));
        g_isr_stats.ring_frames = PCM_FRAMES;
        g_isr_stats.ring_min_frames = PCM_FRAMES;
        g_have_last = false;
        g_period_sum = 0;
        g_last_underrun_evt_block = 0;
        if (hal_audio_open(&fmt) == HAL_OK) {
            g_stream_open = true;
            push_event_loop(AUDIO_EVT_STREAM_OPENED, fmt.sample_rate);
        } else {
            push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
        }
        break;
    }
    case AUDIO_CMD_CLOSE:
        if (g_stream_open) {
            hal_audio_close();
            g_stream_open = false;
            push_event_loop(AUDIO_EVT_STREAM_CLOSED, 0);
        }
        break;
    case AUDIO_CMD_NEW_WINDOW:
        g_window_reset_req++;
        break;
    default:
        push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
        break;
    }
}

void audio_core_main(void) {
    const uint32_t decode_cost_us = (uint32_t)((uint64_t)HAL_AUDIO_BLOCK_FRAMES * 1000000u /
                                               AUDIO_SAMPLE_RATE * NEUPOD_DECODER_LOAD_PCT / 100u);
    (void)spsc_ring_init(&g_pcm_ring, g_pcm_store, FRAME_BYTES, PCM_FRAMES);
    (void)hal_audio_init();
    hal_audio_set_block_callback(on_block);

    for (;;) {
        bool worked = false;

        audio_cmd_t cmd;
        while (spsc_ring_read(&g_audio_cmd_ring, &cmd, 1) == 1) {
            handle_command(&cmd);
            worked = true;
        }

        // Compressed data ring: the real decoder will read it; the stub just drains it.
        uint8_t sink[64];
        uint32_t n = spsc_ring_read(&g_audio_data_ring, sink, sizeof sink);
        if (n != 0) {
            g_data_bytes += n;
            worked = true;
        }

        // Decoder stub: one block of silence per pass while the PCM ring has room.
        if (g_stream_open && spsc_ring_space(&g_pcm_ring) >= HAL_AUDIO_BLOCK_FRAMES) {
            burn_us(decode_cost_us); // pretend to decode; the block interrupt preempts this
            (void)spsc_ring_write(&g_pcm_ring, NULL, HAL_AUDIO_BLOCK_FRAMES);
            g_decoded_blocks++;
            worked = true;
        }

        if (!worked) {
            hal_audio_idle(); // sleep until the next interrupt (block clock)
        }
    }
}

// SPDX-License-Identifier: MPL-2.0
/*
 * The bare-metal audio core (core 1). docs/adr/0011-bare-metal-audio-core.md, docs/audio.md.
 *
 * CORE-1 RULES for this file, spsc_ring.c, audio_shared.c, audio_dsp.c and platforms/rp2350/hal/hal_audio.c
 * (checked by cmake/check_core1_rules.cmake, run as the ctest "audio_core_rules"):
 *   - no FreeRTOS / os.h call, no stdio, no malloc, no SDK primitive the RTOS interop reroutes
 *     (mutex, semaphore, sleep_*, busy_wait_*, critical_section, queue);
 *   - talk to core 0 only through the lock-free rings, the seqlock and the counters in audio_shared.h.
 *
 * Structure: the main loop handles commands and "decodes" (a test signal, or WAV bytes converted to 32-bit)
 * into the PCM ring; the sink's block INTERRUPT (on_block) takes one 256-frame block from the ring, applies
 * the volume ramp, hands it to the sink and keeps the statistics and the mute line. The interrupt preempts
 * the decoder, so block timing does not depend on how long the decoder takes.
 *
 * Sample path: source -> int32 stereo (ADR-0003, exact conversion) -> PCM ring -> [volume: skipped at 0 dB]
 * -> sink. At 0 dB and in steady state the samples reach the DAC bit-exact.
 */
#include <string.h>

#include "audio_dsp.h"
#include "audio_shared.h"
#include "hal_audio.h"
#include "hal_system.h"
#include "ram_func.h"

#define PCM_FRAMES 2048u // 8 blocks = 46 ms between decoder and sink
#define FRAME_BYTES (2u * sizeof(int32_t)) // stereo, signed 32-bit (ADR-0003)
#define PCM_CHUNK_FRAMES 128u              // frames converted per decoder step
#define STORM_BLOCKS 32u                   // consecutive starved blocks (186 ms) before playback is stopped and muted
#define FLUSH_TIMEOUT_US 100000u
#define RAMP_TIMEOUT_US 50000u

// Optional stress knob: percent of a block period the decoder busy-spends per block (0 = off), to prove the
// interrupt-over-decoder arrangement under load. -DNEUPOD_DECODER_LOAD_PCT=25 reproduces the old stub's cost.
#ifndef NEUPOD_DECODER_LOAD_PCT
#define NEUPOD_DECODER_LOAD_PCT 0u
#endif

static int32_t g_pcm_store[PCM_FRAMES * 2u];
static spsc_ring_t g_pcm_ring; // loop produces, block interrupt consumes
static int32_t g_block[HAL_AUDIO_BLOCK_FRAMES * 2u];
static int32_t g_gen[HAL_AUDIO_BLOCK_FRAMES * 2u];       // decoder output scratch (loop)
static uint8_t g_in[PCM_CHUNK_FRAMES * 8u];              // raw PCM bytes (loop)

// Loop -> interrupt (single writer each, plain 32-bit: no atomics needed).
static volatile uint32_t g_window_reset_req;
static volatile uint32_t g_decoded_blocks;
static volatile uint32_t g_data_bytes;
static volatile uint32_t g_evt_dropped_isr; // one counter per context: no shared read-modify-write
static volatile uint32_t g_evt_dropped_loop;
static volatile uint32_t g_audible_req;     // 1 = a source is selected and not paused
static volatile uint32_t g_vol_gain = 0;    // Q30, set from g_vol_att at start
static volatile uint32_t g_vol_att = AUDIO_VOL_DEFAULT_ATT_DB;
static volatile uint32_t g_src;             // audio_source_t
static volatile uint32_t g_eof;             // PCM: no more data will arrive
static volatile uint32_t g_flush_req;
static volatile uint32_t g_sink_rate_hz, g_sink_rate_mhz, g_sink_clkdiv;
// Interrupt -> loop.
static volatile uint32_t g_flush_ack;
static volatile uint32_t g_cur_gain;        // gain applied to the last block (0 = silent)
static volatile uint32_t g_blocks_seen;
static volatile uint32_t g_storm;           // latched by the interrupt; the loop stops the source and clears it

// ---- Interrupt context: serve one block, keep statistics -----------------------------------------

static audio_stats_t g_isr_stats; // owned by the interrupt
static uint64_t g_period_sum;
static uint64_t g_last_us;
static bool g_have_last;
static uint32_t g_reset_seen;
static uint32_t g_flush_seen;
static uint32_t g_last_underrun_evt_block;
static uint32_t g_starved_run;
static uint32_t g_quiet_blocks;
static bool g_mute_state = true;

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
    audio_stats_t *s = &g_isr_stats;

    if (g_flush_req != g_flush_seen) { // the loop wants the ring emptied (new source, stop)
        (void)spsc_ring_discard(&g_pcm_ring, PCM_FRAMES);
        s->pcm_frames = 0;
        g_starved_run = 0;
        g_flush_seen = g_flush_req;
        g_flush_ack = g_flush_seen;
    }

    // Serve the block first: the bookkeeping below must never delay the sink.
    const bool audible = g_audible_req != 0 && g_storm == 0;
    const uint32_t from = g_cur_gain;
    const uint32_t target = audible ? g_vol_gain : 0u;
    const bool serve_ring = audible || from != 0u; // a paused, silent stream does not consume the ring
    bool starved = false;
    if (serve_ring) {
        uint32_t got = spsc_ring_read(&g_pcm_ring, g_block, HAL_AUDIO_BLOCK_FRAMES);
        starved = got < HAL_AUDIO_BLOCK_FRAMES;
        if (starved) {
            memset(g_block + got * 2u, 0, (HAL_AUDIO_BLOCK_FRAMES - got) * FRAME_BYTES);
            if (g_eof == 0) { // running dry at the end of a file is not an underrun
                s->underruns++;
            }
        }
        if (g_src == AUDIO_SRC_PCM) {
            s->pcm_frames += got;
        }
        if (fill < s->ring_min_frames) {
            s->ring_min_frames = fill;
        }
        audio_apply_gain(g_block, HAL_AUDIO_BLOCK_FRAMES, from, target);
    } else {
        memset(g_block, 0, sizeof g_block);
    }
    g_cur_gain = target;
    (void)hal_audio_write(g_block, HAL_AUDIO_BLOCK_FRAMES);

    // Amp mute line: open it just before audio ramps in, close it once two silent blocks have passed.
    if (target != 0u || from != 0u) {
        g_quiet_blocks = 0;
        if (g_mute_state) {
            hal_audio_mute(false);
            g_mute_state = false;
        }
    } else if (++g_quiet_blocks >= 2u && !g_mute_state) {
        hal_audio_mute(true);
        g_mute_state = true;
    }

    // Underrun storm: stop feeding the DAC garbage; the loop tidies up.
    if (starved && audible && g_eof == 0) {
        if (++g_starved_run >= STORM_BLOCKS && g_storm == 0) {
            g_storm = 1;
            push_event_isr(AUDIO_EVT_STORM, g_starved_run);
        }
    } else if (!starved) {
        g_starved_run = 0;
    }

    s->blocks++;
    g_blocks_seen = s->blocks;
    if (g_reset_seen != g_window_reset_req) {
        g_reset_seen = g_window_reset_req;
        s->period_min_us = s->period_avg_us = s->period_max_us = 0;
        s->jitter_max_us = 0;
        s->ring_min_frames = fill;
        s->window_blocks = 0;
        g_period_sum = 0;
        g_have_last = false;
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
    s->source = g_src;
    s->audible = audible ? 1u : 0u;
    s->volume_att_db = g_vol_att;
    s->muted = g_mute_state ? 1u : 0u;
    s->sink_rate_hz = g_sink_rate_hz;
    s->sink_rate_mhz = g_sink_rate_mhz;
    s->sink_clkdiv_x256 = g_sink_clkdiv;

    if (starved && g_eof == 0 && s->blocks - g_last_underrun_evt_block >= 172u) { // at most about one per second
        g_last_underrun_evt_block = s->blocks;
        push_event_isr(AUDIO_EVT_UNDERRUN, s->underruns);
    }

    seqlock_write_begin(&g_audio_stats_lock);
    g_audio_stats = *s;
    seqlock_write_end(&g_audio_stats_lock);
}

// ---- Main loop: commands and the decoder ----------------------------------------------------------

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
static uint32_t g_rate;        // sink rate while open
static bool g_play;            // transport: playing (true) or paused (false)
static tonegen_t g_tg;
static uint8_t g_pcm_ch, g_pcm_bits;
static bool g_draining;
static uint32_t g_drain_mark;

static void update_audible(void) { g_audible_req = (g_src != AUDIO_SRC_NONE && g_play) ? 1u : 0u; }

// Wait (bounded) for the interrupt to ramp the output to silence. Only meaningful while the clock runs.
static void wait_silent(void) {
    if (!g_stream_open) {
        return;
    }
    uint64_t end = hal_micros() + RAMP_TIMEOUT_US;
    while (g_cur_gain != 0u && hal_micros() < end) {
        hal_audio_idle(); // wakes on the next block interrupt
    }
}

// Empty the PCM ring (the interrupt owns its read side) and the data ring (we own that one).
static void flush_rings(void) {
    if (g_stream_open) {
        uint32_t want = g_flush_req + 1u;
        g_flush_req = want;
        uint64_t end = hal_micros() + FLUSH_TIMEOUT_US;
        while (g_flush_ack != want && hal_micros() < end) {
            hal_audio_idle();
        }
    } else {
        (void)spsc_ring_discard(&g_pcm_ring, PCM_FRAMES); // clock stopped: nobody else is reading
    }
    (void)spsc_ring_discard(&g_audio_data_ring, 0xFFFFFFFFu);
}

static void stop_source(void) {
    g_play = false;
    update_audible();
    wait_silent();
    flush_rings();
    g_src = AUDIO_SRC_NONE;
    g_eof = 0;
    g_draining = false;
    update_audible();
}

static void set_nominal(uint32_t rate) {
    g_isr_stats.period_nominal_us = (uint32_t)((uint64_t)HAL_AUDIO_BLOCK_FRAMES * 1000000u / (rate ? rate : 1u));
}

static bool open_sink(uint32_t rate, uint8_t channels, uint8_t bits) {
    hal_audio_format fmt = {.sample_rate = rate, .channels = channels, .valid_bits = bits};
    set_nominal(rate);
    g_have_last = false;
    g_period_sum = 0;
    g_isr_stats.window_blocks = 0; // the period window restarts with the new clock
    g_isr_stats.period_min_us = g_isr_stats.period_avg_us = g_isr_stats.period_max_us = 0;
    g_isr_stats.jitter_max_us = 0;
    if (hal_audio_open(&fmt) != HAL_OK) {
        return false;
    }
    hal_audio_info_t info;
    hal_audio_get_info(&info);
    g_rate = rate;
    g_sink_rate_hz = rate;
    g_sink_rate_mhz = info.rate_actual_mhz;
    g_sink_clkdiv = info.clkdiv_x256;
    g_stream_open = true;
    return true;
}

static void close_sink(void) {
    if (g_stream_open) {
        hal_audio_close();
        g_stream_open = false;
        g_sink_rate_hz = g_sink_rate_mhz = g_sink_clkdiv = 0;
        g_cur_gain = 0;
        g_mute_state = true;
    }
}

static void handle_command(const audio_cmd_t *c) {
    switch (c->type) {
    case AUDIO_CMD_OPEN: {
        if (g_stream_open) {
            push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
            break;
        }
        // Fresh stream: empty rings, fresh counters; the interrupt owns g_isr_stats but is not running yet.
        (void)spsc_ring_discard(&g_pcm_ring, PCM_FRAMES);
        memset(&g_isr_stats, 0, sizeof g_isr_stats);
        g_isr_stats.ring_frames = PCM_FRAMES;
        g_isr_stats.ring_min_frames = PCM_FRAMES;
        g_last_underrun_evt_block = 0;
        g_starved_run = 0;
        g_quiet_blocks = 0;
        g_storm = 0;
        g_cur_gain = 0;
        g_src = AUDIO_SRC_NONE;
        g_play = false;
        update_audible();
        if (open_sink(c->a, (uint8_t)(c->b & 0xFFu), (uint8_t)((c->b >> 8) & 0xFFu))) {
            push_event_loop(AUDIO_EVT_STREAM_OPENED, c->a);
        } else {
            push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
        }
        break;
    }
    case AUDIO_CMD_CLOSE:
        if (g_stream_open) {
            g_play = false;
            g_src = AUDIO_SRC_NONE;
            update_audible();
            close_sink();
            push_event_loop(AUDIO_EVT_STREAM_CLOSED, 0);
        }
        break;
    case AUDIO_CMD_NEW_WINDOW:
        g_window_reset_req++;
        break;
    case AUDIO_CMD_VOLUME:
        g_vol_att = audio_vol_clamp(c->a);
        g_vol_gain = audio_gain_q30(g_vol_att);
        break;
    case AUDIO_CMD_TONE:
        if (!g_stream_open) {
            push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
            break;
        }
        stop_source();
        if (c->a != TONE_OFF) {
            tonegen_init(&g_tg, (tone_mode_t)c->a, g_rate);
            g_src = AUDIO_SRC_TONE;
            g_play = true;
            update_audible();
        }
        break;
    case AUDIO_CMD_PCM_START: {
        const uint32_t rate = c->a;
        const uint8_t ch = (uint8_t)(c->b & 0xFFu), bits = (uint8_t)((c->b >> 8) & 0xFFu);
        if (!g_stream_open || !hal_audio_rate_supported(rate) || (bits != 16 && bits != 24 && bits != 32) ||
            ch < 1 || ch > 2) {
            push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
            g_audio_pcm_started++; // the requester stops waiting; the error event says why
            break;
        }
        stop_source();
        if (rate != g_rate) { // re-clock the sink: no resampling (ADR-0003)
            close_sink();
            (void)spsc_ring_discard(&g_pcm_ring, PCM_FRAMES);
            g_cur_gain = 0;
            if (!open_sink(rate, 2, bits)) {
                push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
                g_audio_pcm_started++;
                break;
            }
        }
        g_pcm_ch = ch;
        g_pcm_bits = bits;
        g_eof = 0;
        g_src = AUDIO_SRC_PCM;
        g_play = true;
        update_audible();
        g_audio_pcm_started++; // after this core 0 may write the first bytes of the stream
        break;
    }
    case AUDIO_CMD_PCM_END:
        if (g_src == AUDIO_SRC_PCM) {
            g_eof = 1;
        }
        break;
    case AUDIO_CMD_PAUSE:
        g_play = c->a == 0;
        update_audible();
        break;
    case AUDIO_CMD_STOP:
        stop_source();
        break;
    default:
        push_event_loop(AUDIO_EVT_CMD_ERROR, c->type);
        break;
    }
}

// Produce PCM for the active source while the ring has room.
static bool pump_source(void) {
    bool worked = false;
    if (!g_stream_open) {
        return false;
    }
    if (g_src == AUDIO_SRC_TONE) {
        if (spsc_ring_space(&g_pcm_ring) >= HAL_AUDIO_BLOCK_FRAMES) {
            if (NEUPOD_DECODER_LOAD_PCT != 0u) {
                burn_us((uint32_t)((uint64_t)HAL_AUDIO_BLOCK_FRAMES * 1000000u / g_rate * NEUPOD_DECODER_LOAD_PCT / 100u));
            }
            tonegen_fill(&g_tg, g_gen, HAL_AUDIO_BLOCK_FRAMES);
            (void)spsc_ring_write(&g_pcm_ring, g_gen, HAL_AUDIO_BLOCK_FRAMES);
            g_decoded_blocks++;
            worked = true;
        }
    } else if (g_src == AUDIO_SRC_PCM) {
        const uint32_t fs = (uint32_t)g_pcm_ch * g_pcm_bits / 8u;
        for (int i = 0; i < 4; i++) {
            uint32_t space = spsc_ring_space(&g_pcm_ring);
            uint32_t frames = spsc_ring_available(&g_audio_data_ring) / fs;
            if (frames > space) {
                frames = space;
            }
            if (frames > PCM_CHUNK_FRAMES) {
                frames = PCM_CHUNK_FRAMES;
            }
            if (frames == 0) {
                break;
            }
            uint32_t n = spsc_ring_read(&g_audio_data_ring, g_in, frames * fs);
            g_data_bytes += n;
            (void)audio_pcm_to_s32(g_gen, g_in, n / fs, g_pcm_bits, g_pcm_ch);
            (void)spsc_ring_write(&g_pcm_ring, g_gen, n / fs);
            g_decoded_blocks = g_decoded_blocks + (n / fs >= PCM_CHUNK_FRAMES / 2u ? 1u : 0u);
            worked = true;
        }
        // End of stream: all data consumed and played out (ring empty, then 3 blocks for the sink's buffers).
        if (g_eof != 0 && spsc_ring_available(&g_audio_data_ring) < fs && spsc_ring_available(&g_pcm_ring) == 0) {
            if (!g_draining) {
                g_draining = true;
                g_drain_mark = g_blocks_seen;
            } else if (g_blocks_seen - g_drain_mark >= 3u) {
                stop_source();
                g_audio_pcm_ended++;
                push_event_loop(AUDIO_EVT_END_OF_TRACK, 0);
                worked = true;
            }
        }
    }
    return worked;
}

void audio_core_main(void) {
    (void)spsc_ring_init(&g_pcm_ring, g_pcm_store, FRAME_BYTES, PCM_FRAMES);
    (void)hal_audio_init();
    hal_audio_set_block_callback(on_block);
    g_vol_gain = audio_gain_q30(g_vol_att);

    for (;;) {
        bool worked = false;

        audio_cmd_t cmd;
        while (spsc_ring_read(&g_audio_cmd_ring, &cmd, 1) == 1) {
            handle_command(&cmd);
            worked = true;
        }

        if (g_storm != 0) { // the interrupt gave up on a starved stream: stop it, then re-arm
            stop_source();
            g_storm = 0;
            worked = true;
        }

        if (pump_source()) {
            worked = true;
        }

        if (!worked) {
            hal_audio_idle(); // sleep until the next interrupt (block clock)
        }
    }
}

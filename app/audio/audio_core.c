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
#include "codec.h"
#include "stream_io.h"
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
static volatile uint32_t g_priming;         // 1 = a stream was just started: stay silent until the PCM ring holds some audio
static volatile uint32_t g_isr_busy_us;     // total time spent in the block interrupt (wraps), for decoder CPU accounting
static volatile uint32_t g_codec_kind;      // audio_codec_t of the current stream
static volatile uint32_t g_codec_err;       // codec_err_t of the last failure
static volatile uint32_t g_verify;          // audio_verify_t
static volatile uint32_t g_dec_avg_us, g_dec_max_us, g_dec_pct_x10; // decoder load window, published by the loop

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
    const bool audible = g_audible_req != 0 && g_storm == 0 && g_priming == 0;
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
    s->codec = g_codec_kind;
    s->codec_error = g_codec_err;
    s->verify_state = g_verify;
    s->decoded_frames = g_audio_decoded_frames;
    s->dec_us_block_avg = g_dec_avg_us;
    s->dec_us_block_max = g_dec_max_us;
    s->dec_pct_x10 = g_dec_pct_x10;

    if (starved && g_eof == 0 && s->blocks - g_last_underrun_evt_block >= 172u) { // at most about one per second
        g_last_underrun_evt_block = s->blocks;
        push_event_isr(AUDIO_EVT_UNDERRUN, s->underruns);
    }

    seqlock_write_begin(&g_audio_stats_lock);
    g_audio_stats = *s;
    seqlock_write_end(&g_audio_stats_lock);
    g_isr_busy_us += (uint32_t)(hal_micros() - now);
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

// Codec stream state (loop only)
static codec_t g_codec;
static bool g_codec_pending;   // stream announced, decoder not opened yet (it blocks on data core 0 sends after the ack)
static bool g_codec_active;
static bool g_decoder_done;    // the decoder has produced its last frame
static audio_stream_desc_t g_desc;
static sio_t g_sio;
static audio_cmd_t g_deferred; // a disruptive command that arrived while the decoder was waiting for data
static bool g_have_deferred;
static uint32_t g_dec_sum_us, g_dec_frames, g_dec_max_per256, g_dec_reset_seen;

#define PRIME_FRAMES 1024u // about 23 ms of audio buffered before the output starts

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
    g_codec_pending = false;
    if (g_codec_active) {
        codec_close(&g_codec);
        g_codec_active = false;
    }
    g_decoder_done = false;
    g_priming = 0;
    g_codec_kind = AUDIO_CODEC_PCM;
    if (g_verify == AUDIO_VERIFY_RUNNING) {
        g_verify = AUDIO_VERIFY_NONE; // abandoned
    }
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

static void handle_command(const audio_cmd_t *c);

// Called by the stream reader while it waits for data: keep the engine responsive. Harmless commands are done on
// the spot; anything that would tear the stream down is parked and the read is abandoned (returns true).
static bool poll_while_waiting(void *ctx) {
    (void)ctx;
    if (g_have_deferred) {
        return true; // a parked command is still waiting for the stream to be unwound
    }
    audio_cmd_t cmd;
    while (spsc_ring_read(&g_audio_cmd_ring, &cmd, 1) == 1) {
        switch (cmd.type) {
        case AUDIO_CMD_VOLUME:
        case AUDIO_CMD_NEW_WINDOW:
        case AUDIO_CMD_PAUSE:
        case AUDIO_CMD_PCM_END:
            handle_command(&cmd);
            break;
        default:
            g_deferred = cmd;
            g_have_deferred = true;
            return true;
        }
    }
    return g_storm != 0;
}

static void fail_stream(uint32_t err) {
    g_codec_err = err;
    stop_source();
    push_event_loop(AUDIO_EVT_CMD_ERROR, 100u + err);
}

static void start_stream(const audio_stream_desc_t *d) {
    const bool codec = d->codec != AUDIO_CODEC_PCM;
    if (!g_stream_open || !hal_audio_rate_supported(d->rate) || d->channels < 1 || d->channels > 2 ||
        (!codec && d->bits != 16 && d->bits != 24 && d->bits != 32)) {
        push_event_loop(AUDIO_EVT_CMD_ERROR, AUDIO_CMD_PCM_START);
        g_audio_pcm_started++; // the requester stops waiting; the error event says why
        return;
    }
    stop_source();
    if (d->rate != g_rate) { // re-clock the sink: no resampling (ADR-0003)
        close_sink();
        (void)spsc_ring_discard(&g_pcm_ring, PCM_FRAMES);
        g_cur_gain = 0;
        if (!open_sink(d->rate, 2, (uint8_t)d->bits)) {
            push_event_loop(AUDIO_EVT_CMD_ERROR, AUDIO_CMD_PCM_START);
            g_audio_pcm_started++;
            return;
        }
    }
    g_desc = *d;
    g_pcm_ch = (uint8_t)d->channels;
    g_pcm_bits = (uint8_t)d->bits;
    g_eof = 0;
    g_decoder_done = false;
    g_codec_err = 0;
    g_audio_decoded_frames = 0;
    g_verify = AUDIO_VERIFY_NONE;
    g_codec_kind = d->codec;
    g_codec_pending = codec;
    g_priming = 1;
    g_src = AUDIO_SRC_PCM;
    g_play = !(codec && d->verify);
    update_audible();
    if (codec) {
        g_sio = (sio_t){.ring = &g_audio_data_ring, .eof = &g_eof, .poll = poll_while_waiting, .idle = hal_audio_idle,
                        .seek_req = &g_audio_seek_req, .seek_off = &g_audio_seek_off, .seek_ack = &g_audio_seek_ack,
                        .seek_done = &g_audio_seek_done, .seek_seq = g_audio_seek_req};
        if (d->verify) {
            g_verify = AUDIO_VERIFY_RUNNING;
        }
    }
    g_audio_pcm_started++; // after this core 0 may write the first bytes of the stream
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
            stop_source();
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
            g_priming = 1;
            g_play = true;
            update_audible();
        }
        break;
    case AUDIO_CMD_PCM_START: {
        audio_stream_desc_t d;
        memset(&d, 0, sizeof d);
        d.codec = AUDIO_CODEC_PCM;
        d.rate = c->a;
        d.channels = c->b & 0xFFu;
        d.bits = (c->b >> 8) & 0xFFu;
        start_stream(&d);
        break;
    }
    case AUDIO_CMD_STREAM_START: {
        audio_stream_desc_t d = g_audio_stream_desc;
        start_stream(&d);
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

static void note_decode_time(uint32_t wall_us, uint32_t isr_us, uint32_t frames) {
    if (frames == 0) {
        return;
    }
    uint32_t us = wall_us > isr_us ? wall_us - isr_us : 0u; // the block interrupt preempted us for isr_us of it
    g_dec_sum_us += us;
    g_dec_frames += frames;
    uint32_t per256 = (uint32_t)((uint64_t)us * HAL_AUDIO_BLOCK_FRAMES / frames);
    if (per256 > g_dec_max_per256) {
        g_dec_max_per256 = per256;
    }
    if (g_dec_frames >= 4u * HAL_AUDIO_BLOCK_FRAMES) {
        g_dec_avg_us = (uint32_t)((uint64_t)g_dec_sum_us * HAL_AUDIO_BLOCK_FRAMES / g_dec_frames);
        g_dec_max_us = g_dec_max_per256;
        uint32_t nominal = g_isr_stats.period_nominal_us;
        g_dec_pct_x10 = nominal != 0 ? (uint32_t)((uint64_t)g_dec_avg_us * 1000u / nominal) : 0u;
    }
}

static void check_dec_window(void) {
    if (g_dec_reset_seen != g_window_reset_req) {
        g_dec_reset_seen = g_window_reset_req;
        g_dec_sum_us = g_dec_frames = g_dec_max_per256 = 0;
    }
}

// Decode a little with the codec. Returns true if it did something.
static bool pump_codec(void) {
    if (g_codec_pending) {
        codec_params_t p;
        memset(&p, 0, sizeof p);
        p.kind = g_desc.codec == AUDIO_CODEC_FLAC ? CODEC_FLAC : g_desc.codec == AUDIO_CODEC_MP3 ? CODEC_MP3 : CODEC_ALAC;
        p.rate = g_desc.rate;
        p.channels = g_desc.channels;
        p.bits = g_desc.bits;
        p.verify = g_desc.verify != 0;
        p.total_frames = g_desc.total_frames;
        p.cookie_len = g_desc.cookie_len > sizeof p.cookie ? (uint32_t)sizeof p.cookie : g_desc.cookie_len;
        memcpy(p.cookie, g_desc.cookie, p.cookie_len);
        codec_io_t io = {.read = sio_read, .seek = sio_seek, .aborted = sio_aborted, .ctx = &g_sio};
        g_codec_pending = false;
        codec_err_t e = codec_open(&g_codec, &p, &io);
        if (g_sio.aborted) { // a command interrupted the open; it runs next
            g_sio.aborted = false;
            return true;
        }
        if (e != CODEC_OK) {
            fail_stream((uint32_t)e);
            return true;
        }
        g_codec_active = true;
        return true;
    }
    if (!g_codec_active || g_decoder_done) {
        return false;
    }
    const bool verify = g_verify == AUDIO_VERIFY_RUNNING;
    if (!verify && spsc_ring_space(&g_pcm_ring) < HAL_AUDIO_BLOCK_FRAMES) {
        return false;
    }
    bool worked = false;
    for (int i = 0; i < 2; i++) { // two blocks per pass at most; commands get looked at in between
        if (!verify && spsc_ring_space(&g_pcm_ring) < HAL_AUDIO_BLOCK_FRAMES) {
            break;
        }
        const uint32_t t0 = (uint32_t)hal_micros(), isr0 = g_isr_busy_us;
        int32_t n = codec_read(&g_codec, g_gen, HAL_AUDIO_BLOCK_FRAMES);
        const uint32_t wall = (uint32_t)hal_micros() - t0, isr = g_isr_busy_us - isr0;
        if (g_sio.aborted) { // the wait for data was cut short by a command; run it
            g_sio.aborted = false;
            return true;
        }
        if (n < 0) {
            fail_stream((uint32_t)-n);
            return true;
        }
        if (n == 0) { // end of the stream
            g_decoder_done = true;
            return true;
        }
        note_decode_time(wall, isr, (uint32_t)n);
        g_audio_decoded_frames += (uint32_t)n;
        if (!verify) {
            (void)spsc_ring_write(&g_pcm_ring, g_gen, (uint32_t)n);
        }
        g_decoded_blocks++;
        worked = true;
    }
    return worked;
}

// Finish a verify run: compare the MD5 and report through the end-of-stream handshake.
static void finish_verify(void) {
    uint8_t md5[16];
    codec_md5_final(&g_codec, md5);
    uint32_t any = 0;
    for (int i = 0; i < 16; i++) {
        any |= g_desc.md5[i];
    }
    g_verify = any == 0 ? AUDIO_VERIFY_NO_MD5 : memcmp(md5, g_desc.md5, 16) == 0 ? AUDIO_VERIFY_OK : AUDIO_VERIFY_MISMATCH;
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
    } else if (g_src == AUDIO_SRC_PCM && g_desc.codec != AUDIO_CODEC_PCM) {
        worked = pump_codec();
        if (g_src != AUDIO_SRC_PCM) {
            return true;
        }
        if (g_decoder_done) {
            if (g_verify == AUDIO_VERIFY_RUNNING) { // no sound in verify mode: done as soon as the data is
                finish_verify();
                stop_source();
                g_audio_pcm_ended++;
                push_event_loop(AUDIO_EVT_END_OF_TRACK, 0);
                return true;
            }
            if (spsc_ring_available(&g_pcm_ring) == 0) {
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
    // The output starts once some audio is buffered, or the stream is already complete.
    if (g_priming != 0 && g_src != AUDIO_SRC_NONE &&
        (spsc_ring_available(&g_pcm_ring) >= PRIME_FRAMES || g_decoder_done || (g_eof != 0 && g_desc.codec == AUDIO_CODEC_PCM))) {
        g_priming = 0;
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
        check_dec_window();

        audio_cmd_t cmd;
        for (;;) {
            if (g_have_deferred) {
                cmd = g_deferred;
                g_have_deferred = false;
            } else if (spsc_ring_read(&g_audio_cmd_ring, &cmd, 1) != 1) {
                break;
            }
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

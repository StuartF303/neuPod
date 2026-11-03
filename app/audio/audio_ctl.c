// SPDX-License-Identifier: MPL-2.0
// Core-0 side of the audio engine (runs in RTOS tasks; may use anything core 0 may use).
#include <stdatomic.h>
#include <string.h>

#include "audio_shared.h"
#include "stream_io.h"
#include "hal_core.h"
#include "os.h"

static void drop_next(void);

extern void audio_core_main(void); // app/audio/audio_core.c, runs on core 1

// The command ring has one producer, but several core-0 tasks send commands (UI, storage, monitor): serialise them.
static os_mutex_t *g_send_mutex;

bool audio_engine_start(void) {
    g_send_mutex = os_mutex_create();
    audio_shared_init(); // before launch: core 1 must find initialised rings
    return hal_core1_launch(audio_core_main) == HAL_OK;
}

static bool send(uint32_t type, uint32_t a, uint32_t b) {
    audio_cmd_t c = {.type = type, .a = a, .b = b};
    if (g_send_mutex != NULL && !os_mutex_lock(g_send_mutex, 20)) {
        return false;
    }
    bool ok = spsc_ring_write(&g_audio_cmd_ring, &c, 1) == 1;
    if (g_send_mutex != NULL) {
        os_mutex_unlock(g_send_mutex);
    }
    return ok;
}

bool audio_open(uint32_t sample_rate, uint8_t channels, uint8_t valid_bits) {
    return send(AUDIO_CMD_OPEN, sample_rate, (uint32_t)channels | ((uint32_t)valid_bits << 8));
}

bool audio_close(void) {
    drop_next();
    return send(AUDIO_CMD_CLOSE, 0, 0);
}

bool audio_stats_new_window(void) { return send(AUDIO_CMD_NEW_WINDOW, 0, 0); }

bool audio_set_volume(uint32_t att_db) { return send(AUDIO_CMD_VOLUME, att_db, 0); }
bool audio_tone(uint32_t mode) { return send(AUDIO_CMD_TONE, mode, 0); }
// ---- Streams ----------------------------------------------------------------------------------------------------
//
// A stream is announced to core 1 in one of two ways: audio_stream_start() (hard: core 1 flushes everything, may re-clock
// the sink, ramps) or audio_stream_next() (gapless: core 1 takes it over when the current stream's last byte has been
// decoded, without touching the sink or the PCM ring). Each gets a sequence number; its description and its end marker live
// in slot (seq & 1) (audio_shared.h). The data ring carries the streams back to back; the byte count published by
// audio_stream_end() tells core 1 where one stops and the next begins.
//
// Hard starts are serialised: the first byte may only be written once core 1 has processed the start (it counts every
// start, including failed ones, in g_audio_pcm_started).
static uint32_t g_starts_issued;
static bool start_allowed(void) { return g_starts_issued == g_audio_pcm_started; }

static uint32_t g_seq;        // last sequence number handed out
static uint32_t g_wseq;       // stream the data being written belongs to (0 = none yet)
static uint32_t g_next_seq;   // announced successor, not yet being written (0 = none)
static uint32_t g_written;    // bytes of stream g_wseq written so far (what audio_stream_end() publishes)

static void drop_next(void) {
    g_audio_next_valid = 0; // before anything reuses the slot: core 1 re-checks this when it takes the stream over
    atomic_thread_fence(memory_order_release); // ... and the clear is visible before any later write to the slot
    g_next_seq = 0;
}

static void slot_prepare(uint32_t seq, const audio_stream_desc_t *d) {
    g_audio_slot[seq & 1u].desc = *d;
    g_audio_slot[seq & 1u].end = SIO_END_UNKNOWN;
}

bool audio_stream_start(const audio_stream_desc_t *d) {
    if (!start_allowed()) {
        return false;
    }
    drop_next();
    const uint32_t seq = g_seq + 1u;
    slot_prepare(seq, d);
    if (!send(AUDIO_CMD_STREAM_START, seq, 0)) {
        return false;
    }
    g_seq = seq;
    g_wseq = seq;
    g_written = 0;
    g_starts_issued++;
    return true;
}

bool audio_pcm_start(uint32_t rate, uint8_t channels, uint8_t bits) {
    audio_stream_desc_t d;
    memset(&d, 0, sizeof d);
    d.codec = AUDIO_CODEC_PCM;
    d.rate = rate;
    d.channels = channels;
    d.bits = bits;
    return audio_stream_start(&d);
}

bool audio_stream_next_ok(void) {
    // The stream being fed must be open in the decoder (so its slot's partner is free and no seek of an opening decoder is
    // in flight), and nothing may be announced already.
    return g_wseq != 0 && g_next_seq == 0 && g_audio_open_seq == g_wseq && start_allowed();
}

bool audio_stream_next(const audio_stream_desc_t *d) {
    if (!audio_stream_next_ok()) {
        return false;
    }
    const uint32_t seq = g_seq + 1u;
    slot_prepare(seq, d);
    g_audio_next_valid = seq; // visible before the command (the ring's release store orders both)
    if (!send(AUDIO_CMD_STREAM_NEXT, seq, 0)) {
        g_audio_next_valid = 0;
        return false;
    }
    g_seq = seq;
    g_next_seq = seq;
    return true;
}

void audio_stream_next_cancel(void) { drop_next(); }

bool audio_stream_next_pending(void) { return g_next_seq != 0; }

bool audio_stream_end(void) {
    if (g_wseq == 0) {
        return false;
    }
    atomic_thread_fence(memory_order_release); // every byte of the stream is in the ring before its length is published
    g_audio_slot[g_wseq & 1u].end = g_written;
    if (g_next_seq != 0) { // the bytes that follow belong to the announced successor
        g_wseq = g_next_seq;
        g_next_seq = 0;
        g_written = 0;
    }
    return true;
}

bool audio_stream_end_published(void) { return g_wseq != 0 && g_audio_slot[g_wseq & 1u].end != SIO_END_UNKNOWN; }

static uint64_t g_seek_target;
static uint32_t g_seek_seen;
bool audio_seek_poll(uint64_t *offset) {
    if (g_audio_seek_req == g_seek_seen) {
        return false;
    }
    g_seek_seen = g_audio_seek_req;
    atomic_thread_fence(memory_order_acquire); // read the offset only after seeing the request counter move
    *offset = g_audio_seek_off;
    g_seek_target = *offset;
    return true;
}
void audio_seek_ack(void) {
    // The decoder is rewinding the stream being fed: whatever was announced behind it is dropped (core 1 forgets it when it
    // flushes) and the end marker, if any, is withdrawn: the feed publishes it again after it has written the rest.
    drop_next();
    g_audio_slot[g_wseq & 1u].end = SIO_END_UNKNOWN;
    g_written = (uint32_t)g_seek_target;
    atomic_thread_fence(memory_order_release); // everything core 0 did to stop writing is visible before the ack
    g_audio_seek_ack = g_seek_seen;
}
bool audio_seek_done(void) {
    bool d = g_audio_seek_done == g_seek_seen;
    atomic_thread_fence(memory_order_acquire);
    return d;
}
uint32_t audio_codec_error(void) { return g_audio_codec_error; }
uint32_t audio_pcm_started_count(void) { return g_audio_pcm_started; }
uint32_t audio_pcm_ended_count(void) { return g_audio_pcm_ended; }
uint32_t audio_track_change_count(void) { return g_audio_track_changes; }
bool audio_pcm_end(void) { return audio_stream_end(); }
bool audio_pause(bool paused) { return send(AUDIO_CMD_PAUSE, paused ? 1u : 0u, 0); }
bool audio_stop(void) {
    drop_next();
    return send(AUDIO_CMD_STOP, 0, 0);
}
uint32_t audio_data_space(void) { return spsc_ring_space(&g_audio_data_ring); }

uint32_t audio_data_write(const void *data, uint32_t bytes) {
    uint32_t n = spsc_ring_write(&g_audio_data_ring, data, bytes);
    g_written += n;
    return n;
}

bool audio_event_poll(audio_event_t *out) {
    return spsc_ring_read(&g_audio_evt_isr_ring, out, 1) == 1 ||
           spsc_ring_read(&g_audio_evt_loop_ring, out, 1) == 1;
}

bool audio_stats_get(audio_stats_t *out) {
    // Bounded: if core 1 stopped in the middle of a write (fault, debugger halt) the sequence number
    // stays odd and an unbounded reader would hang the monitor task, hiding the very stall it must report.
    for (int tries = 0; tries < 1000; tries++) {
        uint32_t q;
        if (!seqlock_read_try_begin(&g_audio_stats_lock, &q)) {
            continue;
        }
        memcpy(out, &g_audio_stats, sizeof *out);
        if (!seqlock_read_retry(&g_audio_stats_lock, q)) {
            return true;
        }
    }
    return false;
}

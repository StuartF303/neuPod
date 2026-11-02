// SPDX-License-Identifier: MPL-2.0
// Core-0 side of the audio engine (runs in RTOS tasks; may use anything core 0 may use).
#include <stdatomic.h>
#include <string.h>

#include "audio_shared.h"
#include "hal_core.h"
#include "os.h"

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

bool audio_close(void) { return send(AUDIO_CMD_CLOSE, 0, 0); }

bool audio_stats_new_window(void) { return send(AUDIO_CMD_NEW_WINDOW, 0, 0); }

bool audio_set_volume(uint32_t att_db) { return send(AUDIO_CMD_VOLUME, att_db, 0); }
bool audio_tone(uint32_t mode) { return send(AUDIO_CMD_TONE, mode, 0); }
// Stream starts are serialised: the descriptor is one shared slot, so a new start is refused until core 1 has
// processed the previous one (it counts every start, including failed ones, in g_audio_pcm_started).
static uint32_t g_starts_issued;
static bool start_allowed(void) { return g_starts_issued == g_audio_pcm_started; }

bool audio_pcm_start(uint32_t rate, uint8_t channels, uint8_t bits) {
    if (!start_allowed() || !send(AUDIO_CMD_PCM_START, rate, (uint32_t)channels | ((uint32_t)bits << 8))) {
        return false;
    }
    g_starts_issued++;
    return true;
}
bool audio_stream_start(const audio_stream_desc_t *d) {
    if (!start_allowed()) {
        return false;
    }
    g_audio_stream_desc = *d;
    if (!send(AUDIO_CMD_STREAM_START, 0, 0)) {
        return false;
    }
    g_starts_issued++;
    return true;
}
static uint32_t g_seek_seen;
bool audio_seek_poll(uint64_t *offset) {
    if (g_audio_seek_req == g_seek_seen) {
        return false;
    }
    g_seek_seen = g_audio_seek_req;
    atomic_thread_fence(memory_order_acquire); // read the offset only after seeing the request counter move
    *offset = g_audio_seek_off;
    return true;
}
void audio_seek_ack(void) {
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
bool audio_pcm_end(void) { return send(AUDIO_CMD_PCM_END, 0, 0); }
bool audio_pause(bool paused) { return send(AUDIO_CMD_PAUSE, paused ? 1u : 0u, 0); }
bool audio_stop(void) { return send(AUDIO_CMD_STOP, 0, 0); }
uint32_t audio_data_space(void) { return spsc_ring_space(&g_audio_data_ring); }

uint32_t audio_data_write(const void *data, uint32_t bytes) {
    return spsc_ring_write(&g_audio_data_ring, data, bytes);
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

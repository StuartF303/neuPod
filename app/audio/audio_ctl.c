// SPDX-License-Identifier: MPL-2.0
// Core-0 side of the audio engine (runs in RTOS tasks; may use anything core 0 may use).
#include <string.h>

#include "audio_shared.h"
#include "hal_core.h"

extern void audio_core_main(void); // app/audio/audio_core.c, runs on core 1

bool audio_engine_start(void) {
    audio_shared_init(); // before launch: core 1 must find initialised rings
    return hal_core1_launch(audio_core_main) == HAL_OK;
}

static bool send(uint32_t type, uint32_t a, uint32_t b) {
    audio_cmd_t c = {.type = type, .a = a, .b = b};
    return spsc_ring_write(&g_audio_cmd_ring, &c, 1) == 1;
}

bool audio_open(uint32_t sample_rate, uint8_t channels, uint8_t valid_bits) {
    return send(AUDIO_CMD_OPEN, sample_rate, (uint32_t)channels | ((uint32_t)valid_bits << 8));
}

bool audio_close(void) { return send(AUDIO_CMD_CLOSE, 0, 0); }

bool audio_stats_new_window(void) { return send(AUDIO_CMD_NEW_WINDOW, 0, 0); }

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

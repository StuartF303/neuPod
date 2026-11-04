// SPDX-License-Identifier: MPL-2.0
#include "audio_shared.h"

#define CMD_SLOTS 16u
#define DATA_BYTES 32768u // 85 ms of 32-bit stereo at 48 kHz: rides out an SD hiccup
#define EVT_SLOTS 32u

static audio_cmd_t g_cmd_store[CMD_SLOTS];
static uint8_t g_data_store[DATA_BYTES];
static audio_event_t g_evt_isr_store[EVT_SLOTS];
static audio_event_t g_evt_loop_store[EVT_SLOTS];

spsc_ring_t g_audio_cmd_ring;
spsc_ring_t g_audio_data_ring;
spsc_ring_t g_audio_evt_isr_ring;
spsc_ring_t g_audio_evt_loop_ring;
seqlock_t g_audio_stats_lock;
audio_stats_t g_audio_stats;
volatile uint32_t g_audio_pcm_started;
volatile uint32_t g_audio_pcm_ended;
audio_slot_t g_audio_slot[2];
volatile uint32_t g_audio_next_valid;
volatile uint32_t g_audio_open_seq;
volatile uint32_t g_audio_track_changes;
volatile uint32_t g_audio_seek_req, g_audio_seek_ack, g_audio_seek_done;
volatile uint64_t g_audio_seek_off;
volatile uint32_t g_audio_verify_state, g_audio_decoded_frames, g_audio_codec_error;

void audio_shared_init(void) {
    (void)spsc_ring_init(&g_audio_cmd_ring, g_cmd_store, sizeof(audio_cmd_t), CMD_SLOTS);
    (void)spsc_ring_init(&g_audio_data_ring, g_data_store, 1, DATA_BYTES);
    (void)spsc_ring_init(&g_audio_evt_isr_ring, g_evt_isr_store, sizeof(audio_event_t), EVT_SLOTS);
    (void)spsc_ring_init(&g_audio_evt_loop_ring, g_evt_loop_store, sizeof(audio_event_t), EVT_SLOTS);
}

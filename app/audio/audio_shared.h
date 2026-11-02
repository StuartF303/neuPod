// SPDX-License-Identifier: MPL-2.0
// Private to app/audio: the rings and statistics shared by the core-0 side (audio_ctl.c) and the
// core-1 side (audio_core.c). Plain data and lock-free types only (core-1 rules, ADR-0011).
#ifndef NEUPOD_AUDIO_SHARED_H
#define NEUPOD_AUDIO_SHARED_H

#include "audio_engine.h"
#include "seqlock.h"
#include "spsc_ring.h"

typedef enum {
    AUDIO_CMD_OPEN = 1, /* a = sample rate, b = channels | valid_bits << 8 */
    AUDIO_CMD_CLOSE,
    AUDIO_CMD_NEW_WINDOW,
    AUDIO_CMD_VOLUME,    /* a = attenuation dB */
    AUDIO_CMD_TONE,      /* a = tone_mode_t (TONE_OFF stops) */
    AUDIO_CMD_PCM_START, /* a = rate, b = channels | bits << 8 */
    AUDIO_CMD_PCM_END,
    AUDIO_CMD_PAUSE,     /* a = 1 pause, 0 resume */
    AUDIO_CMD_STOP,
    AUDIO_CMD_STREAM_START, /* codec stream described by g_audio_stream_desc */
} audio_cmd_type_t;

typedef struct {
    uint32_t type;
    uint32_t a;
    uint32_t b;
} audio_cmd_t;

extern spsc_ring_t g_audio_cmd_ring;       /* core 0 produces, core 1 loop consumes */
extern spsc_ring_t g_audio_data_ring;      /* core 0 produces, core 1 loop consumes */
extern spsc_ring_t g_audio_evt_isr_ring;   /* produced by the core-1 block interrupt */
extern spsc_ring_t g_audio_evt_loop_ring;  /* produced by the core-1 main loop (one producer each) */
extern seqlock_t g_audio_stats_lock;       /* written only by the core-1 block interrupt */
extern audio_stats_t g_audio_stats;
/* Handshake counters written only by core 1 (plain 32-bit stores) and polled by core 0: how many PCM streams core 1
 * has set up (flushed, ready for data) and how many it has played to the end. Core 0 must not write PCM data for a
 * new stream before g_audio_pcm_started shows its audio_pcm_start() was processed, or the setup would discard it. */
extern volatile uint32_t g_audio_pcm_started;
extern volatile uint32_t g_audio_pcm_ended;
/* Description of the next codec stream, written by core 0 before AUDIO_CMD_STREAM_START (the ring's release store
 * publishes it) and read by core 1 while handling that command. */
extern audio_stream_desc_t g_audio_stream_desc;
/* Seek handshake (stream_io.h) */
extern volatile uint32_t g_audio_seek_req, g_audio_seek_ack, g_audio_seek_done;
extern volatile uint64_t g_audio_seek_off;
/* Core 1 -> core 0: FLAC verify progress/result, frames decoded so far in the current stream, last decode error */
extern volatile uint32_t g_audio_verify_state, g_audio_decoded_frames, g_audio_codec_error;

/** Initialise the rings (core 0, before core 1 is launched). */
void audio_shared_init(void);

#endif // NEUPOD_AUDIO_SHARED_H

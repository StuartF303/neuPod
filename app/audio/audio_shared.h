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
    AUDIO_CMD_PAUSE,     /* a = 1 pause, 0 resume */
    AUDIO_CMD_STOP,
    AUDIO_CMD_STREAM_START, /* a = stream sequence number; the stream is described by g_audio_slot[a & 1] */
    AUDIO_CMD_STREAM_NEXT,  /* a = sequence number of the stream to continue with, gapless, when the current one ends */
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
/* Stream slots. Every stream core 0 starts or announces gets a sequence number (1, 2, ...) and uses slot (seq & 1): a
 * hard start (STREAM_START) and the gapless handoff (STREAM_NEXT) both take their description from there, so the current
 * stream's slot is never overwritten while the next is announced. Core 0 fills desc and sets end = SIO_END_UNKNOWN before
 * sending the command (the ring's release store publishes them) and later publishes end, the stream's total length in bytes
 * (stream_io.h), once its last byte is in the data ring. Core 1 only reads. */
typedef struct {
    audio_stream_desc_t desc;
    volatile uint32_t end;
} audio_slot_t;
extern audio_slot_t g_audio_slot[2];
/* Core 0 -> core 1: the sequence number of the announced next stream while it may still be used (0 = none / cancelled).
 * A STREAM_NEXT whose number differs is stale and ignored: core 0 cancels by clearing this BEFORE it reuses the slot. */
extern volatile uint32_t g_audio_next_valid;
/* Core 1 -> core 0: sequence number of the stream whose decoder is open and running (set after the codec opened). Core 0
 * announces the next stream only when this equals the stream it is feeding: then the other slot is free and no seek of an
 * opening decoder can be in flight. */
extern volatile uint32_t g_audio_open_seq;
/* Gapless handoffs completed (each fired AUDIO_EVT_END_OF_TRACK with value 1); written only by the block interrupt. */
extern volatile uint32_t g_audio_track_changes;
/* Seek handshake (stream_io.h) */
extern volatile uint32_t g_audio_seek_req, g_audio_seek_ack, g_audio_seek_done;
extern volatile uint64_t g_audio_seek_off;
/* Core 1 -> core 0: FLAC verify progress/result, frames decoded so far in the current stream, last decode error */
extern volatile uint32_t g_audio_verify_state, g_audio_decoded_frames, g_audio_codec_error;

/** Initialise the rings (core 0, before core 1 is launched). */
void audio_shared_init(void);

#endif // NEUPOD_AUDIO_SHARED_H

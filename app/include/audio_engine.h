// SPDX-License-Identifier: MPL-2.0
/**
 * @file audio_engine.h
 * @brief Core-0 interface to the bare-metal audio core (timing stub; no real codec or DAC yet).
 *
 * Core 1 runs app/audio/audio_core.c with no RTOS (docs/adr/0011-bare-metal-audio-core.md). Core 0
 * (the RTOS side) talks to it only through lock-free rings and a seqlock:
 *   core 0 -> core 1  command ring        (open / close / volume / tone / pcm start-end / pause / stop ...)
 *   core 0 -> core 1  PCM data ring        (WAV bytes for the bring-up player; the compressed-data input later)
 *   core 1 -> core 0  event ring          (stream opened/closed, underrun, end of track)
 *   core 1 -> core 0  statistics seqlock
 * Inside core 1 a "decoder" (silence, test-signal generator, or PCM converter) fills a PCM ring and the
 * sink's block interrupt drains it, applies the volume and hands the block to the sink.
 */
#ifndef NEUPOD_AUDIO_ENGINE_H
#define NEUPOD_AUDIO_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE 44100u
#define AUDIO_DATA_RING_BYTES 32768u /**< capacity of the compressed-data ring (85 ms of 32-bit stereo at 48 kHz): rides out an SD hiccup */

typedef struct {
    uint32_t blocks;           /**< Blocks served since the stream opened. */
    uint32_t underruns;        /**< Blocks the PCM ring could not fill (decoder starved): played as silence. */
    uint32_t sink_missed;      /**< Blocks the sink wanted that were served late (HAL under-runs). */
    uint32_t period_min_us;    /**< Window: shortest time between two block interrupts (0 until measured). */
    uint32_t period_avg_us;    /**< Window: mean time between two block interrupts. */
    uint32_t period_max_us;    /**< Window: longest time between two block interrupts. */
    uint32_t jitter_max_us;    /**< Window: largest |period - nominal|. */
    uint32_t period_nominal_us;
    uint32_t ring_min_frames;  /**< Window: lowest PCM ring fill seen at the start of a block. */
    uint32_t ring_frames;      /**< PCM ring capacity. */
    uint32_t window_blocks;    /**< Blocks in the window. */
    uint32_t decoded_blocks;   /**< Blocks the decoder stub produced. */
    uint32_t data_bytes;       /**< Compressed bytes consumed from the data ring. */
    uint32_t events_dropped;   /**< Events lost because an event ring was full. */
    uint32_t source;           /**< audio_source_t */
    uint32_t audible;          /**< 1 while a source is playing (not paused, not stopped) */
    uint32_t volume_att_db;    /**< software volume: attenuation in dB (0 = bit-perfect) */
    uint32_t muted;            /**< 1 while the output stage's mute line is asserted */
    uint32_t sink_rate_hz;     /**< sample rate the sink was opened at (0 = closed) */
    uint32_t sink_rate_mhz;    /**< rate actually generated, millihertz (clock divider rounding) */
    uint32_t sink_clkdiv_x256; /**< sink clock divider, 1/256 units */
    uint32_t pcm_frames;       /**< frames of the current stream played so far */
    uint32_t codec;            /**< audio_codec_t of the current stream */
    uint32_t codec_error;      /**< codec_err_t of the last failed stream (0 = none) */
    uint32_t verify_state;     /**< audio_verify_t */
    uint32_t decoded_frames;   /**< frames decoded so far (verify mode progress) */
    uint32_t dec_us_block_avg; /**< window: decoder CPU per 256 output frames, microseconds (interrupt time subtracted) */
    uint32_t dec_us_block_max; /**< window: worst single decode call, scaled to 256 frames */
    uint32_t dec_pct_x10;      /**< window: decoder load, tenths of a percent of one core (avg / block period) */
} audio_stats_t;

typedef enum { AUDIO_CODEC_PCM = 0, AUDIO_CODEC_FLAC, AUDIO_CODEC_MP3, AUDIO_CODEC_ALAC } audio_codec_t;
typedef enum { AUDIO_VERIFY_NONE = 0, AUDIO_VERIFY_RUNNING, AUDIO_VERIFY_OK, AUDIO_VERIFY_MISMATCH, AUDIO_VERIFY_NO_MD5 } audio_verify_t;

/** Everything the audio core needs to know about a stream before data arrives. */
typedef struct {
    uint32_t codec; /**< audio_codec_t */
    uint32_t rate;
    uint32_t channels, bits;
    uint32_t verify;     /**< FLAC: decode flat out, no sound, and compare the MD5 */
    uint64_t total_frames; /**< length the container announces (0 = unknown, or not trustworthy: MP3 estimates) */
    uint32_t skip_frames;  /**< ALAC: encoder priming frames to drop at the start (with trim_exact) */
    uint32_t trim_exact;   /**< 1: total_frames is the exact output length (edit list / iTunSMPB): the decoder cuts there */
    uint8_t md5[16];     /**< FLAC STREAMINFO checksum (all zero: none recorded) */
    uint32_t cookie_len; /**< ALAC magic cookie */
    uint8_t cookie[64];
} audio_stream_desc_t;

typedef enum { AUDIO_SRC_NONE = 0, AUDIO_SRC_TONE, AUDIO_SRC_PCM } audio_source_t;

typedef enum {
    AUDIO_EVT_STREAM_OPENED = 1,
    AUDIO_EVT_STREAM_CLOSED,
    AUDIO_EVT_UNDERRUN,     /**< value = total underruns so far (at most one event per second) */
    AUDIO_EVT_END_OF_TRACK, /**< a stream's last sample has played. value 0: nothing follows (the output stops); 1: gapless handoff, the next stream is already playing */
    AUDIO_EVT_CMD_ERROR,    /**< value = command type that failed */
    AUDIO_EVT_STORM,        /**< PCM data starved for too long: playback was stopped and muted (value = blocks) */
} audio_event_type_t;

typedef struct {
    uint32_t type; /**< audio_event_type_t */
    uint32_t value;
} audio_event_t;

/** Launch the audio core (core 1). Call once from core 0 after hal_system_init(). */
bool audio_engine_start(void);

/** Ask the audio core to open a stream. False if the command ring is full. */
bool audio_open(uint32_t sample_rate, uint8_t channels, uint8_t valid_bits);
/** Ask the audio core to close the stream. */
bool audio_close(void);
/** Offer PCM bytes (little-endian integer samples of the format given to audio_pcm_start); returns the bytes accepted. */
uint32_t audio_data_write(const void *data, uint32_t bytes);
/** Bytes the data ring can take right now. */
uint32_t audio_data_space(void);

/** Software volume as an attenuation in dB (0 = full scale, bit-perfect). Clamped to the build's hard cap. */
bool audio_set_volume(uint32_t att_db);
/** Start (or, with TONE_OFF, stop) a test signal on the open stream. Values are tone_mode_t from audio_dsp.h. */
bool audio_tone(uint32_t mode);
/**
 * Start streaming PCM: the data ring is flushed, the sink is re-clocked if @p rate differs, and playback
 * begins as soon as data arrives. Then feed audio_data_write(), and call audio_pcm_end() after the last byte.
 */
bool audio_pcm_start(uint32_t rate, uint8_t channels, uint8_t bits);
/**
 * Start a codec stream (FLAC / MP3: the compressed bytes in file order; ALAC: length-prefixed packets). As for
 * audio_pcm_start(), write no data before audio_pcm_started_count() shows the request was processed. Replaces whatever plays,
 * with a ramp, and cancels an announced successor.
 */
bool audio_stream_start(const audio_stream_desc_t *d);
/** Seek requests from the decoder: true (once per request) with the stream byte offset wanted. */
bool audio_seek_poll(uint64_t *offset);
/** Core 0 has repositioned its feed to the requested offset (and writes nothing until audio_seek_done()). */
void audio_seek_ack(void);
/** Core 1 has flushed the ring: the feed may resume. */
bool audio_seek_done(void);
uint32_t audio_codec_error(void);
/** Number of hard starts (audio_pcm_start / audio_stream_start) core 1 has finished setting up (data may be written once this reaches your count). */
uint32_t audio_pcm_started_count(void);
/** Number of streams that played to their end with nothing following (AUDIO_EVT_END_OF_TRACK with value 0, but pollable by any task). */
uint32_t audio_pcm_ended_count(void);
/** Number of gapless handoffs completed: the last sample of a stream has played and the announced successor's first sample follows
 *  (AUDIO_EVT_END_OF_TRACK with value 1). */
uint32_t audio_track_change_count(void);
/**
 * The current stream's last byte is in the data ring: publish its length so core 1 knows where it ends. If a successor was announced
 * (audio_stream_next), the bytes written from now on belong to it, and it may be written at once (no waiting for core 1).
 * Call audio_stream_next_pending() first to know which case applies. Always succeeds once a stream was started.
 */
bool audio_stream_end(void);
/** Old name of audio_stream_end(). */
bool audio_pcm_end(void);
/** True once the end of the stream being fed has been published (and not withdrawn by a seek). */
bool audio_stream_end_published(void);
/**
 * Gapless handoff, step 1: tell core 1 which stream follows the one being fed. Accepted only when the current stream's decoder is
 * open (core 1 reports it) and no successor is announced yet; otherwise false: try again later. The caller must then (a) finish
 * writing the current stream, (b) call audio_stream_end(), (c) write the successor's bytes. @p d must have the same sample rate and
 * channel count as the current stream (a different format is a hard start: audio_stream_start() after the end event).
 */
bool audio_stream_next(const audio_stream_desc_t *d);
/** audio_stream_next() would be accepted right now. */
bool audio_stream_next_ok(void);
/** Withdraw an announced successor that has not started being written (skip, stop). Safe at any time. */
void audio_stream_next_cancel(void);
/** A successor is announced and the current stream has not been ended yet. */
bool audio_stream_next_pending(void);
bool audio_pause(bool paused);
/** Stop the current source (tone or PCM) with a short fade; the stream stays open. */
bool audio_stop(void);

/** Next event from the audio core, if any. */
bool audio_event_poll(audio_event_t *out);

/**
 * Latest statistics; lock-free snapshot, safe from any task. Retries a bounded number of times;
 * false (and @p out untouched) if core 1 did not publish a consistent snapshot, e.g. it stopped mid-write.
 */
bool audio_stats_get(audio_stats_t *out);

/** Restart the window figures (period min/avg/max, jitter, ring minimum); counters keep running. */
bool audio_stats_new_window(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_AUDIO_ENGINE_H

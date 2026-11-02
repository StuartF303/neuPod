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
    uint32_t pcm_frames;       /**< frames of the current WAV stream played so far */
} audio_stats_t;

typedef enum { AUDIO_SRC_NONE = 0, AUDIO_SRC_TONE, AUDIO_SRC_PCM } audio_source_t;

typedef enum {
    AUDIO_EVT_STREAM_OPENED = 1,
    AUDIO_EVT_STREAM_CLOSED,
    AUDIO_EVT_UNDERRUN,     /**< value = total underruns so far (at most one event per second) */
    AUDIO_EVT_END_OF_TRACK, /**< the PCM stream ran to its end and has finished playing */
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
/** Number of audio_pcm_start() requests core 1 has finished setting up (data may be written once this reaches your count). */
uint32_t audio_pcm_started_count(void);
/** Number of PCM streams that played to their end (AUDIO_EVT_END_OF_TRACK, but pollable by any task). */
uint32_t audio_pcm_ended_count(void);
/** No more PCM follows what is already in the data ring: report AUDIO_EVT_END_OF_TRACK when it has played. */
bool audio_pcm_end(void);
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

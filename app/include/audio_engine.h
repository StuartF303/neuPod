// SPDX-License-Identifier: MPL-2.0
/**
 * @file audio_engine.h
 * @brief Core-0 interface to the bare-metal audio core (timing stub; no real codec or DAC yet).
 *
 * Core 1 runs app/audio/audio_core.c with no RTOS (docs/adr/0011-bare-metal-audio-core.md). Core 0
 * (the RTOS side) talks to it only through lock-free rings and a seqlock:
 *   core 0 -> core 1  command ring        (open / close / new statistics window; later volume, ...)
 *   core 0 -> core 1  compressed-data ring (stub: consumed and discarded; the decoder's input later)
 *   core 1 -> core 0  event ring          (stream opened/closed, underrun, end of track)
 *   core 1 -> core 0  statistics seqlock
 * Inside core 1 a decoder stub fills a PCM ring with silence and the block-clock interrupt drains it
 * into the sink.
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
} audio_stats_t;

typedef enum {
    AUDIO_EVT_STREAM_OPENED = 1,
    AUDIO_EVT_STREAM_CLOSED,
    AUDIO_EVT_UNDERRUN,     /**< value = total underruns so far (at most one event per second) */
    AUDIO_EVT_END_OF_TRACK, /**< reserved: the real decoder reports track ends */
    AUDIO_EVT_CMD_ERROR,    /**< value = command type that failed */
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
/** Offer compressed data; returns the bytes accepted (the ring may be full). */
uint32_t audio_data_write(const void *data, uint32_t bytes);

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

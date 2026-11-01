// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_audio.h
 * @brief PCM audio sink.
 *
 * Policy (docs/adr/0003-audio-quality-policy.md): the app decodes once into a 32-bit mixer
 * and hands the sink frames at the source rate; the sink only resamples if the hardware
 * cannot run at that rate. Samples are signed 32-bit, left-justified, interleaved.
 */
#ifndef NEUPOD_HAL_AUDIO_H
#define NEUPOD_HAL_AUDIO_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Stream format requested by the app. */
typedef struct {
    uint32_t sample_rate; /**< Hz, e.g. 44100. */
    uint8_t channels;     /**< 1 or 2. */
    uint8_t valid_bits;   /**< Significant bits of the source (16, 24, 32) for the sink's info. */
} hal_audio_format;

/** Initialise the sink hardware (muted). */
hal_status_t hal_audio_init(void);

/** True if the sink can run natively at @p sample_rate without resampling. */
bool hal_audio_rate_supported(uint32_t sample_rate);

/** Start a stream in @p fmt. Returns HAL_ENOTSUP if the format cannot be honoured. */
hal_status_t hal_audio_open(const hal_audio_format *fmt);

/**
 * Queue up to @p frames interleaved int32 frames.
 * @return frames accepted (may be fewer than requested when the buffer is full), or a
 *         negative hal_status_t.
 */
int32_t hal_audio_write(const int32_t *samples, uint32_t frames);

/** Frames that can currently be written without blocking. */
uint32_t hal_audio_free_frames(void);

/** Mute / unmute the output stage. */
void hal_audio_mute(bool mute);

/** Stop the stream and release buffers. */
void hal_audio_close(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_AUDIO_H

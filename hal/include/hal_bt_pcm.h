// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_bt_pcm.h
 * @brief The consumer side of the core 1 to core 0 Bluetooth PCM ring, as the Bluetooth stack sees it (docs/adr/0019).
 *
 * Kept apart from hal_radio.h so that the audio core (core 1, ADR-0011) can implement it without pulling in anything but plain types.
 */
#ifndef NEUPOD_HAL_BT_PCM_H
#define NEUPOD_HAL_BT_PCM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Where the encoder gets its PCM: the consumer side of the core 1 to core 0 ring (ADR-0013 amendment, ADR-0019). PCM never passes through
 * hal_bt_*; the stack only pulls from this source, always from its own thread, so every function must be lock-free and must not touch the RTOS.
 * Samples are signed 16-bit stereo, interleaved, at the rate of hal_bt_codec_t.
 */
typedef struct {
    void *ctx;
    uint32_t (*available)(void *ctx);          /**< frames readable now */
    uint32_t (*capacity)(void *ctx);           /**< ring size in frames (the pacing aims at half of it) */
    uint32_t (*read)(void *ctx, int16_t *dst, uint32_t frames); /**< copy out up to @p frames frames; returns the number read */
    void (*flush)(void *ctx);                  /**< drop everything buffered (consumer side) */
    bool (*active)(void *ctx);                 /**< optional: the producer is playing (an empty ring is then an underrun; paused / stopped it is not). NULL: always */
} hal_bt_pcm_source_t;

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_BT_PCM_H

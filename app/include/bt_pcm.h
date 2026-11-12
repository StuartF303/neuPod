// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_pcm.h
 * @brief The Bluetooth PCM ring: audio from the audio core (producer, core 1 block interrupt) to the Bluetooth encoder (consumer, the stack's
 *        thread on core 0). Signed 16-bit stereo, 4096 frames (93 ms at 44.1 kHz).
 *
 * ADR-0013 (amendment of 4 Nov 2026): the ring lives in the same 32 KB as the I2S PCM ring, which is split in two while the output is Bluetooth
 * (decoder ring 16 KB + this one 16 KB); the two outputs are exclusive and the split only changes with the stream closed. Core-1 rules
 * (ADR-0011) apply to the producer side and to this whole module: lock-free, no RTOS, no heap.
 */
#ifndef NEUPOD_BT_PCM_H
#define NEUPOD_BT_PCM_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_bt_pcm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_PCM_FRAME_BYTES 4u /**< 16-bit stereo */

/** Core 1, stream closed: lay the ring over @p storage (@p frames x 4 bytes; a power of two) and empty it. */
void bt_pcm_attach(void *storage, uint32_t frames);

/* ---- Producer (the audio core's block interrupt) ---- */
uint32_t bt_pcm_space(void);
uint32_t bt_pcm_write(const int16_t *interleaved, uint32_t frames);
/** The block interrupt says whether a source is playing (so an empty ring is an underrun and not a pause). */
void bt_pcm_set_active(bool active);

/* ---- Consumer (the Bluetooth stack's thread) ---- */
/** The source to hand to hal_bt_set_pcm_source(). */
const hal_bt_pcm_source_t *bt_pcm_source(void);

/**
 * Any task, one caller at a time (the audio routing): ask the consumer to drop whatever is buffered. Done by the consumer at its next call (so the
 * ring keeps a single reader); with no stream running it happens when the next one asks for audio.
 */
void bt_pcm_request_flush(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_PCM_H

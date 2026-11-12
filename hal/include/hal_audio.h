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

/**
 * Start a stream in @p fmt and its block clock; the first callback comes one block later.
 * Call from the core that should take the clock interrupt. Returns HAL_ENOTSUP if the format
 * cannot be honoured.
 */
hal_status_t hal_audio_open(const hal_audio_format *fmt);

/**
 * Queue up to @p frames interleaved int32 frames.
 * @return frames accepted (may be fewer than requested when the buffer is full), or a
 *         negative hal_status_t.
 */
int32_t hal_audio_write(const int32_t *samples, uint32_t frames);

/** Frames that can currently be written without blocking. */
uint32_t hal_audio_free_frames(void);

/**
 * Samples per pacing block: the sink asks for data in blocks of this many frames
 * (256 frames = 5.8 ms at 44.1 kHz), matching a half of a typical I2S DMA buffer.
 */
#define HAL_AUDIO_BLOCK_FRAMES 256u

/**
 * Block-clock callback: called once per block period from the sink's clock context, i.e. the
 * hardware timer interrupt on the core that called hal_audio_open() (I2S DMA interrupt later;
 * on the host, a timer thread). It must hand the sink its next block with hal_audio_write().
 * @p missed is the number of whole extra blocks that went by since the previous call (0 = on time;
 * more means the sink under-ran). Runs in interrupt context: no locks, no malloc, no stdio, and
 * nothing that needs an RTOS (docs/adr/0011-bare-metal-audio-core.md).
 */
typedef void (*hal_audio_block_cb)(uint32_t missed);

/** Register the block callback. Call before hal_audio_open(). */
void hal_audio_set_block_callback(hal_audio_block_cb cb);

/** Blocks the sink wanted that the callback served late (the DMA ran dry or replayed a block), since hal_audio_open(). */
uint32_t hal_audio_underruns(void);

/**
 * Idle the calling core until an interrupt (or, on the host, a short sleep). May return early;
 * callers loop. For the bare-metal audio core's main loop.
 */
void hal_audio_idle(void);

/**
 * Backend in use, for the boot self-check: "null" accepts and discards samples on a real-time
 * clock (no DAC); a hardware backend returns e.g. "i2s".
 */
const char *hal_audio_backend_name(void);

/**
 * Mute / unmute the output stage. Unmuting is honoured only while a stream is running (the sink is muted
 * from init until its first buffers are queued, and again on close). Safe from interrupt context.
 */
void hal_audio_mute(bool mute);

/**
 * Drive the output stage muted and stop the sink hardware, from ANY core and context, whether or not the
 * stream is open (used at boot and by the fatal-error path after core 1 was reset). Idempotent.
 */
void hal_audio_force_mute(void);

/** Sink facts for the statistics. All zero for the null sink. */
typedef struct {
    uint32_t rate_hz;          /**< requested sample rate */
    uint32_t rate_actual_mhz;  /**< rate actually generated, in millihertz (clock divider rounding) */
    uint32_t clkdiv_x256;      /**< sink clock divider in 1/256 units */
    bool muted;
} hal_audio_info_t;
void hal_audio_get_info(hal_audio_info_t *out);

/**
 * What the next hal_audio_open() is. false (the default): the wired output: the DAC on a board that has one, otherwise the null sink.
 * true: a virtual sink for Bluetooth output: the block clock runs at the sample rate exactly as for the wired sink, but hal_audio_write()
 * discards, because the audio core sends the audio to the Bluetooth ring instead (docs/bluetooth.md). Call with the sink closed.
 * A platform whose wired sink is already only a clock (host, Y2, the null sink) ignores it.
 */
void hal_audio_set_virtual(bool virtual_sink);

/** Stop the stream and release buffers. */
void hal_audio_close(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_AUDIO_H

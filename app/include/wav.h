// SPDX-License-Identifier: MPL-2.0
/**
 * @file wav.h
 * @brief RIFF/WAVE header parser for the bring-up player. Integer PCM only (16/24/32-bit, 1-2 channels,
 *        44.1/48/88.2/96 kHz). Portable, no heap, reads through a callback so it works on a file or memory.
 */
#ifndef NEUPOD_WAV_H
#define NEUPOD_WAV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WAV_OK = 0,
    WAV_E_READ,        /**< read failed or file too short for a header */
    WAV_E_NOT_RIFF,    /**< not a RIFF/WAVE file (RF64 and W64 are not supported) */
    WAV_E_NO_FMT,      /**< no fmt chunk before the data chunk */
    WAV_E_BAD_FMT,     /**< fmt chunk malformed */
    WAV_E_NO_DATA,     /**< no data chunk, or it is empty */
    WAV_E_NOT_PCM,     /**< float or compressed format */
    WAV_E_CHANNELS,    /**< not mono or stereo */
    WAV_E_BITS,        /**< not 16/24/32-bit */
    WAV_E_RATE,        /**< not 44.1/48/88.2/96 kHz */
} wav_err_t;

typedef struct {
    uint32_t rate;
    uint8_t channels;
    uint8_t bits;         /**< container bits per sample */
    uint32_t block_align; /**< bytes per frame */
    uint64_t data_offset; /**< file offset of the first sample byte */
    uint64_t data_bytes;  /**< sample bytes, a whole number of frames, clamped to the file */
    uint64_t frames;
} wav_info_t;

/** Read @p n bytes at @p offset into @p dst; return the bytes read (fewer at end of file, <= 0 on error). */
typedef int32_t (*wav_read_fn)(void *ctx, uint64_t offset, void *dst, uint32_t n);

wav_err_t wav_parse(wav_read_fn rd, void *ctx, uint64_t file_size, wav_info_t *out);
/** Plain-language reason, e.g. "24-bit float is not supported". Never NULL. */
const char *wav_err_str(wav_err_t e);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_WAV_H

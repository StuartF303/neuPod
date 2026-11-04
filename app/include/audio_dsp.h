// SPDX-License-Identifier: MPL-2.0
/**
 * @file audio_dsp.h
 * @brief Sample maths for the audio core: PCM to 32-bit conversion, software volume, test-signal
 *        generators. Pure integer code, no OS, no heap: usable on core 1 (ADR-0011) and in host tests.
 *
 * Mixer format (ADR-0003): interleaved stereo signed 32-bit, left-justified.
 */
#ifndef NEUPOD_AUDIO_DSP_H
#define NEUPOD_AUDIO_DSP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Volume: attenuation in whole dB, applied in the 32-bit domain (never any gain above 0 dB) ---- */

#define AUDIO_VOL_OFF_DB 80u         /**< this attenuation and above = silence */
#define AUDIO_VOL_DEFAULT_ATT_DB 30u /**< power-on level: -30 dB (headphone safety) */
#define AUDIO_GAIN_UNITY (1u << 30)  /**< Q30 gain of exactly 0 dB */
/** Hard cap: the smallest attenuation the engine accepts (0 = full scale allowed). Lower the loudest level of a build by raising it. */
#ifndef NEUPOD_AUDIO_VOLUME_CAP_DB
#define NEUPOD_AUDIO_VOLUME_CAP_DB 0u
#endif

/** Clamp to [NEUPOD_AUDIO_VOLUME_CAP_DB, AUDIO_VOL_OFF_DB]. */
uint32_t audio_vol_clamp(uint32_t att_db);
/** Q30 linear gain for an attenuation (clamped first). AUDIO_GAIN_UNITY at 0 dB, 0 at AUDIO_VOL_OFF_DB. */
uint32_t audio_gain_q30(uint32_t att_db);
/**
 * Multiply @p frames stereo frames by a gain that ramps linearly from @p g_from to @p g_to (Q30) across the
 * block. Equal gains = constant gain; both AUDIO_GAIN_UNITY = untouched samples (bit-exact).
 */
void audio_apply_gain(int32_t *s, uint32_t frames, uint32_t g_from, uint32_t g_to);

/* ---- PCM input conversion: exact, no dither, no resampling ---- */

/**
 * Little-endian integer PCM (@p bits 16, 24 or 32; @p channels 1 or 2) to stereo int32. The sample is
 * shifted left-justified (16-bit: << 16, 24-bit: << 8), so the low bits are zero and the conversion is
 * lossless. Mono is duplicated into both channels. Returns false for an unsupported format.
 */
bool audio_pcm_to_s32(int32_t *dst, const uint8_t *src, uint32_t frames, uint8_t bits, uint8_t channels);

/* ---- Test signals ---- */

/** Full-scale sine (peak 0x7FFFFFFF) for a 32-bit phase (2^32 = one cycle), table + linear interpolation. */
int32_t audio_sine_q31(uint32_t phase);

#define AUDIO_TONE_HZ 1000u
#define AUDIO_TONE_LEVEL_Q31 214748365 /**< 0.1 = -20 dBFS peak */
#define AUDIO_IDENT_SEGMENT_MS 1000u   /**< left, then right, 1 s each */
#define AUDIO_IDENT_LEFT_HZ 400u       /**< ident: low tone on the left */
#define AUDIO_IDENT_RIGHT_HZ 1600u     /**< ident: high tone (two octaves up) on the right */
#define AUDIO_SCALE_NOTE_MS 300u
#define AUDIO_SCALE_RAMP_MS 10u
#define AUDIO_SCALE_REST_MS 600u       /**< silence between passes */
#define AUDIO_SCALE_NOTES 15u          /**< C4..C5 up, then B4..C4 down */

typedef enum { TONE_OFF = 0, TONE_1K, TONE_IDENT, TONE_SCALE } tone_mode_t;

typedef struct {
    tone_mode_t mode;
    uint32_t rate;
    uint32_t phase, inc;
    uint32_t pos;     /**< frames into the current segment / note */
    uint32_t index;   /**< ident: 0 = left, 1 = right; scale: note index, AUDIO_SCALE_NOTES = rest */
    bool switch_pending;
} tonegen_t;

/** Frequency of scale note @p index (0..AUDIO_SCALE_NOTES-1) in millihertz (equal temperament, A4 = 440 Hz). */
uint32_t audio_scale_note_mhz(uint32_t index);
void tonegen_init(tonegen_t *g, tone_mode_t mode, uint32_t rate);
/** Write @p frames stereo frames at -20 dBFS peak (before the volume). */
void tonegen_fill(tonegen_t *g, int32_t *out, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_AUDIO_DSP_H

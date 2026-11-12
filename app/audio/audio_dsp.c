// SPDX-License-Identifier: MPL-2.0
// Sample maths for the audio core (core-1 rules apply: no OS, stdio or heap; ADR-0011).
#include "audio_dsp.h"

#include <stddef.h>

#include "ram_func.h"

#include "audio_tables.inc"

uint32_t audio_vol_clamp(uint32_t att_db) {
    const uint32_t cap = NEUPOD_AUDIO_VOLUME_CAP_DB; // a variable: the default cap of 0 would make the compare constant
    if (att_db < cap) {
        att_db = cap;
    }
    return att_db > AUDIO_VOL_OFF_DB ? AUDIO_VOL_OFF_DB : att_db;
}

uint32_t audio_gain_q30(uint32_t att_db) {
    att_db = audio_vol_clamp(att_db);
    return att_db >= AUDIO_VOL_OFF_DB ? 0u : k_gain_q30[att_db];
}

void NEUPOD_RAM_FUNC(audio_apply_gain)(int32_t *s, uint32_t frames, uint32_t g_from, uint32_t g_to) {
    if (g_from == AUDIO_GAIN_UNITY && g_to == AUDIO_GAIN_UNITY) {
        return; // 0 dB: the samples pass untouched
    }
    if (g_from == g_to) {
        if (g_to == 0u) {
            for (uint32_t i = 0; i < frames * 2u; i++) {
                s[i] = 0;
            }
            return;
        }
        for (uint32_t i = 0; i < frames * 2u; i++) {
            s[i] = (int32_t)(((int64_t)s[i] * (int64_t)g_to) >> 30);
        }
        return;
    }
    int32_t step = frames != 0 ? ((int32_t)g_to - (int32_t)g_from) / (int32_t)frames : 0;
    int32_t g = (int32_t)g_from;
    for (uint32_t i = 0; i < frames; i++) {
        s[2u * i] = (int32_t)(((int64_t)s[2u * i] * g) >> 30);
        s[2u * i + 1u] = (int32_t)(((int64_t)s[2u * i + 1u] * g) >> 30);
        g += step;
    }
}

bool audio_pcm_to_s32(int32_t *dst, const uint8_t *src, uint32_t frames, uint8_t bits, uint8_t channels) {
    if ((bits != 16 && bits != 24 && bits != 32) || channels < 1 || channels > 2) {
        return false;
    }
    const uint32_t bytes = bits / 8u;
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t v[2];
        for (uint32_t c = 0; c < channels; c++) {
            const uint8_t *p = src + ((size_t)i * channels + c) * bytes;
            if (bits == 16) {
                v[c] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 24);
            } else if (bits == 24) {
                v[c] = ((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24);
            } else {
                v[c] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            }
        }
        dst[2u * i] = (int32_t)v[0];
        dst[2u * i + 1u] = (int32_t)v[channels == 2 ? 1 : 0];
    }
    return true;
}

int32_t audio_sine_q31(uint32_t phase) {
    uint32_t idx = phase >> 22; // 10 bits of table index
    int64_t a = k_sine[idx];
    int64_t b = k_sine[(idx + 1u) & (SINE_TABLE_LEN - 1u)];
    uint32_t frac = (phase >> 6) & 0xFFFFu; // next 16 bits interpolate
    return (int32_t)(a + (((b - a) * (int64_t)frac) >> 16));
}

// ---- Test signals ----------------------------------------------------------------------------------

// Equal temperament, A4 = 440 Hz, rounded to the nearest millihertz.
static const uint32_t k_scale_mhz[8] = {261626u, 293665u, 329628u, 349228u, 391995u, 440000u, 493883u, 523251u};

uint32_t audio_scale_note_mhz(uint32_t index) {
    uint32_t n = index < 8u ? index : (14u - index); // 0..7 up, 8..14 -> 6..0 down
    return k_scale_mhz[n > 7u ? 7u : n];
}

static uint32_t phase_inc(uint32_t mhz, uint32_t rate) {
    return (uint32_t)((((uint64_t)mhz << 32) + (uint64_t)rate * 500u) / ((uint64_t)rate * 1000u));
}

static int32_t level(int32_t s) { return (int32_t)(((int64_t)s * AUDIO_TONE_LEVEL_Q31) >> 31); }

void tonegen_init(tonegen_t *g, tone_mode_t mode, uint32_t rate) {
    g->mode = mode;
    g->rate = rate;
    g->phase = 0;
    g->pos = 0;
    g->index = 0;
    g->switch_pending = false;
    uint32_t mhz = AUDIO_TONE_HZ * 1000u;
    if (mode == TONE_SCALE) {
        mhz = audio_scale_note_mhz(0);
    } else if (mode == TONE_IDENT) {
        mhz = AUDIO_IDENT_LEFT_HZ * 1000u;
    }
    g->inc = phase_inc(mhz, rate);
}

// Raised-cosine envelope value (Q31) for x = k / ramp in 0..1: sin^2(pi/2 * x).
static int32_t env(uint32_t k, uint32_t step) {
    int64_t s = audio_sine_q31(k * step); // k * step is at most 2^30: a quarter turn
    return (int32_t)((s * s) >> 31);
}

void tonegen_fill(tonegen_t *g, int32_t *out, uint32_t frames) {
    const uint32_t seg = (uint64_t)g->rate * AUDIO_IDENT_SEGMENT_MS / 1000u;
    const uint32_t note = (uint64_t)g->rate * AUDIO_SCALE_NOTE_MS / 1000u;
    const uint32_t ramp = (uint64_t)g->rate * AUDIO_SCALE_RAMP_MS / 1000u;
    const uint32_t rest = (uint64_t)g->rate * AUDIO_SCALE_REST_MS / 1000u;
    const uint32_t ramp_step = ramp != 0 ? (1u << 30) / ramp : 0;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t l = 0, r = 0;
        switch (g->mode) {
        case TONE_1K:
            l = r = level(audio_sine_q31(g->phase));
            g->phase += g->inc;
            break;
        case TONE_IDENT: {
            int32_t v = level(audio_sine_q31(g->phase));
            uint32_t next = g->phase + g->inc;
            if (g->index == 0) {
                l = v;
            } else {
                r = v;
            }
            if (++g->pos >= seg) {
                g->switch_pending = true;
            }
            if (g->switch_pending && next < g->phase) { // switch at the zero crossing (phase wrap): no click
                g->index ^= 1u;
                g->pos = 0;
                g->switch_pending = false;
                next = 0; // restart the new tone at phase 0 (the wrap left a small residue)
                g->inc = phase_inc((g->index == 0 ? AUDIO_IDENT_LEFT_HZ : AUDIO_IDENT_RIGHT_HZ) * 1000u, g->rate);
            }
            g->phase = next;
            break;
        }
        case TONE_SCALE:
            if (g->index >= AUDIO_SCALE_NOTES) { // rest between passes
                if (++g->pos >= rest) {
                    g->index = 0;
                    g->pos = 0;
                    g->phase = 0;
                    g->inc = phase_inc(audio_scale_note_mhz(0), g->rate);
                }
            } else {
                int32_t v = level(audio_sine_q31(g->phase));
                if (g->pos < ramp) {
                    v = (int32_t)(((int64_t)v * env(g->pos, ramp_step)) >> 31);
                } else if (g->pos >= note - ramp) {
                    v = (int32_t)(((int64_t)v * env(note - g->pos, ramp_step)) >> 31);
                }
                l = r = v;
                g->phase += g->inc;
                if (++g->pos >= note) {
                    g->pos = 0;
                    g->phase = 0;
                    g->index++;
                    if (g->index < AUDIO_SCALE_NOTES) {
                        g->inc = phase_inc(audio_scale_note_mhz(g->index), g->rate);
                    }
                }
            }
            break;
        case TONE_OFF:
            break;
        }
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}

// 32-bit left-justified stereo to 16-bit for the Bluetooth encoder (SBC carries 16 bit). With @p dither a triangular (TPDF) dither of +-1 LSB16 is
// added before the rounding, from a xorshift generator whose state is *rng (one draw gives both uniform halves). Without it the samples are
// only rounded, so 16-bit material at 0 dB reaches the encoder unchanged.
void NEUPOD_RAM_FUNC(audio_s32_to_s16)(const int32_t *in, int16_t *out, uint32_t frames, bool dither, uint32_t *rng) {
    uint32_t x = *rng != 0u ? *rng : 0x2545F491u;
    for (uint32_t i = 0; i < frames * 2u; i++) {
        int64_t v = in[i];
        if (dither) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            v += (int64_t)(int16_t)(x & 0xFFFFu) + (int64_t)(int16_t)(x >> 16); // two uniform +-0x8000: triangular, +-1 LSB16
        }
        v = (v + 0x8000) >> 16;
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    *rng = x;
}

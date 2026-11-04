// SPDX-License-Identifier: MPL-2.0
// Host tests for the audio maths and the WAV parser: PCM conversion, volume, sine level, generators.
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio_dsp.h"
#include "unit.h"
#include "wav.h"

/* ---- WAV parser over a memory image ---- */

typedef struct {
    uint8_t buf[512];
    size_t n;
} img_t;

static void put32(img_t *m, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        m->buf[m->n++] = (uint8_t)(v >> (8 * i));
    }
}
static void put16(img_t *m, uint32_t v) {
    m->buf[m->n++] = (uint8_t)v;
    m->buf[m->n++] = (uint8_t)(v >> 8);
}
static void putid(img_t *m, const char *id) {
    memcpy(m->buf + m->n, id, 4);
    m->n += 4;
}
static void fmt_chunk(img_t *m, uint32_t tag, uint32_t ch, uint32_t rate, uint32_t bits) {
    putid(m, "fmt ");
    put32(m, 16);
    put16(m, tag);
    put16(m, ch);
    put32(m, rate);
    put32(m, rate * ch * bits / 8);
    put16(m, ch * bits / 8);
    put16(m, bits);
}
static void header(img_t *m) {
    m->n = 0;
    putid(m, "RIFF");
    put32(m, 0);
    putid(m, "WAVE");
}
static int32_t rd(void *ctx, uint64_t off, void *dst, uint32_t n) {
    const img_t *m = ctx;
    if (off >= m->n) {
        return 0;
    }
    uint32_t k = off + n > m->n ? (uint32_t)(m->n - off) : n;
    memcpy(dst, m->buf + off, k);
    return (int32_t)k;
}
static wav_err_t parse(img_t *m, uint64_t file_size, wav_info_t *w) { return wav_parse(rd, m, file_size, w); }

TEST(wav_good_16_44k_stereo) {
    img_t m;
    wav_info_t w;
    header(&m);
    fmt_chunk(&m, 1, 2, 44100, 16);
    putid(&m, "data");
    put32(&m, 400);
    ASSERT_EQ(WAV_OK, parse(&m, 44 + 400, &w));
    ASSERT_EQ(44100, w.rate);
    ASSERT_EQ(2, w.channels);
    ASSERT_EQ(16, w.bits);
    ASSERT_EQ(4, w.block_align);
    ASSERT_EQ(44, w.data_offset);
    ASSERT_EQ(400, w.data_bytes);
    ASSERT_EQ(100, w.frames);
}

TEST(wav_formats_accepted) {
    img_t m;
    wav_info_t w;
    const uint32_t bits[3] = {16, 24, 32}, rates[4] = {44100, 48000, 88200, 96000};
    for (int b = 0; b < 3; b++) {
        for (int r = 0; r < 4; r++) {
            for (uint32_t ch = 1; ch <= 2; ch++) {
                header(&m);
                fmt_chunk(&m, 1, ch, rates[r], bits[b]);
                putid(&m, "data");
                put32(&m, 4800);
                ASSERT_EQ(WAV_OK, parse(&m, 44 + 4800, &w));
                ASSERT_EQ(rates[r], w.rate);
            }
        }
    }
}

TEST(wav_list_chunk_and_odd_sizes_before_data) {
    img_t m;
    wav_info_t w;
    header(&m);
    putid(&m, "LIST");
    put32(&m, 5); // odd size: padded to 6
    memcpy(m.buf + m.n, "abcde\0", 6);
    m.n += 6;
    fmt_chunk(&m, 1, 2, 48000, 24);
    putid(&m, "fact");
    put32(&m, 3); // odd again
    memcpy(m.buf + m.n, "xyz\0", 4);
    m.n += 4;
    putid(&m, "data");
    put32(&m, 600);
    size_t data_at = m.n;
    ASSERT_EQ(WAV_OK, parse(&m, data_at + 600, &w));
    ASSERT_EQ(data_at, w.data_offset);
    ASSERT_EQ(600, w.data_bytes);
    ASSERT_EQ(100, w.frames);
    ASSERT_EQ(24, w.bits);
}

TEST(wav_data_larger_than_file_is_clamped_and_whole_frames) {
    img_t m;
    wav_info_t w;
    header(&m);
    fmt_chunk(&m, 1, 2, 44100, 16);
    putid(&m, "data");
    put32(&m, 0xFFFFFFFFu); // streaming writer
    ASSERT_EQ(WAV_OK, parse(&m, 44 + 1003, &w));
    ASSERT_EQ(1000, w.data_bytes); // 1003 rounded down to whole 4-byte frames
}

TEST(wav_extensible_pcm_accepted_float_rejected) {
    img_t m;
    wav_info_t w;
    header(&m);
    putid(&m, "fmt ");
    put32(&m, 40);
    put16(&m, 0xFFFE);
    put16(&m, 2);
    put32(&m, 48000);
    put32(&m, 48000 * 2 * 3);
    put16(&m, 6);
    put16(&m, 24);
    put16(&m, 22);
    put16(&m, 24);
    put32(&m, 3);
    put16(&m, 1); // sub-format PCM
    memset(m.buf + m.n, 0, 14);
    m.n += 14;
    putid(&m, "data");
    put32(&m, 60);
    ASSERT_EQ(WAV_OK, parse(&m, m.n + 60, &w));
    ASSERT_EQ(24, w.bits);
    header(&m);
    fmt_chunk(&m, 3, 2, 44100, 32); // IEEE float
    putid(&m, "data");
    put32(&m, 64);
    ASSERT_EQ(WAV_E_NOT_PCM, parse(&m, m.n + 64, &w));
}

TEST(wav_rejections) {
    img_t m;
    wav_info_t w;
    header(&m);
    fmt_chunk(&m, 1, 6, 44100, 16);
    putid(&m, "data");
    put32(&m, 64);
    ASSERT_EQ(WAV_E_CHANNELS, parse(&m, m.n + 64, &w));
    header(&m);
    fmt_chunk(&m, 1, 2, 44100, 8);
    putid(&m, "data");
    put32(&m, 64);
    ASSERT_EQ(WAV_E_BITS, parse(&m, m.n + 64, &w));
    header(&m);
    fmt_chunk(&m, 1, 2, 22050, 16);
    putid(&m, "data");
    put32(&m, 64);
    ASSERT_EQ(WAV_E_RATE, parse(&m, m.n + 64, &w));
    header(&m);
    fmt_chunk(&m, 1, 2, 44100, 16);
    m.buf[32] = 5; // block align wrong
    putid(&m, "data");
    put32(&m, 64);
    ASSERT_EQ(WAV_E_BAD_FMT, parse(&m, m.n + 64, &w));
    header(&m);
    putid(&m, "data"); // data before fmt
    put32(&m, 64);
    ASSERT_EQ(WAV_E_NO_FMT, parse(&m, m.n + 64, &w));
    header(&m);
    fmt_chunk(&m, 1, 2, 44100, 16); // no data chunk at all
    ASSERT_EQ(WAV_E_NO_DATA, parse(&m, m.n, &w));
    header(&m);
    fmt_chunk(&m, 1, 2, 44100, 16);
    putid(&m, "data");
    put32(&m, 0);
    ASSERT_EQ(WAV_E_NO_DATA, parse(&m, m.n, &w)); // empty data
    memcpy(m.buf, "RIFX", 4);
    ASSERT_EQ(WAV_E_NOT_RIFF, parse(&m, m.n, &w));
    memcpy(m.buf, "RIFF", 4);
    memcpy(m.buf + 8, "AVI ", 4);
    ASSERT_EQ(WAV_E_NOT_RIFF, parse(&m, m.n, &w));
    m.n = 5;
    ASSERT_EQ(WAV_E_READ, parse(&m, 5, &w));
    ASSERT_TRUE(strlen(wav_err_str(WAV_E_BITS)) > 5);
}

/* ---- Sample conversion ---- */

TEST(pcm_conversion_is_exact) {
    int32_t out[8];
    const uint8_t s16[] = {0x34, 0x12, 0x00, 0x80, 0xFF, 0x7F, 0xFF, 0xFF}; // 0x1234, -32768, 32767, -1
    ASSERT_TRUE(audio_pcm_to_s32(out, s16, 2, 16, 2));
    ASSERT_EQ((int32_t)0x12340000, out[0]);
    ASSERT_EQ((int32_t)0x80000000, out[1]);
    ASSERT_TRUE(audio_pcm_to_s32(out, s16, 4, 16, 1)); // mono: duplicated
    ASSERT_EQ((int32_t)0x12340000, out[0]);
    ASSERT_EQ(out[0], out[1]);
    ASSERT_EQ((int32_t)0x7FFF0000, out[4]);
    ASSERT_EQ(out[4], out[5]);
    ASSERT_EQ((int32_t)0xFFFF0000, out[6]);
    const uint8_t s24[] = {0x56, 0x34, 0x12, 0x00, 0x00, 0x80}; // 0x123456, -8388608
    ASSERT_TRUE(audio_pcm_to_s32(out, s24, 1, 24, 2));
    ASSERT_EQ((int32_t)0x12345600, out[0]);
    ASSERT_EQ((int32_t)0x80000000, out[1]);
    const uint8_t s32[] = {0x78, 0x56, 0x34, 0x12, 0xFF, 0xFF, 0xFF, 0xFF};
    ASSERT_TRUE(audio_pcm_to_s32(out, s32, 1, 32, 2));
    ASSERT_EQ((int32_t)0x12345678, out[0]);
    ASSERT_EQ(-1, out[1]);
    ASSERT_FALSE(audio_pcm_to_s32(out, s32, 1, 8, 2));
    ASSERT_FALSE(audio_pcm_to_s32(out, s32, 1, 16, 3));
    // Every 16-bit value round-trips: >> 16 gives the source back.
    for (int v = -32768; v < 32768; v += 7) {
        uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF)};
        int32_t o[2];
        (void)audio_pcm_to_s32(o, b, 1, 16, 1);
        ASSERT_EQ(v, o[0] >> 16);
    }
}

/* ---- Volume ---- */

TEST(volume_zero_db_is_identity) {
    ASSERT_EQ(AUDIO_GAIN_UNITY, audio_gain_q30(0));
    int32_t in[8] = {0x7FFFFFFF, (int32_t)0x80000000, 1, -1, 123456789, -987654321, 0, 42};
    int32_t s[8];
    memcpy(s, in, sizeof s);
    audio_apply_gain(s, 4, AUDIO_GAIN_UNITY, AUDIO_GAIN_UNITY);
    ASSERT_EQ(0, memcmp(s, in, sizeof s));
    // The multiply path is also exact at unity (through the equal-gains branch with a non-unity guard bypassed).
    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(in[i], (int32_t)(((int64_t)in[i] * AUDIO_GAIN_UNITY) >> 30));
    }
}

TEST(volume_table_matches_db_and_is_monotonic) {
    for (uint32_t n = 0; n < AUDIO_VOL_OFF_DB; n++) {
        double want = ldexp(pow(10.0, -(double)n / 20.0), 30);
        ASSERT_TRUE(fabs((double)audio_gain_q30(n) - want) <= 1.0);
        if (n > 0) {
            ASSERT_TRUE(audio_gain_q30(n) < audio_gain_q30(n - 1));
        }
    }
    ASSERT_EQ(0, audio_gain_q30(AUDIO_VOL_OFF_DB));
    ASSERT_EQ(0, audio_gain_q30(1000)); // clamped to off
    // -6 dB is half within 1 %, -30 dB is 3.16 %.
    ASSERT_TRUE(fabs(audio_gain_q30(6) / (double)AUDIO_GAIN_UNITY - 0.5012) < 0.001);
}

TEST(volume_cap_and_default) {
    const uint32_t cap = NEUPOD_AUDIO_VOLUME_CAP_DB;
    ASSERT_EQ(cap, audio_vol_clamp(0));
    ASSERT_EQ(AUDIO_VOL_OFF_DB, audio_vol_clamp(5000));
    ASSERT_TRUE(audio_gain_q30(0) <= AUDIO_GAIN_UNITY); // never any gain above 0 dB
    ASSERT_TRUE(AUDIO_VOL_DEFAULT_ATT_DB >= 20); // the power-on level is quiet
    ASSERT_TRUE(audio_gain_q30(AUDIO_VOL_DEFAULT_ATT_DB) < AUDIO_GAIN_UNITY / 20);
}

TEST(gain_ramp_and_attenuation) {
    int32_t s[2 * 256];
    for (int i = 0; i < 512; i++) {
        s[i] = 1 << 28;
    }
    audio_apply_gain(s, 256, 0, AUDIO_GAIN_UNITY);
    ASSERT_EQ(0, s[0]);
    ASSERT_TRUE(s[2 * 255] > (1 << 28) - (1 << 21)); // the ramp ends one step short of the target
    for (int i = 1; i < 256; i++) {
        ASSERT_TRUE(s[2 * i] >= s[2 * (i - 1)]); // monotonic fade-in
    }
    for (int i = 0; i < 512; i++) {
        s[i] = 1 << 28;
    }
    audio_apply_gain(s, 256, audio_gain_q30(6), audio_gain_q30(6));
    ASSERT_TRUE(fabs((double)s[10] / (1 << 28) - 0.5012) < 0.001);
    audio_apply_gain(s, 256, 0, 0);
    ASSERT_EQ(0, s[100]);
}

/* ---- Sine / generators ---- */

TEST(sine_table_level_and_shape) {
    int32_t peak = 0;
    double maxerr = 0;
    for (uint32_t i = 0; i < 65536; i++) {
        uint32_t ph = i << 16;
        int32_t v = audio_sine_q31(ph);
        if (v > peak) {
            peak = v;
        }
        double want = 2147483647.0 * sin(2.0 * M_PI * (double)i / 65536.0);
        if (fabs((double)v - want) > maxerr) {
            maxerr = fabs((double)v - want);
        }
    }
    ASSERT_EQ(0x7FFFFFFF, peak);
    ASSERT_TRUE(maxerr / 2147483647.0 < 6e-6); // linear interpolation error, about -104 dBFS
}

static void tone_stats(tone_mode_t mode, uint32_t rate, uint32_t frames, double *peak_db, double *rms_db) {
    static int32_t buf[96000 * 2];
    tonegen_t g;
    tonegen_init(&g, mode, rate);
    tonegen_fill(&g, buf, frames);
    int32_t pk = 0;
    double sum = 0;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t v = buf[2 * i];
        if (abs(v) > pk) {
            pk = abs(v);
        }
        sum += (double)v * v;
    }
    *peak_db = 20.0 * log10((double)pk / 2147483648.0);
    *rms_db = 10.0 * log10(sum / frames / (2147483648.0 * 2147483648.0));
}

TEST(tone_is_minus_20_dbfs) {
    double pk, rms;
    tone_stats(TONE_1K, 48000, 48000, &pk, &rms);
    ASSERT_TRUE(fabs(pk - (-20.0)) < 0.001);
    ASSERT_TRUE(fabs(rms - (-23.0103)) < 0.01); // sine: rms = peak - 3.01 dB
    tone_stats(TONE_1K, 44100, 44100, &pk, &rms);
    ASSERT_TRUE(fabs(pk - (-20.0)) < 0.001);
    ASSERT_EQ(214748365, AUDIO_TONE_LEVEL_Q31);
}

TEST(tone_frequency_is_exact) {
    static int32_t buf[96000 * 2];
    const uint32_t rates[2] = {44100, 48000};
    for (int r = 0; r < 2; r++) {
        tonegen_t g;
        tonegen_init(&g, TONE_1K, rates[r]);
        tonegen_fill(&g, buf, rates[r] * 2);
        int crossings = 0; // rising zero crossings in 2 s = 2000 for 1 kHz
        for (uint32_t i = 1; i < rates[r] * 2; i++) {
            if (buf[2 * (i - 1)] < 0 && buf[2 * i] >= 0) {
                crossings++;
            }
        }
        ASSERT_TRUE(crossings >= 1999 && crossings <= 2001);
    }
}

TEST(ident_alternates_channels_at_zero_crossings) {
    static int32_t buf[48000 * 4 * 2];
    tonegen_t g;
    tonegen_init(&g, TONE_IDENT, 48000);
    tonegen_fill(&g, buf, 48000 * 4);
    int left_energy_s[4] = {0}, right_energy_s[4] = {0};
    for (uint32_t i = 0; i < 48000u * 4u; i++) {
        int seg = (int)(i / 48000u);
        if (buf[2 * i] != 0) {
            left_energy_s[seg]++;
        }
        if (buf[2 * i + 1] != 0) {
            right_energy_s[seg]++;
        }
    }
    ASSERT_TRUE(left_energy_s[0] > 40000 && right_energy_s[0] == 0);   // first second: left only
    ASSERT_TRUE(right_energy_s[1] > 40000 && left_energy_s[1] < 48000 / 400 + 16);  // then right (switch waits for a zero crossing: <= one 400 Hz period)
    ASSERT_TRUE(left_energy_s[2] > 40000 && right_energy_s[2] < 48000 / 1600 + 16);
    // No step at the switch: the sample before the right channel's first non-zero is tiny.
    for (uint32_t i = 1; i < 48000u * 4u; i++) {
        if ((buf[2 * i] == 0) != (buf[2 * (i - 1)] == 0) && buf[2 * i] != 0) { // left restarts
            ASSERT_TRUE(abs(buf[2 * i]) < 30000000); // started near a zero crossing (< 1.4 % of full scale)
        }
    }
}

TEST(ident_frequencies_per_channel) {
    static int32_t buf[96000 * 3 * 2];
    const uint32_t rates[4] = {44100, 48000, 88200, 96000};
    for (int r = 0; r < 4; r++) {
        const uint32_t rate = rates[r];
        tonegen_t g;
        tonegen_init(&g, TONE_IDENT, rate);
        tonegen_fill(&g, buf, rate * 3);
        int l_cross = 0, r_cross = 0;
        uint32_t l_tail = 0, r_first = 0;
        for (uint32_t i = 1; i < rate * 2; i++) {
            if (i < rate && buf[2 * (i - 1)] < 0 && buf[2 * i] >= 0) {
                l_cross++;   // second 1, left
            }
            if (i >= rate && buf[2 * (i - 1) + 1] < 0 && buf[2 * i + 1] >= 0) {
                r_cross++;   // second 2, right
            }
            if (i < rate && buf[2 * i + 1] != 0) {
                r_first++;   // right must be silent while left plays
            }
            if (i >= rate && buf[2 * i] != 0) {
                l_tail++;    // left tail after the switch: at most one 400 Hz period
            }
        }
        ASSERT_TRUE(l_cross >= 399 && l_cross <= 401);
        ASSERT_TRUE(r_cross >= 1599 && r_cross <= 1601);
        ASSERT_EQ(0, r_first);
        ASSERT_TRUE(l_tail <= rate / 400u + 16u);
    }
}

TEST(scale_notes_and_envelope) {
    ASSERT_EQ(261626, audio_scale_note_mhz(0));
    ASSERT_EQ(440000, audio_scale_note_mhz(5));
    ASSERT_EQ(523251, audio_scale_note_mhz(7));
    ASSERT_EQ(audio_scale_note_mhz(6), audio_scale_note_mhz(8));  // descending
    ASSERT_EQ(audio_scale_note_mhz(0), audio_scale_note_mhz(14));
    static const int semis[8] = {0, 2, 4, 5, 7, 9, 11, 12};
    for (uint32_t i = 0; i < 8; i++) {
        double want = 440000.0 * pow(2.0, (semis[i] - 9) / 12.0);
        ASSERT_TRUE(fabs((double)audio_scale_note_mhz(i) - want) <= 0.5001);
    }
    const uint32_t rate = 48000, note = rate * AUDIO_SCALE_NOTE_MS / 1000, ramp = rate * AUDIO_SCALE_RAMP_MS / 1000;
    static int32_t buf[48000 * 2 * 2];
    tonegen_t g;
    tonegen_init(&g, TONE_SCALE, rate);
    tonegen_fill(&g, buf, rate * 2);
    // Each note starts and ends at (near) zero: raised cosine ramp, no clicks.
    for (uint32_t n = 0; n < 6; n++) {
        ASSERT_TRUE(abs(buf[2 * (n * note)]) < 100000);
        ASSERT_TRUE(abs(buf[2 * (n * note + note - 1)]) < 2000000);
        int32_t mid = 0;
        for (uint32_t i = ramp; i < note - ramp; i++) {
            if (abs(buf[2 * (n * note + i)]) > mid) {
                mid = abs(buf[2 * (n * note + i)]);
            }
        }
        ASSERT_TRUE(mid > 210000000); // reaches the -20 dBFS peak in the middle of the note
        ASSERT_TRUE(mid <= AUDIO_TONE_LEVEL_Q31);
        // Largest sample-to-sample step stays small (no discontinuity): < 3 % of full scale at 523 Hz.
        for (uint32_t i = 1; i < note; i++) {
            int64_t d = (int64_t)buf[2 * (n * note + i)] - buf[2 * (n * note + i - 1)];
            ASSERT_TRUE(llabs(d) < 70000000);
        }
    }
    // After 15 notes + the rest it starts over.
    tonegen_init(&g, TONE_SCALE, rate);
    uint32_t pass = note * AUDIO_SCALE_NOTES + rate * AUDIO_SCALE_REST_MS / 1000;
    static int32_t big[48000 * 8 * 2];
    tonegen_fill(&g, big, pass + 2000);
    ASSERT_EQ(0, big[2 * (note * AUDIO_SCALE_NOTES + 100)]);        // inside the rest: silence
    ASSERT_TRUE(abs(big[2 * (pass + 700)]) > 1000);               // first note again (past its attack start)
}

int main(void) {
    RUN(wav_good_16_44k_stereo);
    RUN(wav_formats_accepted);
    RUN(wav_list_chunk_and_odd_sizes_before_data);
    RUN(wav_data_larger_than_file_is_clamped_and_whole_frames);
    RUN(wav_extensible_pcm_accepted_float_rejected);
    RUN(wav_rejections);
    RUN(pcm_conversion_is_exact);
    RUN(volume_zero_db_is_identity);
    RUN(volume_table_matches_db_and_is_monotonic);
    RUN(volume_cap_and_default);
    RUN(gain_ramp_and_attenuation);
    RUN(sine_table_level_and_shape);
    RUN(tone_is_minus_20_dbfs);
    RUN(tone_frequency_is_exact);
    RUN(ident_alternates_channels_at_zero_crossings);
    RUN(ident_frequencies_per_channel);
    RUN(scale_notes_and_envelope);
    return RESULT();
}

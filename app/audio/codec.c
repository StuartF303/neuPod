// SPDX-License-Identifier: MPL-2.0
/*
 * Decoders on the bare-metal audio core (core 1). CORE-1 RULES apply (ADR-0011, checked by
 * cmake/check_core1_rules.cmake and tools/check_core1_closure.py): no malloc, stdio, RTOS. The vendored decoders get
 * their memory from the arena through codec_alloc.h.
 */
#include <string.h>

#include "alac_dec.h"
#include "arena.h"
#include "audio_dsp.h"
#include "codec.h"
#include "codec_dr_config.h"
#include "../third_party/dr_libs/dr_flac.h"
#include "../third_party/dr_libs/dr_mp3.h"

static uint8_t g_store[CODEC_ARENA_BYTES] __attribute__((aligned(8)));
static arena_t g_arena = {g_store, sizeof g_store, 0, 0, 0};

void *codec_malloc(size_t n) { return arena_alloc(&g_arena, n); }
void *codec_realloc(void *p, size_t n) { return arena_realloc(&g_arena, p, n); }
void codec_free(void *p) { arena_free(&g_arena, p); }
size_t codec_arena_high_water(void) { return g_arena.high; }
size_t codec_arena_failures(void) { return g_arena.failures; }

void codec_md5_pcm(md5_t *m, const int32_t *s, uint32_t frames, uint32_t channels, uint32_t bits) {
    const uint32_t bytes = (bits + 7u) / 8u, shift = 32u - bits;
    uint8_t buf[192];
    uint32_t n = 0;
    for (uint32_t i = 0; i < frames * channels; i++) {
        int32_t v = s[i] >> shift; // right-justified, sign-extended
        for (uint32_t b = 0; b < bytes; b++) {
            buf[n++] = (uint8_t)((uint32_t)v >> (8u * b));
        }
        if (n + 4u > sizeof buf) {
            md5_update(m, buf, n);
            n = 0;
        }
    }
    if (n != 0) {
        md5_update(m, buf, n);
    }
}

// ---- dr_libs I/O adapters --------------------------------------------------------------------------------

static size_t on_read(void *ud, void *dst, size_t n) {
    codec_t *c = ud;
    return c->io.read(c->io.ctx, dst, n);
}
static drflac_bool32 flac_seek(void *ud, int off, drflac_seek_origin o) {
    codec_t *c = ud;
    return c->io.seek(c->io.ctx, off, o == DRFLAC_SEEK_SET ? 0 : 1) ? DRFLAC_TRUE : DRFLAC_FALSE;
}
static drmp3_bool32 mp3_seek(void *ud, int off, drmp3_seek_origin o) {
    codec_t *c = ud;
    return c->io.seek(c->io.ctx, off, o == DRMP3_SEEK_SET ? 0 : 1) ? DRMP3_TRUE : DRMP3_FALSE;
}

// ---- open / close ------------------------------------------------------------------------------------------

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

codec_err_t codec_open(codec_t *c, const codec_params_t *p, const codec_io_t *io) {
    memset(c, 0, sizeof *c);
    arena_reset(&g_arena);
    g_arena.failures = 0;
    c->io = *io;
    c->kind = p->kind;
    c->verify = p->verify && p->kind == CODEC_FLAC;
    c->total_frames = p->total_frames;
    c->skip_left = p->kind == CODEC_ALAC ? p->skip_frames : 0u; // MP3: dr_mp3 trims; FLAC: exact already
    c->limit = p->trim_exact && p->kind == CODEC_ALAC ? p->total_frames : 0u;
    if (c->verify) {
        md5_init(&c->md5);
    }
    if (p->kind == CODEC_FLAC) {
        drflac *f = drflac_open(on_read, flac_seek, NULL, c, NULL);
        if (f == NULL) {
            return g_arena.failures ? CODEC_ERR_MEM : CODEC_ERR_OPEN;
        }
        c->impl = f;
        c->channels = f->channels;
        c->bits = f->bitsPerSample;
        if (f->channels < 1 || f->channels > 2 || f->channels != p->channels || f->bitsPerSample != p->bits ||
            f->sampleRate != p->rate) {
            return CODEC_ERR_FORMAT;
        }
        return CODEC_OK;
    }
    if (p->kind == CODEC_MP3) {
        drmp3 *m = arena_alloc(&g_arena, sizeof *m);
        if (m == NULL) {
            return CODEC_ERR_MEM;
        }
        memset(m, 0, sizeof *m);
        if (!drmp3_init(m, on_read, mp3_seek, NULL, NULL, c, NULL)) {
            return g_arena.failures ? CODEC_ERR_MEM : CODEC_ERR_OPEN;
        }
        c->impl = m;
        c->channels = m->channels;
        c->bits = 32;
        if (m->channels < 1 || m->channels > 2 || m->sampleRate != p->rate) {
            return CODEC_ERR_FORMAT;
        }
        return CODEC_OK;
    }
    if (p->kind == CODEC_ALAC) {
        alac_dec_t *a = arena_alloc(&g_arena, sizeof *a);
        if (a == NULL) {
            return CODEC_ERR_MEM;
        }
        int32_t st = alac_dec_init(a, p->cookie, p->cookie_len, codec_malloc);
        if (st != 0) {
            return st == kALAC_MemFullError ? CODEC_ERR_MEM : CODEC_ERR_FORMAT;
        }
        c->alac = a;
        c->channels = a->cfg.numChannels;
        c->bits = a->cfg.bitDepth;
        if (c->channels != p->channels || c->bits != p->bits || a->cfg.sampleRate != p->rate) {
            return CODEC_ERR_FORMAT;
        }
        c->out_bps = alac_dec_out_bytes_per_frame(a);
        c->frame_cap = a->cfg.frameLength * c->channels * ((c->bits + 7u) / 8u) + 64u;
        c->frame_buf = arena_alloc(&g_arena, c->frame_cap);
        c->out_buf = arena_alloc(&g_arena, (size_t)a->cfg.frameLength * c->out_bps);
        if (c->frame_buf == NULL || c->out_buf == NULL) {
            return CODEC_ERR_MEM;
        }
        return CODEC_OK;
    }
    return CODEC_ERR_FORMAT;
}

void codec_close(codec_t *c) {
    // The arena is wholesale-reset by the next codec_open(); nothing to free one by one.
    memset(c, 0, sizeof *c);
}

void codec_md5_final(codec_t *c, uint8_t out[16]) { md5_final(&c->md5, out); }

// ---- decoding ----------------------------------------------------------------------------------------------

static void expand_mono(int32_t *out, uint32_t n) {
    for (uint32_t i = n; i-- > 0;) {
        int32_t v = out[i];
        out[2u * i] = v;
        out[2u * i + 1u] = v;
    }
}

static int32_t float_to_s32(float f) {
    if (f >= 1.0f) return 0x7FFFFFFF;
    if (f <= -1.0f) return (int32_t)0x80000000;
    float v = f * 2147483648.0f;
    return (int32_t)(v >= 0.0f ? v + 0.5f : v - 0.5f); // nearest, halves away from zero; |v| < 2^31 here
}

static int32_t read_raw(codec_t *c, int32_t *out, uint32_t max_frames);

// Zero frames means the end of the stream, an abandoned read (not our business), or a failure that looks like an end:
// dr_flac and dr_mp3 return 0 on damaged data and on arena exhaustion, so those are told apart here.
int32_t codec_read(codec_t *c, int32_t *out, uint32_t max_frames) {
    if (c->limit != 0 && c->frames_out >= c->limit) {
        return 0; // the trimmed end (encoder padding follows in the stream)
    }
    int32_t n;
    for (;;) {
        n = read_raw(c, out, max_frames);
        if (n <= 0 || c->skip_left == 0) {
            break;
        }
        uint32_t k = (uint32_t)n < c->skip_left ? (uint32_t)n : c->skip_left; // encoder priming: not part of the track
        c->skip_left -= k;
        if (k == (uint32_t)n) {
            continue;
        }
        memmove(out, out + 2u * k, (size_t)(n - (int32_t)k) * 2u * sizeof(int32_t));
        n -= (int32_t)k;
        break;
    }
    if (n > 0) {
        if (c->limit != 0 && c->frames_out + (uint32_t)n > c->limit) {
            n = (int32_t)(c->limit - c->frames_out);
        }
        c->frames_out += (uint32_t)n;
        return n;
    }
    if (n < 0) {
        return n;
    }
    if (c->io.aborted != NULL && c->io.aborted(c->io.ctx)) {
        return 0;
    }
    if (g_arena.failures != 0) {
        return -CODEC_ERR_MEM;
    }
    // ALAC may end a packet or two short (encoder priming); FLAC announces its length exactly
    uint64_t slack = c->kind == CODEC_ALAC ? 8192u : 0u;
    if (c->total_frames != 0 && c->kind != CODEC_MP3 && c->frames_out + slack < c->total_frames) {
        return -CODEC_ERR_TRUNCATED;
    }
    return 0;
}

static int32_t read_raw(codec_t *c, int32_t *out, uint32_t max_frames) {
    if (max_frames > 512u) {
        max_frames = 512u;
    }
    if (c->kind == CODEC_FLAC) {
        uint32_t n = (uint32_t)drflac_read_pcm_frames_s32((drflac *)c->impl, max_frames, (drflac_int32 *)out);
        if (c->verify && n != 0) {
            codec_md5_pcm(&c->md5, out, n, c->channels, c->bits);
        }
        if (c->channels == 1) {
            expand_mono(out, n);
        }
        return (int32_t)n;
    }
    if (c->kind == CODEC_MP3) {
        float tmp[128 * 2]; // small: this runs on core 1's stack next to minimp3's own scratch
        if (max_frames > 128u) {
            max_frames = 128u;
        }
        uint32_t n = (uint32_t)drmp3_read_pcm_frames_f32((drmp3 *)c->impl, max_frames, tmp);
        for (uint32_t i = 0; i < n; i++) {
            if (c->channels == 1) {
                out[2u * i] = out[2u * i + 1u] = float_to_s32(tmp[i]);
            } else {
                out[2u * i] = float_to_s32(tmp[2u * i]);
                out[2u * i + 1u] = float_to_s32(tmp[2u * i + 1u]);
            }
        }
        return (int32_t)n;
    }
    if (c->kind == CODEC_ALAC) {
        alac_dec_t *a = c->alac;
        while (c->out_pos >= c->out_frames) { // fetch and decode the next packet
            uint8_t lb[4];
            if (c->io.read(c->io.ctx, lb, 4) != 4) {
                return 0;
            }
            uint32_t len = be32(lb);
            if (len == 0 || len > c->frame_cap) {
                return -CODEC_ERR_DATA;
            }
            if (c->io.read(c->io.ctx, c->frame_buf, len) != len) {
                return 0;
            }
            uint32_t frames = 0;
            if (alac_dec_decode(a, c->frame_buf, len, c->out_buf, &frames) != 0 || frames > a->cfg.frameLength) {
                return -CODEC_ERR_DATA;
            }
            c->out_frames = frames;
            c->out_pos = 0;
        }
        uint32_t n = c->out_frames - c->out_pos;
        if (n > max_frames) {
            n = max_frames;
        }
        (void)audio_pcm_to_s32(out, c->out_buf + (size_t)c->out_pos * c->out_bps, n, c->bits == 16 ? 16 : c->bits == 32 ? 32 : 24,
                               (uint8_t)c->channels);
        c->out_pos += n;
        return (int32_t)n;
    }
    return -CODEC_ERR_FORMAT;
}

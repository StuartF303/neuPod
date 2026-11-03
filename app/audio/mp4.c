// SPDX-License-Identifier: MPL-2.0
// Minimal MP4/M4A demuxer: the moov sample tables, the ALAC magic cookie, ilst tags. AAC is recognised (so the UI
// can say it is unsupported) but not decoded; the packet iterator is codec-independent so AAC can use it later.
#include <string.h>

#include "meta.h"

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }

typedef struct {
    meta_read_fn rd;
    void *ctx;
    uint64_t file_size;
    uint32_t boxes; // budget: a crafted file cannot make us walk forever
    mp4_info_t *out;
    bool in_audio_trak; // currently inside the first 'soun' trak
    bool got_audio, trak_done;
    bool aac, other;
    uint32_t timescale;
    uint64_t duration;
    int err;
} pctx_t;

typedef struct {
    uint32_t type;
    uint64_t body, end; // payload range
} box_t;

static bool read_box(pctx_t *p, uint64_t pos, uint64_t limit, box_t *b) {
    uint8_t h[16];
    if (pos + 8 > limit || p->rd(p->ctx, pos, h, 8) != 8) return false;
    uint64_t size = be32(h);
    b->type = be32(h + 4);
    uint32_t hl = 8;
    if (size == 1) {
        if (p->rd(p->ctx, pos + 8, h + 8, 8) != 8) return false;
        size = be64(h + 8);
        hl = 16;
    } else if (size == 0) {
        size = limit - pos;
    }
    if (size < hl || size > limit - pos) return false; // limit >= pos here; written so a 64-bit size cannot wrap
    b->body = pos + hl;
    b->end = pos + size;
    return true;
}

static void ilst_item(pctx_t *p, const box_t *it) {
    box_t d;
    if (!read_box(p, it->body, it->end, &d) || d.type != FOURCC('d', 'a', 't', 'a') || d.end - d.body < 8) return;
    uint32_t n = (uint32_t)(d.end - d.body - 8);
    uint8_t v[96];
    uint8_t flags[4];
    if (n > sizeof v || p->rd(p->ctx, d.body, flags, 4) != 4 || p->rd(p->ctx, d.body + 8, v, n) != (int32_t)n) return;
    audio_tags_t *t = &p->out->tags;
    if (it->type == FOURCC(0xA9, 'n', 'a', 'm')) meta_text_to_utf8(t->title, sizeof t->title, v, n, 3);
    else if (it->type == FOURCC(0xA9, 'A', 'R', 'T')) meta_text_to_utf8(t->artist, sizeof t->artist, v, n, 3);
    else if (it->type == FOURCC(0xA9, 'a', 'l', 'b')) meta_text_to_utf8(t->album, sizeof t->album, v, n, 3);
    else if (it->type == FOURCC('t', 'r', 'k', 'n') && n >= 4) t->track = (uint16_t)be16(v + 2);
}

static void walk(pctx_t *p, uint64_t pos, uint64_t end, int depth, uint32_t parent);

static void leaf(pctx_t *p, const box_t *b, uint32_t parent) {
    mp4_info_t *o = p->out;
    uint8_t h[40];
    uint64_t len = b->end - b->body;
    if (parent == FOURCC('i', 'l', 's', 't')) {
        ilst_item(p, b);
        return;
    }
    if (b->type == FOURCC('m', 'd', 'h', 'd') && !p->got_audio) { // before 'hdlr' in the file: keep the latest trak's
        if (len >= 24 && p->rd(p->ctx, b->body, h, 32) >= 24) {
            if (h[0] == 1 && len >= 32) { p->timescale = be32(h + 20); p->duration = be64(h + 24); }
            else { p->timescale = be32(h + 12); p->duration = be32(h + 16); }
        }
        return;
    }
    if (!p->in_audio_trak) return;
    switch (b->type) {
    case FOURCC('s', 't', 's', 'd'): {
        // [ver/flags 4][count 4] then the first sample entry: size, format, 6 reserved, dref index, then audio fields
        uint8_t e[44];
        if (len < 8 + 36 || p->rd(p->ctx, b->body + 8, e, 36) != 36) break;
        uint32_t fmt = be32(e + 4);
        uint32_t esize = be32(e);
        uint32_t ver = be16(e + 16);
        uint64_t child = b->body + 8 + 36 + (ver == 1 ? 16u : 0u);
        if (fmt == FOURCC('m', 'p', '4', 'a')) { p->aac = true; break; }
        if (fmt != FOURCC('a', 'l', 'a', 'c')) { p->other = true; break; }
        o->channels = (uint8_t)be16(e + 24);
        o->bits = (uint8_t)be16(e + 26);
        o->rate = be32(e + 32) >> 16;
        uint64_t eend = b->body + 8 + esize;
        box_t c;
        if (read_box(p, child, eend < b->end ? eend : b->end, &c) && c.type == FOURCC('a', 'l', 'a', 'c')) {
            uint64_t cl = c.end - c.body;
            if (cl >= 4 + 24) {
                uint32_t n = (uint32_t)(cl - 4) < MP4_COOKIE_MAX ? (uint32_t)(cl - 4) : MP4_COOKIE_MAX;
                if (p->rd(p->ctx, c.body + 4, o->cookie, n) == (int32_t)n) {
                    o->cookie_len = n;
                    o->is_alac = true;
                }
            }
        }
        break;
    }
    case FOURCC('s', 't', 's', 'z'):
        if (len >= 12 && p->rd(p->ctx, b->body, h, 12) == 12) {
            o->stsz_fixed = be32(h + 4);
            o->sample_count = be32(h + 8);
            o->stsz_off = b->body + 12;
            if (o->stsz_fixed == 0 && (uint64_t)o->sample_count * 4 > len - 12) o->sample_count = 0;
        }
        break;
    case FOURCC('s', 't', 's', 'c'):
        if (len >= 8 && p->rd(p->ctx, b->body, h, 8) == 8) {
            o->stsc_count = be32(h + 4);
            o->stsc_off = b->body + 8;
            if ((uint64_t)o->stsc_count * 12 > len - 8) o->stsc_count = 0;
        }
        break;
    case FOURCC('s', 't', 'c', 'o'):
    case FOURCC('c', 'o', '6', '4'):
        if (len >= 8 && p->rd(p->ctx, b->body, h, 8) == 8) {
            o->co64 = b->type == FOURCC('c', 'o', '6', '4');
            o->stco_count = be32(h + 4);
            o->stco_off = b->body + 8;
            if ((uint64_t)o->stco_count * (o->co64 ? 8u : 4u) > len - 8) o->stco_count = 0;
        }
        break;
    default: break;
    }
}

static void walk(pctx_t *p, uint64_t pos, uint64_t end, int depth, uint32_t parent) {
    box_t b;
    while (p->boxes < 20000u && read_box(p, pos, end, &b)) {
        p->boxes++;
        switch (b.type) {
        case FOURCC('m', 'o', 'o', 'v'):
        case FOURCC('m', 'd', 'i', 'a'):
        case FOURCC('m', 'i', 'n', 'f'):
        case FOURCC('s', 't', 'b', 'l'):
        case FOURCC('u', 'd', 't', 'a'):
        case FOURCC('i', 'l', 's', 't'):
            if (depth < 8) walk(p, b.body, b.end, depth + 1, b.type);
            break;
        case FOURCC('m', 'e', 't', 'a'): // full box: 4 bytes of version/flags before the children
            if (depth < 8) walk(p, b.body + 4, b.end, depth + 1, b.type);
            break;
        case FOURCC('t', 'r', 'a', 'k'):
            if (!p->got_audio && !p->trak_done) {
                p->in_audio_trak = false;
                walk(p, b.body, b.end, depth + 1, b.type);
                if (p->in_audio_trak) p->got_audio = true;
                p->in_audio_trak = false;
            }
            break;
        case FOURCC('h', 'd', 'l', 'r'): {
            uint8_t h[12];
            if (b.end - b.body >= 12 && p->rd(p->ctx, b.body, h, 12) == 12 && be32(h + 8) == FOURCC('s', 'o', 'u', 'n')) {
                p->in_audio_trak = true;
            }
            break;
        }
        default:
            leaf(p, &b, parent);
            break;
        }
        pos = b.end;
    }
}

meta_err_t mp4_parse(meta_read_fn rd, void *ctx, uint64_t file_size, mp4_info_t *out) {
    memset(out, 0, sizeof *out);
    pctx_t p = {.rd = rd, .ctx = ctx, .file_size = file_size, .out = out};
    uint8_t h[8];
    if (rd(ctx, 0, h, 8) != 8) return META_E_READ;
    if (memcmp(h + 4, "ftyp", 4) != 0) return META_E_FORMAT;
    walk(&p, 0, file_size, 0, 0);
    if (!p.got_audio) return META_E_NO_AUDIO;
    if (p.aac) return META_E_AAC;
    if (p.other || !out->is_alac) return META_E_CODEC;
    if (out->stsz_off == 0 || out->stsc_off == 0 || out->stco_off == 0 || out->sample_count == 0 || out->stsc_count == 0 ||
        out->stco_count == 0) {
        return META_E_FORMAT;
    }
    // The cookie is authoritative for the stream format.
    out->frame_length = be32(out->cookie);
    out->bits = out->cookie[5];
    out->channels = out->cookie[9];
    out->rate = be32(out->cookie + 20);
    if (out->channels < 1 || out->channels > 2) return META_E_CHANNELS;
    if (out->rate != 44100u && out->rate != 48000u && out->rate != 88200u && out->rate != 96000u) return META_E_RATE;
    if (out->bits == 32) return META_E_ALAC32; // 4096-frame 32-bit stereo would need ~115 KB of arena
    if (out->bits != 16 && out->bits != 20 && out->bits != 24) return META_E_BITS;
    if (out->frame_length == 0 || out->frame_length > 16384) return META_E_FORMAT;
    out->file_size = file_size;
    out->max_packet = out->frame_length * out->channels * ((out->bits + 7u) / 8u) + 64u; // what the decoder's frame buffer holds
    if (p.timescale != 0) { // duration * rate without overflow, clamped to 2^40 frames
        uint64_t secs = p.duration / p.timescale, rem = p.duration % p.timescale;
        out->total_frames = secs > (1ull << 40) / out->rate ? (1ull << 40) : secs * out->rate + rem * out->rate / p.timescale;
    }
    return META_OK;
}

// ---- packet iterator ---------------------------------------------------------------------------------------

void mp4_iter_init(mp4_iter_t *it, const mp4_info_t *info) {
    memset(it, 0, sizeof *it);
    it->info = info;
    it->chunk = 0xFFFFFFFFu; // "before the first chunk"
    it->run = 0;
}

static bool sample_size(mp4_iter_t *it, meta_read_fn rd, void *ctx, uint32_t idx, uint32_t *sz) {
    const mp4_info_t *m = it->info;
    if (m->stsz_fixed != 0) {
        *sz = m->stsz_fixed;
        return true;
    }
    if (idx < it->sz_first || idx >= it->sz_first + it->sz_n) {
        uint32_t want = m->sample_count - idx < 64u ? m->sample_count - idx : 64u;
        uint8_t raw[256];
        if (rd(ctx, m->stsz_off + (uint64_t)idx * 4u, raw, want * 4u) != (int32_t)(want * 4u)) return false;
        for (uint32_t i = 0; i < want; i++) it->szbuf[i] = be32(raw + 4 * i);
        it->sz_first = idx;
        it->sz_n = want;
    }
    *sz = it->szbuf[idx - it->sz_first];
    return true;
}

bool mp4_iter_next(mp4_iter_t *it, meta_read_fn rd, void *ctx, uint64_t *offset, uint32_t *size) {
    const mp4_info_t *m = it->info;
    if (it->sample >= m->sample_count) return false;
    if (it->chunk == 0xFFFFFFFFu || it->in_chunk >= it->chunk_samples) { // move to the next chunk
        uint32_t c = it->chunk == 0xFFFFFFFFu ? 0 : it->chunk + 1;
        if (c >= m->stco_count) return false;
        // stsc entries are (first_chunk 1-based, samples_per_chunk, desc): the last entry with first_chunk <= c+1 applies
        if (it->chunk == 0xFFFFFFFFu) it->run = 0;
        while (it->run + 1 < m->stsc_count) {
            uint8_t e[4];
            if (rd(ctx, m->stsc_off + (uint64_t)(it->run + 1) * 12u, e, 4) != 4) return false;
            if (be32(e) <= c + 1) it->run++;
            else break;
        }
        uint8_t e[8];
        if (rd(ctx, m->stsc_off + (uint64_t)it->run * 12u, e, 8) != 8) return false;
        it->chunk_samples = be32(e + 4);
        uint8_t o[8];
        uint32_t w = m->co64 ? 8u : 4u;
        if (rd(ctx, m->stco_off + (uint64_t)c * w, o, w) != (int32_t)w) return false;
        it->pos = m->co64 ? be64(o) : be32(o);
        it->chunk = c;
        it->in_chunk = 0;
        if (it->chunk_samples == 0) return false;
    }
    uint32_t sz;
    if (!sample_size(it, rd, ctx, it->sample, &sz)) return false;
    if (sz == 0 || sz > m->max_packet || it->pos > m->file_size || sz > m->file_size - it->pos) return false; // outside the file / too big for the decoder
    *offset = it->pos;
    *size = sz;
    it->pos += sz;
    it->in_chunk++;
    it->sample++;
    return true;
}

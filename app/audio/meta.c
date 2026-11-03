// SPDX-License-Identifier: MPL-2.0
#include <stdbool.h>
#include <string.h>

#include "meta.h"

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

const char *meta_err_str(meta_err_t e) {
    switch (e) {
    case META_OK: return "ok";
    case META_E_READ: return "Cannot read the file";
    case META_E_FORMAT: return "Unrecognised or damaged file";
    case META_E_CHANNELS: return "More than 2 channels is not supported yet";
    case META_E_RATE: return "Unsupported sample rate (44.1/48/88.2/96 kHz only)";
    case META_E_ALAC32: return "32-bit ALAC not supported";
    case META_E_BITS: return "Unsupported bit depth";
    case META_E_BLOCKSIZE: return "FLAC block size too large for the decoder";
    case META_E_AAC: return "AAC not supported yet";
    case META_E_CODEC: return "Unsupported audio codec in this file";
    case META_E_NO_AUDIO: return "No audio found in the file";
    }
    return "?";
}

static uint8_t *g_scratch;
static size_t g_scratch_n;
void meta_set_scratch(void *buf, size_t bytes) {
    g_scratch = buf;
    g_scratch_n = bytes;
}

static bool rate_ok(uint32_t r) { return r == 44100u || r == 48000u || r == 88200u || r == 96000u; }

// ---- text ------------------------------------------------------------------------------------------------

static size_t put_utf8(char *d, size_t cap, size_t at, uint32_t cp) {
    uint8_t tmp[4];
    size_t n;
    if (cp < 0x80) {
        tmp[0] = (uint8_t)cp;
        n = 1;
    } else if (cp < 0x800) {
        tmp[0] = (uint8_t)(0xC0 | (cp >> 6));
        tmp[1] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 2;
    } else {
        tmp[0] = (uint8_t)(0xE0 | (cp >> 12));
        tmp[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (uint8_t)(0x80 | (cp & 0x3F));
        n = 3;
    }
    if (at + n + 1 > cap) {
        return (size_t)-1; // full: never cut a character in half
    }
    memcpy(d + at, tmp, n);
    return at + n;
}

void meta_text_to_utf8(char *dst, size_t cap, const uint8_t *src, size_t n, int enc) {
    size_t at = 0;
    if (cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (enc == 0 || enc == 3) {
        for (size_t i = 0; i < n && src[i] != 0; i++) {
            if (enc == 3) { // UTF-8: copy whole characters
                size_t len = src[i] < 0x80 ? 1 : (src[i] >> 5) == 6 ? 2 : (src[i] >> 4) == 14 ? 3 : (src[i] >> 3) == 30 ? 4 : 1;
                if (i + len > n || at + len + 1 > cap) {
                    break;
                }
                memcpy(dst + at, src + i, len);
                at += len;
                i += len - 1;
            } else {
                size_t r = put_utf8(dst, cap, at, src[i]);
                if (r == (size_t)-1) {
                    break; // full: keep what is already there
                }
                at = r;
            }
        }
    } else { // UTF-16, with or without BOM
        bool le = false;
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            if (src[0] == 0xFF && src[1] == 0xFE) {
                le = true;
                i = 2;
            } else if (src[0] == 0xFE && src[1] == 0xFF) {
                i = 2;
            }
        }
        for (; i + 1 < n; i += 2) {
            uint32_t u = le ? (uint32_t)(src[i] | (src[i + 1] << 8)) : (uint32_t)((src[i] << 8) | src[i + 1]);
            if (u == 0) {
                break;
            }
            if (u >= 0xD800 && u < 0xE000) { // surrogate pair: outside what the font shows anyway
                if (u < 0xDC00 && i + 3 < n) {
                    uint32_t lo = le ? (uint32_t)(src[i + 2] | (src[i + 3] << 8)) : (uint32_t)((src[i + 2] << 8) | src[i + 3]);
                    if (lo >= 0xDC00 && lo < 0xE000) {
                        i += 2; // skip the low half too
                    }
                }
                u = '?';
            }
            size_t r = put_utf8(dst, cap, at, u);
            if (r == (size_t)-1) {
                break;
            }
            at = r;
        }
    }
    dst[at] = '\0';
}

static int ieq(const char *a, const char *b, size_t n) { // case-insensitive compare of n chars
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
    }
    return 1;
}

static uint16_t track_from(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9' && v < 10000) {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return (uint16_t)v;
}

// ---- embedded pictures (position only; see art_ref_t) ------------------------------------------------------

art_kind_t meta_art_sniff(meta_read_fn rd, void *ctx, uint64_t offset, uint32_t length) {
    uint8_t m[4];
    if (length < 4 || rd(ctx, offset, m, 4) != 4) return ART_NONE;
    if (m[0] == 0xFF && m[1] == 0xD8 && m[2] == 0xFF) return ART_JPEG;
    if (m[0] == 0x89 && m[1] == 'P' && m[2] == 'N' && m[3] == 'G') return ART_PNG;
    return ART_OTHER;
}

static int art_score(const art_ref_t *a) {
    if (a->kind == ART_NONE) return 0;
    return (a->kind == ART_JPEG ? 4 : a->kind == ART_PNG ? 2 : 1) + (a->type == 3 ? 1 : 0);
}

void meta_art_offer(art_ref_t *best, const art_ref_t *cand) {
    if (art_score(cand) > art_score(best)) *best = *cand;
}

// ---- FLAC ------------------------------------------------------------------------------------------------

static void vorbis_comments(const uint8_t *b, uint32_t n, audio_tags_t *t) {
    uint32_t p = 0;
    if (n < 8) return;
    uint32_t vendor = le32(b);
    if (vendor > n - 8) return; // vendor string must fit with the comment count after it
    p = 4 + vendor;
    uint32_t count = le32(b + p);
    p += 4;
    for (uint32_t i = 0; i < count && p + 4 <= n; i++) {
        uint32_t len = le32(b + p);
        p += 4;
        if (len > n - p) return;
        const char *s = (const char *)(b + p);
        const char *eq = memchr(s, '=', len);
        if (eq != NULL) {
            size_t k = (size_t)(eq - s);
            const uint8_t *v = (const uint8_t *)eq + 1;
            size_t vl = len - k - 1;
            if (k == 5 && ieq(s, "TITLE", 5)) meta_text_to_utf8(t->title, sizeof t->title, v, vl, 3);
            else if (k == 6 && ieq(s, "ARTIST", 6)) meta_text_to_utf8(t->artist, sizeof t->artist, v, vl, 3);
            else if (k == 5 && ieq(s, "ALBUM", 5)) meta_text_to_utf8(t->album, sizeof t->album, v, vl, 3);
            else if (k == 11 && ieq(s, "TRACKNUMBER", 11)) {
                char tmp[12];
                size_t m = vl < 11 ? vl : 11;
                memcpy(tmp, v, m);
                tmp[m] = 0;
                t->track = track_from(tmp);
            }
        }
        p += len;
    }
}

// FLAC PICTURE block: type, mime, description, width, height, depth, colours, length, data (all 32-bit big-endian counts).
static void flac_picture(meta_read_fn rd, void *ctx, uint64_t body, uint32_t len, audio_tags_t *t) {
    uint8_t h[8];
    if (len < 32 || rd(ctx, body, h, 8) != 8) return;
    uint32_t ptype = be32(h), mlen = be32(h + 4);
    uint64_t at = 8 + (uint64_t)mlen; // the mime string is skipped: the image's own first bytes say what it is
    uint8_t w[4];
    if (at + 4 > len || rd(ctx, body + at, w, 4) != 4) return;
    at += 4 + (uint64_t)be32(w); // description
    uint8_t f[20];
    if (at + 20 > len || rd(ctx, body + at, f, 20) != 20) return;
    uint32_t dlen = be32(f + 16);
    at += 20;
    if (dlen > len - at) return;
    art_ref_t c = {.offset = body + at, .length = dlen, .type = (uint8_t)(ptype > 255 ? 0 : ptype)};
    c.kind = (uint8_t)meta_art_sniff(rd, ctx, c.offset, dlen);
    meta_art_offer(&t->art, &c);
}

static uint64_t id3v2_size(meta_read_fn rd, void *ctx, uint8_t hdr[10]) {
    if (rd(ctx, 0, hdr, 10) != 10 || memcmp(hdr, "ID3", 3) != 0) return 0;
    if ((hdr[6] | hdr[7] | hdr[8] | hdr[9]) & 0x80) return 0;
    uint64_t sz = ((uint64_t)hdr[6] << 21) | ((uint64_t)hdr[7] << 14) | ((uint64_t)hdr[8] << 7) | hdr[9];
    return 10 + sz + ((hdr[5] & 0x10) ? 10 : 0);
}

meta_err_t flac_parse(meta_read_fn rd, void *ctx, uint64_t file_size, flac_info_t *out) {
    memset(out, 0, sizeof *out);
    uint8_t h10[10];
    uint64_t pos = id3v2_size(rd, ctx, h10); // some files carry an ID3v2 tag in front
    uint8_t b[4];
    if (rd(ctx, pos, b, 4) != 4) return META_E_READ;
    if (memcmp(b, "fLaC", 4) != 0) return META_E_FORMAT;
    pos += 4;
    bool have_si = false;
    for (int guard = 0; guard < 256; guard++) {
        uint8_t bh[4];
        if (rd(ctx, pos, bh, 4) != 4) return META_E_READ;
        bool last = (bh[0] & 0x80) != 0;
        uint32_t type = bh[0] & 0x7Fu;
        uint32_t len = ((uint32_t)bh[1] << 16) | ((uint32_t)bh[2] << 8) | bh[3];
        uint64_t body = pos + 4;
        if (type == 0) {
            uint8_t si[34];
            if (len < 34 || rd(ctx, body, si, 34) != 34) return META_E_FORMAT;
            out->min_block = ((uint32_t)si[0] << 8) | si[1];
            out->max_block = ((uint32_t)si[2] << 8) | si[3];
            out->rate = ((uint32_t)si[10] << 12) | ((uint32_t)si[11] << 4) | (si[12] >> 4);
            out->channels = (uint8_t)(((si[12] >> 1) & 7u) + 1u);
            out->bits = (uint8_t)((((si[12] & 1u) << 4) | (si[13] >> 4)) + 1u);
            out->total_frames = ((uint64_t)(si[13] & 0xFu) << 32) | be32(si + 14);
            memcpy(out->md5, si + 18, 16);
            have_si = true;
        } else if (type == 6) {
            flac_picture(rd, ctx, body, len, &out->tags);
        } else if (type == 4 && g_scratch != NULL && len <= g_scratch_n) {
            if (rd(ctx, body, g_scratch, len) == (int32_t)len) vorbis_comments(g_scratch, len, &out->tags);
        }
        pos = body + len;
        if (last) break;
        if (pos >= file_size) return META_E_FORMAT;
    }
    if (!have_si) return META_E_FORMAT;
    out->audio_offset = pos;
    if (out->channels > 2) return META_E_CHANNELS;
    if (!rate_ok(out->rate)) return META_E_RATE;
    if (out->bits != 16 && out->bits != 20 && out->bits != 24 && out->bits != 32) return META_E_BITS;
    if (out->max_block > (out->bits == 32 ? META_FLAC_MAX_BLOCK_32BIT : META_FLAC_MAX_BLOCK) || out->max_block < 16) return META_E_BLOCKSIZE;
    return META_OK;
}

// ---- MP3 -------------------------------------------------------------------------------------------------

typedef struct {
    uint32_t rate, kbps, frame_bytes, spf;
    uint8_t channels, version; // version: 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5
} mp3_hdr_t;

static bool mp3_header(const uint8_t *h, mp3_hdr_t *o) {
    static const uint16_t br1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const uint16_t br2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const uint32_t sr1[3] = {44100, 48000, 32000};
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
    uint32_t ver = (h[1] >> 3) & 3u, layer = (h[1] >> 1) & 3u, bi = h[2] >> 4, si = (h[2] >> 2) & 3u, pad = (h[2] >> 1) & 1u;
    if (ver == 1 || layer != 1 || bi == 0 || bi == 15 || si == 3) return false;
    o->version = (uint8_t)ver;
    o->kbps = ver == 3 ? br1[bi] : br2[bi];
    o->rate = ver == 3 ? sr1[si] : sr1[si] >> (ver == 2 ? 1 : 2);
    o->spf = ver == 3 ? 1152 : 576;
    o->channels = ((h[3] >> 6) == 3) ? 1 : 2;
    o->frame_bytes = (ver == 3 ? 144000u : 72000u) * o->kbps / o->rate + pad;
    return true;
}

static void id3_text_frame(const char *id, const uint8_t *d, uint32_t n, audio_tags_t *t) {
    if (n < 2) return;
    int enc = d[0] > 3 ? 0 : d[0];
    const uint8_t *s = d + 1;
    uint32_t sl = n - 1;
    if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) meta_text_to_utf8(t->title, sizeof t->title, s, sl, enc);
    else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) meta_text_to_utf8(t->artist, sizeof t->artist, s, sl, enc);
    else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) meta_text_to_utf8(t->album, sizeof t->album, s, sl, enc);
    else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK")) {
        char tmp[16] = {0};
        meta_text_to_utf8(tmp, sizeof tmp, s, sl, enc);
        t->track = track_from(tmp);
    }
}

// APIC (v2.3/2.4: encoding, mime\0, type, description, data) or PIC (v2.2: encoding, 3-byte format, type, description, data).
// Frames that are compressed, encrypted or unsynchronised cannot be streamed from the file and are ignored.
static void id3_picture(meta_read_fn rd, void *ctx, uint64_t at, uint32_t sz, uint32_t ver, audio_tags_t *t) {
    uint8_t b[160]; // mime and description only; a longer description makes the picture unusable
    uint32_t n = sz < sizeof b ? sz : (uint32_t)sizeof b;
    if (n < 6 || rd(ctx, at, b, n) != (int32_t)n) return;
    uint32_t enc = b[0], i = 1;
    if (ver == 2) {
        i = 4;
    } else {
        while (i < n && b[i] != 0) i++; // mime type
        i++;
    }
    if (i >= n) return;
    uint8_t ptype = b[i++];
    if (enc == 1 || enc == 2) { // UTF-16 description: ends at an aligned 00 00
        while (i + 1 < n && (b[i] != 0 || b[i + 1] != 0)) i += 2;
        i += 2;
    } else {
        while (i < n && b[i] != 0) i++;
        i++;
    }
    if (i >= n || i >= sz) return; // description longer than what was read: not a normal picture
    art_ref_t c = {.offset = at + i, .length = sz - i, .type = ptype};
    c.kind = (uint8_t)meta_art_sniff(rd, ctx, c.offset, c.length);
    meta_art_offer(&t->art, &c);
}

static void id3v2_tags(meta_read_fn rd, void *ctx, const uint8_t h[10], uint64_t total, audio_tags_t *t) {
    uint32_t ver = h[3];
    if (ver < 2 || ver > 4) return;
    uint64_t pos = 10, end = total - ((h[5] & 0x10) ? 10 : 0);
    if (ver >= 3 && (h[5] & 0x40)) { // extended header
        uint8_t e[4];
        if (rd(ctx, pos, e, 4) != 4) return;
        uint32_t es = ver == 4 ? (((e[0] & 0x7Fu) << 21) | ((e[1] & 0x7Fu) << 14) | ((e[2] & 0x7Fu) << 7) | (e[3] & 0x7Fu)) : be32(e) + 4;
        pos += es;
    }
    uint32_t idlen = ver == 2 ? 3 : 4, hlen = ver == 2 ? 6 : 10;
    for (int guard = 0; guard < 200 && pos + hlen <= end; guard++) {
        uint8_t fh[10];
        if (rd(ctx, pos, fh, hlen) != (int32_t)hlen || fh[0] == 0) return;
        char id[5] = {0};
        memcpy(id, fh, idlen);
        uint32_t sz = ver == 2 ? (((uint32_t)fh[3] << 16) | ((uint32_t)fh[4] << 8) | fh[5])
                      : ver == 4 ? (((fh[4] & 0x7Fu) << 21) | ((fh[5] & 0x7Fu) << 14) | ((fh[6] & 0x7Fu) << 7) | (fh[7] & 0x7Fu))
                                 : be32(fh + 4);
        if (pos + hlen + sz > end) return;
        if ((ver == 2 ? !strcmp(id, "PIC") : !strcmp(id, "APIC")) && !(h[5] & 0x80)) {
            uint32_t skip = 0;
            bool usable = true;
            if (ver == 3) usable = (fh[9] & 0xE0) == 0; // compression, encryption, grouping
            if (ver == 4) {
                usable = (fh[9] & 0x4E) == 0; // grouping, compression, encryption, unsynchronisation
                if (fh[9] & 0x01) skip = 4;   // data length indicator
            }
            if (usable && sz > skip) id3_picture(rd, ctx, pos + hlen + skip, sz - skip, ver, t);
        }
        if (id[0] == 'T' && sz > 1 && sz <= 400) {
            uint8_t d[400];
            uint32_t skip = ver == 4 && (fh[9] & 0x01) ? 4u : 0u; // v2.4 data length indicator
            if (sz > skip + 1 && rd(ctx, pos + hlen + skip, d, sz - skip) == (int32_t)(sz - skip)) {
                uint32_t n = sz - skip;
                if ((h[5] & 0x80) || (ver == 4 && (fh[9] & 0x02))) { // unsynchronisation: FF 00 stands for FF
                    uint32_t w = 0;
                    for (uint32_t i = 0; i < n; i++) {
                        d[w++] = d[i];
                        if (d[i] == 0xFF && i + 1 < n && d[i + 1] == 0) i++;
                    }
                    n = w;
                }
                id3_text_frame(id, d, n, t);
            }
        }
        pos += hlen + sz;
    }
}

meta_err_t mp3_parse(meta_read_fn rd, void *ctx, uint64_t file_size, mp3_info_t *out) {
    memset(out, 0, sizeof *out);
    uint8_t h10[10];
    uint64_t start = id3v2_size(rd, ctx, h10);
    if (start != 0) id3v2_tags(rd, ctx, h10, start, &out->tags);
    uint8_t buf[512];
    mp3_hdr_t h = {0};
    uint64_t pos = start, found = 0;
    for (uint64_t scanned = 0; scanned < 65536 && !found; scanned += 256) { // find a header confirmed by the one after it
        int32_t n = rd(ctx, pos, buf, sizeof buf);
        if (n < 4) break;
        for (int32_t i = 0; i + 4 <= n && i < 256; i++) {
            mp3_hdr_t c;
            if (!mp3_header(buf + i, &c)) continue;
            uint8_t nx[4];
            if (rd(ctx, pos + (uint64_t)i + c.frame_bytes, nx, 4) == 4) {
                mp3_hdr_t c2;
                if (!mp3_header(nx, &c2) || c2.rate != c.rate) continue;
            }
            h = c;
            found = pos + (uint64_t)i;
            break;
        }
        pos += 256;
    }
    if (!found) return META_E_FORMAT;
    if (!rate_ok(h.rate) || h.rate > 48000u) return META_E_RATE;
    out->audio_offset = found;
    out->rate = h.rate;
    out->channels = h.channels;
    out->bitrate_kbps = h.kbps;
    uint8_t fr[64] = {0};
    uint32_t side = h.version == 3 ? (h.channels == 1 ? 17u : 32u) : (h.channels == 1 ? 9u : 17u);
    uint64_t frames = 0;
    if (rd(ctx, found, fr, 64) == 64) {
        if (!(fr[1] & 1)) side += 2; // protection bit clear: a 2-byte CRC follows the header
    }
    if (4 + side + 12 <= 64) {
        const uint8_t *x = fr + 4 + side;
        if ((!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4)) && (x[7] & 1)) {
            frames = be32(x + 8);
            out->vbr = !memcmp(x, "Xing", 4);
        }
    }
    uint64_t audio_bytes = file_size > found ? file_size - found : 0;
    if (frames != 0) {
        out->total_frames = frames * h.spf;
        uint64_t secs = out->total_frames / h.rate;
        if (out->vbr && secs != 0) out->bitrate_kbps = (uint32_t)(audio_bytes * 8u / 1000u / secs);
    } else {
        out->total_frames = h.kbps != 0 ? audio_bytes * 8u * h.rate / (h.kbps * 1000u) : 0;
        out->estimated = true;
    }
    return META_OK;
}

uint32_t flac_make_header(const flac_info_t *f, uint8_t o[FLAC_SYNTH_HEADER_BYTES]) {
    memset(o, 0, FLAC_SYNTH_HEADER_BYTES);
    memcpy(o, "fLaC", 4);
    o[4] = 0x80; // last-metadata-block flag, type 0 (STREAMINFO)
    o[7] = 34;
    uint8_t *si = o + 8;
    si[0] = (uint8_t)(f->min_block >> 8);
    si[1] = (uint8_t)f->min_block;
    // The decoder sizes its sample buffer from this value and would grow it (leaking arena space) if a frame were larger.
    // Declaring the largest block we accept makes the buffer fixed: a bigger frame then fails cleanly instead of growing.
    uint32_t maxb = f->bits == 32 ? META_FLAC_MAX_BLOCK_32BIT : META_FLAC_MAX_BLOCK;
    si[2] = (uint8_t)(maxb >> 8);
    si[3] = (uint8_t)maxb; // min/max frame size (bytes 4..9) stay 0 = unknown
    si[10] = (uint8_t)(f->rate >> 12);
    si[11] = (uint8_t)(f->rate >> 4);
    si[12] = (uint8_t)(((f->rate & 0xFu) << 4) | (((uint32_t)f->channels - 1u) << 1) | (((uint32_t)f->bits - 1u) >> 4));
    si[13] = (uint8_t)((((uint32_t)f->bits - 1u) << 4) | (uint8_t)((f->total_frames >> 32) & 0xFu));
    si[14] = (uint8_t)(f->total_frames >> 24);
    si[15] = (uint8_t)(f->total_frames >> 16);
    si[16] = (uint8_t)(f->total_frames >> 8);
    si[17] = (uint8_t)f->total_frames;
    memcpy(si + 18, f->md5, 16);
    return FLAC_SYNTH_HEADER_BYTES;
}

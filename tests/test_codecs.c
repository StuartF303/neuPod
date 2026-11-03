// SPDX-License-Identifier: MPL-2.0
// Host tests for the codecs: FLAC bit-exactness (vs libFLAC's STREAMINFO MD5 and an independent MD5 of the source),
// MP3 sanity, ALAC bit-exactness (Apple's reference encoder), the MP4 demuxer, tag parsers, MD5, the arena, and the
// core0/core1 stream handshake. Vectors: tests/assets/audio (see tools/gen_audio_vectors.py, tools/gen_alac_vectors.py).
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "alac_dec.h"
#include "arena.h"
#include "audio_dsp.h"
#include "codec.h"
#include "codec_dr_config.h"
#include "../app/third_party/dr_libs/dr_flac.h"
#include "../app/third_party/dr_libs/dr_mp3.h"
#include "md5.h"
#include "meta.h"
#include "stream_io.h"
#include "unit.h"

#define ASSETS NEUPOD_TEST_ASSETS

typedef struct {
    uint8_t *d;
    size_t n;
} blob_t;

static blob_t load(const char *name) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", ASSETS, name);
    blob_t b = {0, 0};
    FILE *f = fopen(p, "rb");
    if (!f) {
        printf("  cannot open %s\n", p);
        return b;
    }
    fseek(f, 0, SEEK_END);
    b.n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    b.d = malloc(b.n + 1);
    if (fread(b.d, 1, b.n, f) != b.n) b.n = 0;
    fclose(f);
    return b;
}

static int32_t mem_rd(void *ctx, uint64_t off, void *dst, uint32_t n) {
    const blob_t *b = ctx;
    if (off >= b->n) return 0;
    uint32_t k = off + n > b->n ? (uint32_t)(b->n - off) : n;
    memcpy(dst, b->d + off, k);
    return (int32_t)k;
}

// codec_io over a memory buffer
typedef struct {
    const uint8_t *d;
    size_t n, pos;
} mio_t;
static size_t mio_read(void *c, void *dst, size_t n) {
    mio_t *m = c;
    size_t k = m->n - m->pos < n ? m->n - m->pos : n;
    memcpy(dst, m->d + m->pos, k);
    m->pos += k;
    return k;
}
static bool mio_seek(void *c, int64_t off, int origin) {
    mio_t *m = c;
    int64_t t = origin == 0 ? off : (int64_t)m->pos + off;
    if (t < 0 || (size_t)t > m->n) return false;
    m->pos = (size_t)t;
    return true;
}

static void hex(const uint8_t *d, char *out) {
    for (int i = 0; i < 16; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

typedef struct {
    char md5[40];
    unsigned rate, ch, bits, frames;
} expect_t;

static bool read_expect(const char *name, expect_t *e) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", ASSETS, name);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    int r = fscanf(f, "%39s %u %u %u %u", e->md5, &e->rate, &e->ch, &e->bits, &e->frames);
    fclose(f);
    return r == 5;
}

// Decode a whole in-memory stream; returns frames, fills md5 of the native samples (FLAC layout).
static uint32_t decode_all(const codec_params_t *p, const uint8_t *data, size_t n, uint8_t md5[16], double *secs,
                           int32_t **keep, uint32_t keep_max) {
    mio_t m = {data, n, 0};
    codec_io_t io = {mio_read, mio_seek, NULL, &m};
    codec_t c;
    codec_err_t e = codec_open(&c, p, &io);
    if (e != CODEC_OK) {
        printf("  codec_open failed %d\n", e);
        return 0;
    }
    static int32_t out[512 * 2];
    md5_t h;
    md5_init(&h);
    uint32_t total = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        int32_t k = codec_read(&c, out, 256);
        if (k <= 0) break;
        if (keep && total + (uint32_t)k <= keep_max) memcpy(*keep + 2 * total, out, (size_t)k * 8);
        // hash the native channels (mono: left only), as FLAC / our sources define it
        if (p->channels == 1) {
            int32_t mono[256];
            for (int i = 0; i < k; i++) mono[i] = out[2 * i];
            codec_md5_pcm(&h, mono, (uint32_t)k, 1, p->bits);
        } else {
            codec_md5_pcm(&h, out, (uint32_t)k, 2, p->bits);
        }
        total += (uint32_t)k;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (secs) *secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    md5_final(&h, md5);
    codec_close(&c);
    return total;
}

/* ---- MD5 ---- */
TEST(md5_rfc1321_vectors) {
    const struct { const char *in, *out; } v[] = {
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"a", "0cc175b9c0f1b6a831c399e269772661"},
        {"abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        {"12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a"}};
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        md5_t m;
        uint8_t d[16];
        char h[33];
        md5_init(&m);
        size_t n = strlen(v[i].in);
        md5_update(&m, v[i].in, n / 2); // split update exercises the buffering
        md5_update(&m, v[i].in + n / 2, n - n / 2);
        md5_final(&m, d);
        hex(d, h);
        ASSERT_EQ(0, strcmp(h, v[i].out));
    }
}

/* ---- arena ---- */
TEST(arena_semantics) {
    static uint8_t store[1024];
    arena_t a;
    arena_init(&a, store, sizeof store);
    void *p1 = arena_alloc(&a, 100);
    void *p2 = arena_alloc(&a, 200);
    ASSERT_TRUE(p1 && p2 && ((uintptr_t)p1 & 7) == 0 && ((uintptr_t)p2 & 7) == 0);
    ASSERT_TRUE((uint8_t *)p2 >= (uint8_t *)p1 + 100);
    memset(p1, 0xAA, 100);
    memset(p2, 0xBB, 200);
    ASSERT_EQ(0xAA, ((uint8_t *)p1)[99]);
    size_t before = a.used;
    arena_free(&a, p1); // not the newest: nothing comes back
    ASSERT_EQ(before, a.used);
    arena_free(&a, p2); // newest: rolls back
    ASSERT_TRUE(a.used < before);
    void *p3 = arena_realloc(&a, p1, 300); // p1 is now the newest: grows in place
    ASSERT_TRUE(p3 == p1);
    ASSERT_EQ(0xAA, ((uint8_t *)p3)[50]);
    void *big = arena_alloc(&a, 5000);
    ASSERT_TRUE(big == NULL);
    ASSERT_EQ(1, a.failures);
    ASSERT_TRUE(arena_alloc(&a, (size_t)-1) == NULL); // overflow-safe
    size_t hw = a.high;
    arena_reset(&a);
    ASSERT_EQ(0, a.used);
    ASSERT_EQ(hw, a.high);
    void *q = arena_alloc(&a, 1000);
    ASSERT_TRUE(q != NULL);
    ASSERT_TRUE(arena_alloc(&a, 100) == NULL); // 1000 + headers leaves too little
}

TEST(arena_worst_cases_fit) {
    const size_t hdr = 8;
    // FLAC: struct + largest block (8192 samples, stereo, int32) + seek table slack
    size_t flac = sizeof(drflac) + (size_t)META_FLAC_MAX_BLOCK * 2 * 4 + 4096 + 4 * hdr;
    // MP3: struct + input buffer
    size_t mp3 = sizeof(drmp3) + DRMP3_DATA_CHUNK_SIZE + 4 * hdr;
    // ALAC 32-bit stereo, 4096-frame packets: state + 3 buffers + compressed packet + packed output
    size_t alac = sizeof(alac_dec_t) + 3 * (size_t)4096 * 4 + (4096 * 2 * 4 + 64) + 4096 * 8 + 8 * hdr;
    printf("  worst-case arena use: flac %zu, mp3 %zu, alac32 %zu of %u bytes\n", flac, mp3, alac, CODEC_ARENA_BYTES);
    ASSERT_TRUE(flac <= CODEC_ARENA_BYTES);
    ASSERT_TRUE(mp3 <= CODEC_ARENA_BYTES);
    ASSERT_TRUE(alac <= CODEC_ARENA_BYTES);
}

/* ---- FLAC ---- */
static void flac_case(const char *base) {
    char n1[64], n2[64];
    snprintf(n1, sizeof n1, "%s.flac", base);
    snprintf(n2, sizeof n2, "%s.md5", base);
    blob_t f = load(n1);
    expect_t e;
    ASSERT_TRUE(f.n > 0 && read_expect(n2, &e));
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &f, f.n, &fi));
    ASSERT_EQ(e.rate, fi.rate);
    ASSERT_EQ(e.ch, fi.channels);
    ASSERT_EQ(e.bits, fi.bits);
    ASSERT_EQ(e.frames, fi.total_frames);
    char h[33];
    hex(fi.md5, h);
    ASSERT_EQ(0, strcmp(h, e.md5)); // libFLAC's STREAMINFO MD5 equals the independent MD5 of the source samples
    // Feed what core 0 feeds: the synthesised header, then the audio frames from audio_offset.
    uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
    uint32_t hl = flac_make_header(&fi, hdr);
    size_t n = hl + (f.n - fi.audio_offset);
    uint8_t *stream = malloc(n);
    memcpy(stream, hdr, hl);
    memcpy(stream + hl, f.d + fi.audio_offset, f.n - fi.audio_offset);
    codec_params_t p = {.kind = CODEC_FLAC, .rate = fi.rate, .channels = fi.channels, .bits = fi.bits};
    uint8_t md5[16];
    double secs;
    uint32_t frames = decode_all(&p, stream, n, md5, &secs, NULL, 0);
    hex(md5, h);
    ASSERT_EQ(e.frames, frames);
    ASSERT_EQ(0, strcmp(h, e.md5));
    printf("  %s: %u frames bit-exact in %.1f ms (%.0fx real time on this PC); arena high water %zu B\n", base, frames,
           secs * 1e3, (double)frames / fi.rate / secs, codec_arena_high_water());
    ASSERT_TRUE(codec_arena_high_water() <= CODEC_ARENA_BYTES);
    ASSERT_EQ(0, codec_arena_failures());
    // Verify mode on the same data: the codec's own MD5 must match STREAMINFO.
    mio_t m = {stream, n, 0};
    codec_io_t io = {mio_read, mio_seek, NULL, &m};
    codec_t c;
    p.verify = true;
    ASSERT_EQ(CODEC_OK, codec_open(&c, &p, &io));
    static int32_t out[1024];
    while (codec_read(&c, out, 256) > 0) {
    }
    uint8_t vm[16];
    codec_md5_final(&c, vm);
    ASSERT_EQ(0, memcmp(vm, fi.md5, 16));
    free(stream);
    free(f.d);
}
TEST(flac_16_44k_stereo_bit_exact) { flac_case("flac16_44k_st"); }
TEST(flac_24_96k_stereo_bit_exact) { flac_case("flac24_96k_st"); }
TEST(flac_24_48k_stereo_bit_exact) { flac_case("flac24_48k_st"); }
TEST(flac_16_48k_mono_bit_exact_and_duplicated) {
    flac_case("flac16_48k_mono");
    blob_t f = load("flac16_48k_mono.flac");
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &f, f.n, &fi));
    uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
    uint32_t hl = flac_make_header(&fi, hdr);
    uint8_t *stream = malloc(hl + f.n);
    memcpy(stream, hdr, hl);
    memcpy(stream + hl, f.d + fi.audio_offset, f.n - fi.audio_offset);
    mio_t m = {stream, hl + f.n - fi.audio_offset, 0};
    codec_io_t io = {mio_read, mio_seek, NULL, &m};
    codec_params_t p = {.kind = CODEC_FLAC, .rate = fi.rate, .channels = 1, .bits = 16};
    codec_t c;
    ASSERT_EQ(CODEC_OK, codec_open(&c, &p, &io));
    int32_t out[1024];
    int32_t k = codec_read(&c, out, 100);
    ASSERT_EQ(100, k);
    for (int i = 0; i < k; i++) ASSERT_EQ(out[2 * i], out[2 * i + 1]);
    ASSERT_EQ(0, out[0] & 0xFFFF); // left-justified 16-bit: low half is zero
    free(stream);
    free(f.d);
}
TEST(flac_header_roundtrip_and_rejections) {
    blob_t f = load("flac24_96k_st.flac");
    flac_info_t a, b;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &f, f.n, &a));
    uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
    blob_t hb = {hdr, flac_make_header(&a, hdr)};
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &hb, hb.n, &b));
    ASSERT_EQ(a.rate, b.rate);
    ASSERT_EQ(a.channels, b.channels);
    ASSERT_EQ(a.bits, b.bits);
    ASSERT_EQ(a.total_frames, b.total_frames);
    ASSERT_EQ(0, memcmp(a.md5, b.md5, 16));
    ASSERT_EQ(META_FLAC_MAX_BLOCK, b.max_block); // the synthesised header pre-sizes the decoder's sample buffer to the limit
    ASSERT_EQ(42, b.audio_offset);
    hdr[0] = 'X';
    ASSERT_EQ(META_E_FORMAT, flac_parse(mem_rd, &hb, hb.n, &b));
    hdr[0] = 'f';
    flac_info_t big = a;
    flac_make_header(&a, hdr);
    hdr[10] = 0x40; // declare 16384-sample blocks (the header builder itself never does)
    hdr[11] = 0;
    ASSERT_EQ(META_E_BLOCKSIZE, flac_parse(mem_rd, &hb, hb.n, &b));
    big = a;
    big.bits = 32;
    big.max_block = 4096; // dr_flac keeps 8 bytes per sample for 32-bit streams: 4096 is the most that fits
    flac_make_header(&big, hdr);
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &hb, hb.n, &b));
    ASSERT_EQ(META_FLAC_MAX_BLOCK_32BIT, b.max_block);
    big = a;
    big.channels = 6;
    flac_make_header(&big, hdr);
    ASSERT_EQ(META_E_CHANNELS, flac_parse(mem_rd, &hb, hb.n, &b));
    big = a;
    big.rate = 22050;
    flac_make_header(&big, hdr);
    ASSERT_EQ(META_E_RATE, flac_parse(mem_rd, &hb, hb.n, &b));
    free(f.d);
}
TEST(flac_vorbis_comments) {
    uint8_t buf[512];
    size_t n = 0;
    flac_info_t base;
    blob_t f = load("flac16_44k_st.flac");
    flac_parse(mem_rd, &f, f.n, &base);
    base.min_block = base.max_block = 4096;
    uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
    flac_make_header(&base, hdr);
    hdr[4] = 0x00; // STREAMINFO no longer last
    memcpy(buf, hdr, 42);
    n = 42;
    const char *c[] = {"title=Caf\xC3\xA9 Song", "ARTIST=The Band", "Album=Long Player", "TRACKNUMBER=3/11"};
    uint8_t vc[256];
    size_t v = 0;
    uint32_t vl = 3;
    memcpy(vc + v, &vl, 4); v += 4; memcpy(vc + v, "neu", 3); v += 3;
    uint32_t cnt = 4;
    memcpy(vc + v, &cnt, 4); v += 4;
    for (int i = 0; i < 4; i++) {
        uint32_t l = (uint32_t)strlen(c[i]);
        memcpy(vc + v, &l, 4); v += 4;
        memcpy(vc + v, c[i], l); v += l;
    }
    buf[n++] = 0x80 | 4;
    buf[n++] = 0; buf[n++] = (uint8_t)(v >> 8); buf[n++] = (uint8_t)v;
    memcpy(buf + n, vc, v);
    n += v;
    blob_t m = {buf, n};
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m, n, &fi));
    ASSERT_EQ(0, strcmp(fi.tags.title, "Caf\xC3\xA9 Song"));
    ASSERT_EQ(0, strcmp(fi.tags.artist, "The Band"));
    ASSERT_EQ(0, strcmp(fi.tags.album, "Long Player"));
    ASSERT_EQ(3, fi.tags.track);
    ASSERT_EQ(n, fi.audio_offset);
    free(f.d);
}

/* ---- MP3 ---- */
TEST(mp3_decode_length_and_level) {
    blob_t f = load("mp3_44k_st.mp3");
    char p[512];
    snprintf(p, sizeof p, "%s/mp3_44k_st.txt", ASSETS);
    FILE *tf = fopen(p, "r");
    unsigned rate, ch, frames;
    double rl, rr;
    ASSERT_TRUE(tf && fscanf(tf, "%u %u %u %lf %lf", &rate, &ch, &frames, &rl, &rr) == 5);
    if (tf) fclose(tf);
    mp3_info_t mi;
    ASSERT_EQ(META_OK, mp3_parse(mem_rd, &f, f.n, &mi));
    ASSERT_EQ(44100, mi.rate);
    ASSERT_EQ(2, mi.channels);
    ASSERT_TRUE(mi.bitrate_kbps >= 32 && mi.bitrate_kbps <= 320);
    // This encoder wrote no Xing/Info frame (a VBR stream), so the length is only a size-based estimate and says so.
    ASSERT_TRUE(mi.estimated && mi.total_frames > 0);
    codec_params_t cp = {.kind = CODEC_MP3, .rate = mi.rate, .channels = mi.channels, .bits = 32};
    static int32_t keepbuf[60000 * 2];
    int32_t *keep = keepbuf;
    uint8_t md5[16];
    double secs;
    uint32_t got = decode_all(&cp, f.d + mi.audio_offset, f.n - mi.audio_offset, md5, &secs, &keep, 60000);
    printf("  mp3: %u frames (source %u) in %.1f ms (%.0fx real time); arena high water %zu B\n", got, frames, secs * 1e3,
           (double)got / mi.rate / secs, codec_arena_high_water());
    ASSERT_TRUE(got > frames - 3000 && got < frames + 3000); // encoder delay / padding
    double sl = 0, sr = 0;
    uint32_t a = 4000, b = got - 4000, cnt = 0;
    for (uint32_t i = a; i < b; i++, cnt++) {
        double l = keepbuf[2 * i] / 2147483648.0, r = keepbuf[2 * i + 1] / 2147483648.0;
        sl += l * l;
        sr += r * r;
    }
    double dl = sqrt(sl / cnt), dr = sqrt(sr / cnt);
    ASSERT_TRUE(fabs(20 * log10(dl / rl)) < 1.0);
    ASSERT_TRUE(fabs(20 * log10(dr / rr)) < 1.0);
    ASSERT_TRUE(codec_arena_high_water() <= CODEC_ARENA_BYTES);
    // rate mismatch with the announced format is refused
    cp.rate = 48000;
    mio_t m = {f.d + mi.audio_offset, f.n - mi.audio_offset, 0};
    codec_io_t io = {mio_read, mio_seek, NULL, &m};
    codec_t c;
    ASSERT_EQ(CODEC_ERR_FORMAT, codec_open(&c, &cp, &io));
    free(f.d);
}
static void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
TEST(mp3_id3v2_tags) {
    blob_t f = load("mp3_44k_st.mp3");
    mp3_info_t plain;
    ASSERT_EQ(META_OK, mp3_parse(mem_rd, &f, f.n, &plain));
    uint8_t *buf = malloc(f.n + 512);
    size_t n = 10;
    // TIT2 UTF-16 with BOM, TPE1 ISO-8859-1, TALB UTF-8, TRCK
    struct { const char *id; uint8_t enc; const uint8_t *txt; size_t len; } fr[4] = {
        {"TIT2", 1, (const uint8_t *)"\xFF\xFEH\0i\0\xE9\0", 8},
        {"TPE1", 0, (const uint8_t *)"Jos\xE9", 4},
        {"TALB", 3, (const uint8_t *)"Alb\xC3\xBCm", 6},
        {"TRCK", 0, (const uint8_t *)"9/10", 4}};
    for (int i = 0; i < 4; i++) {
        memcpy(buf + n, fr[i].id, 4);
        put_be32(buf + n + 4, (uint32_t)fr[i].len + 1);
        buf[n + 8] = buf[n + 9] = 0;
        buf[n + 10] = fr[i].enc;
        memcpy(buf + n + 11, fr[i].txt, fr[i].len);
        n += 11 + fr[i].len;
    }
    size_t tag = n - 10 + 64; // 64 bytes of zero padding
    memset(buf + n, 0, 64);
    n += 64;
    memcpy(buf, "ID3\x03\x00\x00", 6);
    buf[6] = (uint8_t)((tag >> 21) & 0x7F); buf[7] = (uint8_t)((tag >> 14) & 0x7F);
    buf[8] = (uint8_t)((tag >> 7) & 0x7F); buf[9] = (uint8_t)(tag & 0x7F);
    memcpy(buf + n, f.d + plain.audio_offset, f.n - plain.audio_offset);
    blob_t m = {buf, n + f.n - plain.audio_offset};
    mp3_info_t mi;
    ASSERT_EQ(META_OK, mp3_parse(mem_rd, &m, m.n, &mi));
    ASSERT_EQ(n, mi.audio_offset);
    ASSERT_EQ(0, strcmp(mi.tags.title, "Hi\xC3\xA9"));
    ASSERT_EQ(0, strcmp(mi.tags.artist, "Jos\xC3\xA9"));
    ASSERT_EQ(0, strcmp(mi.tags.album, "Alb\xC3\xBCm"));
    ASSERT_EQ(9, mi.tags.track);
    free(buf);
    free(f.d);
}

/* ---- ALAC / MP4 ---- */
static void alac_case(const char *base, bool tags) {
    char n1[64], n2[64];
    snprintf(n1, sizeof n1, "%s.m4a", base);
    snprintf(n2, sizeof n2, "%s.md5", base);
    blob_t f = load(n1);
    expect_t e;
    ASSERT_TRUE(f.n > 0 && read_expect(n2, &e));
    mp4_info_t mi;
    ASSERT_EQ(META_OK, mp4_parse(mem_rd, &f, f.n, &mi));
    ASSERT_EQ(e.rate, mi.rate);
    ASSERT_EQ(e.ch, mi.channels);
    ASSERT_EQ(e.bits, mi.bits);
    ASSERT_EQ(e.frames, mi.total_frames);
    ASSERT_EQ(4096, mi.frame_length);
    if (tags) {
        ASSERT_EQ(0, strcmp(mi.tags.title, "Test Title \xC3\xA9"));
        ASSERT_EQ(0, strcmp(mi.tags.artist, "Test Artist"));
        ASSERT_EQ(0, strcmp(mi.tags.album, "Test Album"));
        ASSERT_EQ(7, mi.tags.track);
    }
    // Walk the packets with the iterator and frame them (u32 BE length + bytes) as core 0 does.
    uint8_t *stream = malloc(f.n + 4 * (size_t)mi.sample_count);
    size_t sn = 0;
    mp4_iter_t it;
    mp4_iter_init(&it, &mi);
    uint64_t off, prev_end = 0;
    uint32_t sz, count = 0;
    while (mp4_iter_next(&it, mem_rd, &f, &off, &sz)) {
        ASSERT_TRUE(off >= prev_end || count % 1 == 0);
        ASSERT_TRUE(off + sz <= f.n);
        put_be32(stream + sn, sz);
        memcpy(stream + sn + 4, f.d + off, sz);
        sn += 4 + sz;
        prev_end = off + sz;
        count++;
    }
    ASSERT_EQ(mi.sample_count, count);
    codec_params_t p = {.kind = CODEC_ALAC, .rate = mi.rate, .channels = mi.channels, .bits = mi.bits, .cookie_len = mi.cookie_len};
    memcpy(p.cookie, mi.cookie, mi.cookie_len);
    uint8_t md5[16];
    double secs;
    uint32_t frames = decode_all(&p, stream, sn, md5, &secs, NULL, 0);
    char h[33];
    hex(md5, h);
    ASSERT_EQ(e.frames, frames);
    ASSERT_EQ(0, strcmp(h, e.md5));
    printf("  %s: %u frames bit-exact in %.1f ms (%.0fx real time); arena high water %zu B\n", base, frames, secs * 1e3,
           (double)frames / mi.rate / secs, codec_arena_high_water());
    ASSERT_TRUE(codec_arena_high_water() <= CODEC_ARENA_BYTES);
    free(stream);
    free(f.d);
}
TEST(alac_16_44k_stereo_bit_exact_with_tags) { alac_case("alac16_44k_st", true); }
TEST(alac_24_48k_stereo_bit_exact_co64) { alac_case("alac24_48k_st", false); }
TEST(alac_16_48k_mono_bit_exact) { alac_case("alac16_48k_mono", false); }

static size_t mkbox(uint8_t *p, const char *t, const uint8_t *body, size_t n) {
    put_be32(p, (uint32_t)(8 + n));
    memcpy(p + 4, t, 4);
    if (n) memcpy(p + 8, body, n);
    return 8 + n;
}
TEST(mp4_aac_is_recognised_and_garbage_rejected) {
    // ftyp + moov/trak/mdia/(hdlr soun)(minf/stbl/stsd with an mp4a entry)
    uint8_t ft[16] = {'M', '4', 'A', ' ', 0, 0, 0, 0, 'M', '4', 'A', ' '};
    uint8_t hd[12] = {0, 0, 0, 0, 0, 0, 0, 0, 's', 'o', 'u', 'n'};
    uint8_t sd[8 + 36] = {0};
    put_be32(sd + 4, 1);
    put_be32(sd + 8, 36);
    memcpy(sd + 12, "mp4a", 4);
    uint8_t buf[512], t1[200], t2[200], t3[200], t4[200];
    size_t n = mkbox(buf, "ftyp", ft, 12), k;
    k = mkbox(t1, "stsd", sd, sizeof sd);
    k = mkbox(t2, "stbl", t1, k);
    k = mkbox(t3, "minf", t2, k);
    uint8_t mdia[300];
    size_t m = mkbox(mdia, "hdlr", hd, 12);
    memcpy(mdia + m, t3, k);
    m += k;
    k = mkbox(t4, "mdia", mdia, m);
    uint8_t trak[300], moov[400];
    k = mkbox(trak, "trak", t4, k);
    k = mkbox(moov, "moov", trak, k);
    memcpy(buf + n, moov, k);
    n += k;
    blob_t b = {buf, n};
    mp4_info_t mi;
    ASSERT_EQ(META_E_AAC, mp4_parse(mem_rd, &b, n, &mi));
    ASSERT_EQ(0, strcmp(meta_err_str(META_E_AAC), "AAC not supported yet"));
    memcpy(buf + 4, "free", 4);
    ASSERT_EQ(META_E_FORMAT, mp4_parse(mem_rd, &b, n, &mi));
}
TEST(mp4_iterator_sample_table_maths) {
    // 7 packets: stsz 10..70, chunks of 3 (runs: 3 per chunk from chunk 1, 1 per chunk from chunk 3), offsets known.
    uint8_t img[1024] = {0};
    mp4_info_t mi;
    memset(&mi, 0, sizeof mi);
    mi.sample_count = 7;
    mi.file_size = 100000;
    mi.max_packet = 1000;
    mi.stsz_off = 0;
    for (int i = 0; i < 7; i++) put_be32(img + 4 * i, 10u * (uint32_t)(i + 1));
    mi.stsc_off = 100;
    mi.stsc_count = 2;
    put_be32(img + 100, 1); put_be32(img + 104, 3); put_be32(img + 108, 1);
    put_be32(img + 112, 3); put_be32(img + 116, 1); put_be32(img + 120, 1);
    mi.stco_off = 200;
    mi.stco_count = 3; // chunk 1: 3 packets, chunk 2: 3 packets, chunk 3: 1 packet
    put_be32(img + 200, 1000); put_be32(img + 204, 5000); put_be32(img + 208, 9000);
    blob_t b = {img, sizeof img};
    mp4_iter_t it;
    mp4_iter_init(&it, &mi);
    uint64_t off;
    uint32_t sz;
    const uint64_t want_off[7] = {1000, 1010, 1030, 5000, 5040, 5090, 9000};
    for (int i = 0; i < 7; i++) {
        ASSERT_TRUE(mp4_iter_next(&it, mem_rd, &b, &off, &sz));
        ASSERT_EQ(want_off[i], off);
        ASSERT_EQ(10 * (i + 1), sz);
    }
    ASSERT_FALSE(mp4_iter_next(&it, mem_rd, &b, &off, &sz));
    mi.stsz_fixed = 25; // fixed-size packets
    mp4_iter_init(&it, &mi);
    ASSERT_TRUE(mp4_iter_next(&it, mem_rd, &b, &off, &sz));
    ASSERT_EQ(25, sz);
}

/* ---- stream_io: core 0 <-> core 1 handshake with a real second thread ---- */
typedef struct {
    spsc_ring_t ring;
    uint8_t store[4096];
    volatile uint32_t eof, req, ack, done;
    volatile uint64_t off;
    volatile int stop_poll;
    volatile uint32_t polls;
    pthread_t th;
    volatile int mode; // 0 = write a counting stream then eof; 1 = idle (nothing to write); 2 = serve seeks
    volatile uint64_t pos; // core 0's feed position
    uint64_t length;
} h_t;
static bool h_poll(void *c) { h_t *h = c; h->polls++; return h->stop_poll != 0; }
static void h_idle(void) { struct timespec t = {0, 200000}; nanosleep(&t, NULL); }
static void *core0(void *arg) {
    h_t *h = arg;
    uint32_t seen = 0;
    while (h->mode != 9) {
        if (h->req != seen) { // seek: reposition, ack, wait for done
            seen = h->req;
            h->pos = h->off;
            h->ack = seen;
            while (h->done != seen && h->mode != 9) h_idle();
        }
        if (h->mode == 0 && h->pos < h->length) {
            uint8_t b = (uint8_t)(h->pos * 7u + (h->pos >> 8));
            if (spsc_ring_write(&h->ring, &b, 1) == 1) {
                h->pos++;
                if (h->pos == h->length) h->eof = 1;
            } else {
                h_idle();
            }
        } else {
            h_idle();
        }
    }
    return NULL;
}
static void h_start(h_t *h, uint64_t length) {
    memset(h, 0, sizeof *h);
    spsc_ring_init(&h->ring, h->store, 1, 4096);
    h->length = length;
    pthread_create(&h->th, NULL, core0, h);
}
static sio_t h_sio(h_t *h) {
    return (sio_t){.ring = &h->ring, .eof = &h->eof, .poll = h_poll, .idle = h_idle, .ctx = h, .seek_req = &h->req,
                   .seek_off = &h->off, .seek_ack = &h->ack, .seek_done = &h->done};
}
TEST(stream_io_reads_whole_stream_to_eof) {
    h_t h;
    h_start(&h, 20000);
    sio_t s = h_sio(&h);
    uint8_t buf[1500];
    uint64_t total = 0;
    for (;;) {
        size_t k = sio_read(&s, buf, sizeof buf);
        for (size_t i = 0; i < k; i++) {
            uint64_t pos = total + i;
            if (buf[i] != (uint8_t)(pos * 7u + (pos >> 8))) { ASSERT_TRUE(0); return; }
        }
        total += k;
        if (k < sizeof buf) break;
    }
    ASSERT_EQ(20000, total);
    ASSERT_FALSE(s.aborted);
    h.mode = 9;
    pthread_join(h.th, NULL);
}
TEST(stream_io_abort_while_waiting_does_not_deadlock) {
    h_t h;
    h_start(&h, 0);
    h.mode = 1; // core 0 sends nothing
    sio_t s = h_sio(&h);
    h.stop_poll = 1; // the "command arrived" signal
    uint8_t buf[100];
    ASSERT_EQ(0, sio_read(&s, buf, sizeof buf));
    ASSERT_TRUE(s.aborted);
    ASSERT_TRUE(h.polls > 0);
    h.mode = 9;
    pthread_join(h.th, NULL);
}
TEST(stream_io_forward_and_backward_seek) {
    h_t h;
    h_start(&h, 100000);
    sio_t s = h_sio(&h);
    uint8_t b[64];
    ASSERT_EQ(64, sio_read(&s, b, 64));
    ASSERT_TRUE(sio_seek(&s, 1000, 0)); // forward absolute: skipped by reading
    ASSERT_EQ(1000, s.pos);
    ASSERT_EQ(0, h.req);                // no handshake needed
    ASSERT_TRUE(sio_seek(&s, 500, 1));  // relative forward
    ASSERT_EQ(1500, s.pos);
    ASSERT_TRUE(sio_seek(&s, 200, 0));  // backwards: the handshake
    ASSERT_EQ(1, h.req);
    ASSERT_EQ(200, h.off);
    ASSERT_EQ(200, s.pos);
    ASSERT_EQ(1, h.done);
    ASSERT_EQ(1, sio_read(&s, b, 1)); // the next byte is the one at stream offset 200
    ASSERT_EQ((uint8_t)(200u * 7u + (200u >> 8)), b[0]);
    ASSERT_EQ(32, sio_read(&s, b, 32));
    for (int i = 0; i < 32; i++) ASSERT_EQ((uint8_t)((201u + (uint32_t)i) * 7u + ((201u + (uint32_t)i) >> 8)), b[i]);
    ASSERT_FALSE(sio_seek(&s, -5, 1));
    h.mode = 9;
    pthread_join(h.th, NULL);
}

// NEUPOD_BENCH=1 build/host/tests/test_codecs: decode speed of each vector over many repetitions (not a pass/fail test).
static void bench_one(const char *name, int kind) {
    blob_t f = load(name);
    codec_params_t p = {0};
    const uint8_t *data = f.d;
    size_t n = f.n;
    uint8_t *tmp = NULL;
    mp4_info_t m4;
    flac_info_t fi;
    mp3_info_t mi;
    if (kind == CODEC_FLAC) {
        flac_parse(mem_rd, &f, f.n, &fi);
        uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
        flac_make_header(&fi, hdr);
        n = 42 + f.n - fi.audio_offset;
        tmp = malloc(n);
        memcpy(tmp, hdr, 42);
        memcpy(tmp + 42, f.d + fi.audio_offset, f.n - fi.audio_offset);
        data = tmp;
        p = (codec_params_t){.kind = CODEC_FLAC, .rate = fi.rate, .channels = fi.channels, .bits = fi.bits};
    } else if (kind == CODEC_MP3) {
        mp3_parse(mem_rd, &f, f.n, &mi);
        data = f.d + mi.audio_offset;
        n = f.n - mi.audio_offset;
        p = (codec_params_t){.kind = CODEC_MP3, .rate = mi.rate, .channels = mi.channels, .bits = 32};
    } else {
        mp4_parse(mem_rd, &f, f.n, &m4);
        tmp = malloc(f.n + 4096);
        size_t sn = 0;
        mp4_iter_t it;
        mp4_iter_init(&it, &m4);
        uint64_t off;
        uint32_t sz;
        while (mp4_iter_next(&it, mem_rd, &f, &off, &sz)) {
            put_be32(tmp + sn, sz);
            memcpy(tmp + sn + 4, f.d + off, sz);
            sn += 4 + sz;
        }
        data = tmp;
        n = sn;
        p = (codec_params_t){.kind = CODEC_ALAC, .rate = m4.rate, .channels = m4.channels, .bits = m4.bits, .cookie_len = m4.cookie_len};
        memcpy(p.cookie, m4.cookie, m4.cookie_len);
    }
    uint8_t md5[16];
    double total = 0;
    uint64_t frames = 0;
    for (int i = 0; i < 300; i++) {
        double secs;
        frames += decode_all(&p, data, n, md5, &secs, NULL, 0);
        total += secs;
    }
    printf("bench %-18s %6.1f Mframes/s = %6.0fx real time (rate %u, %u ch); md5 hashing included\n", name, (double)frames / total / 1e6,
           (double)frames / total / p.rate, p.rate, p.channels);
    free(tmp);
    free(f.d);
}

/* ---- hostile input ---- */
static uint32_t g_rng = 12345;
static uint32_t rnd(void) { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

// Parse and, if the parse succeeds, decode a bounded amount. The only pass criterion is "does not crash / corrupt memory"
// (run it under the ASan build too); failing cleanly is fine.
static void hostile(int kind, const uint8_t *d, size_t n) {
    blob_t b = {(uint8_t *)d, n};
    codec_params_t p = {0};
    const uint8_t *data = d;
    size_t dn = n;
    uint8_t *tmp = NULL;
    flac_info_t fi;
    mp3_info_t mi;
    mp4_info_t m4;
    static mp4_info_t keep;
    if (kind == CODEC_FLAC) {
        if (flac_parse(mem_rd, &b, n, &fi) != META_OK) return;
        uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
        flac_make_header(&fi, hdr);
        dn = 42 + (n > fi.audio_offset ? n - fi.audio_offset : 0);
        tmp = malloc(dn);
        memcpy(tmp, hdr, 42);
        if (n > fi.audio_offset) memcpy(tmp + 42, d + fi.audio_offset, n - fi.audio_offset);
        data = tmp;
        p = (codec_params_t){.kind = CODEC_FLAC, .rate = fi.rate, .channels = fi.channels, .bits = fi.bits, .total_frames = fi.total_frames};
    } else if (kind == CODEC_MP3) {
        if (mp3_parse(mem_rd, &b, n, &mi) != META_OK) return;
        data = d + mi.audio_offset;
        dn = n - mi.audio_offset;
        p = (codec_params_t){.kind = CODEC_MP3, .rate = mi.rate, .channels = mi.channels, .bits = 32};
    } else {
        if (mp4_parse(mem_rd, &b, n, &m4) != META_OK) return;
        keep = m4;
        tmp = malloc(n + 4096);
        size_t sn = 0;
        mp4_iter_t it;
        mp4_iter_init(&it, &keep);
        uint64_t off;
        uint32_t sz;
        while (mp4_iter_next(&it, mem_rd, &b, &off, &sz) && sn < n + 2000) {
            if (sn + 4 + sz > n + 4096) break;
            put_be32(tmp + sn, sz);
            memcpy(tmp + sn + 4, d + off, sz);
            sn += 4 + sz;
        }
        data = tmp;
        dn = sn;
        p = (codec_params_t){.kind = CODEC_ALAC, .rate = m4.rate, .channels = m4.channels, .bits = m4.bits, .cookie_len = m4.cookie_len, .total_frames = m4.total_frames};
        memcpy(p.cookie, m4.cookie, m4.cookie_len);
    }
    mio_t m = {data, dn, 0};
    codec_io_t io = {mio_read, mio_seek, NULL, &m};
    codec_t c;
    if (codec_open(&c, &p, &io) == CODEC_OK) {
        static int32_t out[1024];
        uint32_t total = 0;
        int32_t k;
        while ((k = codec_read(&c, out, 256)) > 0 && (total += (uint32_t)k) < 200000) {
        }
    }
    free(tmp);
}
static void hostile_file(const char *name, int kind) {
    blob_t f = load(name);
    ASSERT_TRUE(f.n > 0);
    for (size_t n = 0; n < f.n && n < 400; n++) hostile(kind, f.d, n);          // truncated at every early byte
    for (size_t n = 400; n < f.n; n += f.n / 40 + 1) hostile(kind, f.d, n);      // and spread through the rest
    uint8_t *w = malloc(f.n);
    for (int i = 0; i < 300; i++) {                                              // random byte flips, fixed seed
        memcpy(w, f.d, f.n);
        int flips = 1 + (int)(rnd() % 4);
        for (int j = 0; j < flips; j++) {
            size_t at = (i % 3 == 0) ? rnd() % (f.n < 600 ? f.n : 600) : rnd() % f.n; // a third of them in the headers
            w[at] ^= (uint8_t)(1u << (rnd() % 8));
        }
        hostile(kind, w, f.n);
    }
    free(w);
    free(f.d);
}
TEST(hostile_flac_truncated_and_flipped) { hostile_file("flac16_44k_st.flac", CODEC_FLAC); hostile_file("flac24_96k_st.flac", CODEC_FLAC); }
TEST(hostile_mp3_truncated_and_flipped) { hostile_file("mp3_44k_st.mp3", CODEC_MP3); }
TEST(hostile_m4a_truncated_and_flipped) { hostile_file("alac16_44k_st.m4a", CODEC_ALAC); hostile_file("alac24_48k_st.m4a", CODEC_ALAC); }

TEST(damaged_flac_is_not_reported_as_finished_or_md5_ok) {
    blob_t f = load("flac16_44k_st.flac");
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &f, f.n, &fi));
    uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
    flac_make_header(&fi, hdr);
    size_t cut = fi.audio_offset + (f.n - fi.audio_offset) / 2;
    size_t n = 42 + (cut - fi.audio_offset);
    uint8_t *stream = malloc(n);
    memcpy(stream, hdr, 42);
    memcpy(stream + 42, f.d + fi.audio_offset, cut - fi.audio_offset);
    mio_t m = {stream, n, 0};
    codec_io_t io = {mio_read, mio_seek, NULL, &m};
    codec_params_t p = {.kind = CODEC_FLAC, .rate = fi.rate, .channels = fi.channels, .bits = fi.bits, .total_frames = fi.total_frames, .verify = true};
    codec_t c;
    ASSERT_EQ(CODEC_OK, codec_open(&c, &p, &io));
    static int32_t out[1024];
    int32_t k, last = 1;
    while ((k = codec_read(&c, out, 256)) > 0) last = k;
    (void)last;
    ASSERT_EQ(-CODEC_ERR_TRUNCATED, k); // an early end is an error, so "MD5 OK" / "finished" cannot be reported
    free(stream);
    free(f.d);
}

TEST(alac_partial_frame_count_beyond_frame_length_is_rejected) {
    blob_t f = load("alac16_44k_st.m4a");
    mp4_info_t mi;
    ASSERT_EQ(META_OK, mp4_parse(mem_rd, &f, f.n, &mi));
    alac_dec_t d;
    ASSERT_EQ(0, alac_dec_init(&d, mi.cookie, mi.cookie_len, malloc));
    // bits: CPE tag(001) instance(0000) unused(12 zeros) header 4 bits: partial=1 shift=00 escape=0 -> 1000, then a 32-bit count
    uint8_t pkt[64] = {0};
    size_t bit = 0;
    #define PUT(v, nb) do { for (int q = (nb) - 1; q >= 0; q--, bit++) if (((v) >> q) & 1) pkt[bit / 8] |= (uint8_t)(0x80u >> (bit % 8)); } while (0)
    PUT(1u, 3);
    PUT(0u, 4);
    PUT(0u, 12);
    PUT(8u, 4);
    PUT(0x00FFFFFFu, 32); // 16.7 million samples: far beyond frameLength
    #undef PUT
    uint8_t out[8192 * 4];
    uint32_t frames = 0;
    ASSERT_EQ(kALAC_ParamError, alac_dec_decode(&d, pkt, sizeof pkt, out, &frames));
    // the same for a mono element (SCE)
    memset(pkt, 0, sizeof pkt);
    bit = 0;
    #define PUT(v, nb) do { for (int q = (nb) - 1; q >= 0; q--, bit++) if (((v) >> q) & 1) pkt[bit / 8] |= (uint8_t)(0x80u >> (bit % 8)); } while (0)
    PUT(0u, 3);
    PUT(0u, 4);
    PUT(0u, 12);
    PUT(8u, 4);
    PUT(0x00FFFFFFu, 32);
    #undef PUT
    ASSERT_EQ(kALAC_ParamError, alac_dec_decode(&d, pkt, sizeof pkt, out, &frames));
    free(d.mixU); free(d.mixV); free(d.pred);
    free(f.d);
}

TEST(text_conversion_never_overruns) {
    char dst[8];
    const uint8_t lat[] = "0123456789ééééééé";
    meta_text_to_utf8(dst, sizeof dst, lat, sizeof lat - 1, 0); // fills the buffer, must stay inside and terminated
    ASSERT_TRUE(strlen(dst) < sizeof dst);
    char tmp[16];
    memset(tmp, 'Z', sizeof tmp); // uninitialised-looking buffer, as on a stack
    const uint8_t trck[] = "12345678901234567890";
    meta_text_to_utf8(tmp, sizeof tmp, trck, sizeof trck - 1, 0);
    ASSERT_EQ(15, strlen(tmp));
    // a lone high surrogate and a pair in UTF-16
    const uint8_t u16[] = {0xFF, 0xFE, 'A', 0, 0x3D, 0xD8, 0x00, 0xDE, 'B', 0};
    meta_text_to_utf8(dst, sizeof dst, u16, sizeof u16, 1);
    ASSERT_EQ(0, strcmp(dst, "A?B"));
}

TEST(id3_oversized_latin1_track_does_not_overrun) {
    blob_t f = load("mp3_44k_st.mp3");
    mp3_info_t plain;
    ASSERT_EQ(META_OK, mp3_parse(mem_rd, &f, f.n, &plain));
    uint8_t buf[512];
    memset(buf, 0, sizeof buf);
    memcpy(buf, "ID3\x03\x00\x00", 6);
    size_t n = 10;
    memcpy(buf + n, "TRCK", 4);
    put_be32(buf + n + 4, 31);
    buf[n + 10] = 0;
    memcpy(buf + n + 11, "123456789012345678901234567890", 30);
    n += 10 + 31;
    size_t tag = n - 10;
    buf[9] = (uint8_t)(tag & 0x7F); buf[8] = (uint8_t)((tag >> 7) & 0x7F);
    memcpy(buf + n, f.d + plain.audio_offset, 300);
    blob_t m = {buf, n + 300};
    mp3_info_t mi;
    meta_err_t e = mp3_parse(mem_rd, &m, m.n, &mi);
    ASSERT_TRUE(e == META_OK || e == META_E_FORMAT);
    free(f.d);
}

TEST(mp4_giant_and_looping_boxes_are_bounded) {
    uint8_t buf[64] = {0};
    put_be32(buf, 16); memcpy(buf + 4, "ftyp", 4); memcpy(buf + 8, "M4A ", 4);
    put_be32(buf + 16, 1); memcpy(buf + 20, "moov", 4); // 64-bit size follows
    for (int i = 0; i < 8; i++) buf[24 + i] = 0xFF;      // ~2^64: must not wrap past the file end
    blob_t b = {buf, 40};
    mp4_info_t mi;
    ASSERT_TRUE(mp4_parse(mem_rd, &b, 40, &mi) != META_OK);
    // a chain of empty 8-byte boxes is cut off by the box budget instead of walking for ever
    static uint8_t many[8 * 30000 + 16];
    put_be32(many, 16); memcpy(many + 4, "ftyp", 4); memcpy(many + 8, "M4A ", 4);
    for (size_t i = 16; i + 8 <= sizeof many; i += 8) { put_be32(many + i, 8); memcpy(many + i + 4, "free", 4); }
    blob_t mb = {many, sizeof many};
    ASSERT_TRUE(mp4_parse(mem_rd, &mb, sizeof many, &mi) != META_OK);
}

TEST(stream_io_abort_is_sticky) {
    h_t h;
    h_start(&h, 0);
    h.mode = 1;
    sio_t s = h_sio(&h);
    h.stop_poll = 1;
    uint8_t buf[16];
    ASSERT_EQ(0, sio_read(&s, buf, sizeof buf));
    ASSERT_TRUE(s.aborted);
    h.stop_poll = 0; // even though polling would now say "go on", the earlier abort stands until the owner clears it
    ASSERT_EQ(0, sio_read(&s, buf, sizeof buf));
    ASSERT_FALSE(sio_seek(&s, 10, 0));
    ASSERT_TRUE(sio_aborted(&s));
    h.mode = 9;
    pthread_join(h.th, NULL);
}

int main(void) {
    static uint8_t scratch[2048];
    meta_set_scratch(scratch, sizeof scratch);
    if (getenv("NEUPOD_BENCH")) {
        bench_one("flac16_44k_st.flac", CODEC_FLAC);
        bench_one("flac24_96k_st.flac", CODEC_FLAC);
        bench_one("mp3_44k_st.mp3", CODEC_MP3);
        bench_one("alac16_44k_st.m4a", CODEC_ALAC);
        bench_one("alac24_48k_st.m4a", CODEC_ALAC);
        return 0;
    }
    RUN(md5_rfc1321_vectors);
    RUN(arena_semantics);
    RUN(arena_worst_cases_fit);
    RUN(flac_16_44k_stereo_bit_exact);
    RUN(flac_24_96k_stereo_bit_exact);
    RUN(flac_24_48k_stereo_bit_exact);
    RUN(flac_16_48k_mono_bit_exact_and_duplicated);
    RUN(flac_header_roundtrip_and_rejections);
    RUN(flac_vorbis_comments);
    RUN(mp3_decode_length_and_level);
    RUN(mp3_id3v2_tags);
    RUN(alac_16_44k_stereo_bit_exact_with_tags);
    RUN(alac_24_48k_stereo_bit_exact_co64);
    RUN(alac_16_48k_mono_bit_exact);
    RUN(mp4_aac_is_recognised_and_garbage_rejected);
    RUN(mp4_iterator_sample_table_maths);
    RUN(stream_io_reads_whole_stream_to_eof);
    RUN(stream_io_abort_while_waiting_does_not_deadlock);
    RUN(stream_io_forward_and_backward_seek);
    RUN(stream_io_abort_is_sticky);
    RUN(text_conversion_never_overruns);
    RUN(id3_oversized_latin1_track_does_not_overrun);
    RUN(mp4_giant_and_looping_boxes_are_bounded);
    RUN(alac_partial_frame_count_beyond_frame_length_is_rejected);
    RUN(damaged_flac_is_not_reported_as_finished_or_md5_ok);
    RUN(hostile_flac_truncated_and_flipped);
    RUN(hostile_mp3_truncated_and_flipped);
    RUN(hostile_m4a_truncated_and_flipped);
    return RESULT();
}

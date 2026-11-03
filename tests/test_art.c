// SPDX-License-Identifier: MPL-2.0
// Album art: the streamed JPEG decoder (TJpgDec) against generated pictures, the source order, and the picture-position parsing of
// the tag readers (MP3 APIC / PIC, FLAC PICTURE, MP4 covr), including that a big PNG is skipped without reading its body.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "meta.h"
#include "unit.h"

#ifndef NEUPOD_ART_ASSETS
#error "NEUPOD_ART_ASSETS must name tests/assets/art"
#endif

// ---- a file in memory, with read accounting ---------------------------------------------------------------------------

typedef struct {
    const uint8_t *d;
    uint64_t n;
    uint64_t dlen; // bytes really present (0 = all n); the rest of the file reads as zeros and is never stored
    uint64_t total, max_chunk, reads;
    uint64_t watch_lo, watch_hi, watched; // bytes read inside [watch_lo, watch_hi)
    uint64_t watch_skip;                  // ... not counting the first watch_skip bytes of it (the 4 that identify the format)
} memf_t;

static int32_t mem_rd(void *ctx, uint64_t off, void *dst, uint32_t n) {
    memf_t *m = ctx;
    if (off >= m->n) return 0;
    if (n > m->n - off) n = (uint32_t)(m->n - off);
    uint64_t real = m->dlen != 0 ? m->dlen : m->n;
    uint32_t k = off < real ? (uint32_t)(real - off < n ? real - off : n) : 0;
    memcpy(dst, m->d + off, k);
    memset((uint8_t *)dst + k, 0, n - k);
    m->total += n;
    m->reads++;
    if (n > m->max_chunk) m->max_chunk = n;
    if (m->watch_hi != 0) {
        uint64_t a = off > m->watch_lo + m->watch_skip ? off : m->watch_lo + m->watch_skip, b = off + n < m->watch_hi ? off + n : m->watch_hi;
        if (b > a) m->watched += b - a;
    }
    return (int32_t)n;
}

static uint8_t *load(const char *name, size_t *len) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", NEUPOD_ART_ASSETS, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("  cannot open %s\n", path);
        *len = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)sz);
    size_t got = fread(b, 1, (size_t)sz, f);
    fclose(f);
    *len = got;
    return b;
}

// ---- JPEG decoding --------------------------------------------------------------------------------------------------

static uint16_t g_px[ART_PIXELS];

static const uint8_t k_col[4][3] = {{220, 40, 40}, {40, 200, 60}, {50, 60, 220}, {230, 210, 40}};

static int near_col(uint16_t p, const uint8_t c[3]) {
    int r = ((p >> 11) & 31) * 255 / 31, g = ((p >> 5) & 63) * 255 / 63, b = (p & 31) * 255 / 31;
    return abs(r - c[0]) < 32 && abs(g - c[1]) < 32 && abs(b - c[2]) < 32;
}

// Decode a whole asset and check size, aspect ratio and the colour at the middle of each quadrant.
static void check_jpeg(const char *name, uint32_t src_w, uint32_t src_h, int scale, bool gray) {
    size_t n;
    uint8_t *d = load(name, &n);
    ASSERT_TRUE(d != NULL);
    if (d == NULL) return;
    memf_t m = {.d = d, .n = n};
    art_info_t info;
    memset(g_px, 0xA5, sizeof g_px);
    art_err_t e = art_decode(mem_rd, &m, 0, (uint32_t)n, g_px, &info, NULL, NULL);
    if (e != ART_OK) printf("  %s: %s\n", name, art_err_str(e));
    ASSERT_EQ(ART_OK, e);
    if (e == ART_OK) {
        ASSERT_EQ(src_w, info.src_w);
        ASSERT_EQ(src_h, info.src_h);
        ASSERT_EQ(scale, info.scale);
        uint32_t big = src_w > src_h ? src_w : src_h;
        if (big >= ART_MAX_DIM) {
            ASSERT_EQ(ART_MAX_DIM, info.w > info.h ? info.w : info.h); // the long side fills the box
        } else {
            ASSERT_EQ(src_w, info.w); // small pictures are not enlarged
            ASSERT_EQ(src_h, info.h);
        }
        // same aspect ratio within 3 %
        ASSERT_TRUE(abs((int)(info.w * src_h) - (int)(info.h * src_w)) * 100 < 3 * (int)(info.h * src_w));
        if (!gray) {
            for (int q = 0; q < 4; q++) {
                uint32_t x = (q & 1 ? 3u : 1u) * info.w / 4u, y = (q & 2 ? 3u : 1u) * info.h / 4u;
                int ok = near_col(g_px[y * info.w + x], k_col[q]);
                if (!ok) printf("  %s quadrant %d: pixel %04x\n", name, q, g_px[y * info.w + x]);
                ASSERT_TRUE(ok);
            }
        } else { // grey: red quadrant darker than yellow
            uint16_t a = g_px[(info.h / 4) * info.w + info.w / 4], b = g_px[(3 * info.h / 4) * info.w + 3 * info.w / 4];
            ASSERT_TRUE(((a >> 5) & 63) < ((b >> 5) & 63));
        }
        ASSERT_TRUE(m.max_chunk <= 512); // small SD reads only
        ASSERT_TRUE(m.total <= n);       // nothing read twice, nothing beyond the file
    }
    free(d);
}

TEST(test_decode_420_600x600) { check_jpeg("q420_600x600.jpg", 600, 600, 3, false); }
TEST(test_decode_420_500x333) { check_jpeg("q420_500x333.jpg", 500, 333, 2, false); }
TEST(test_decode_444_128) { check_jpeg("q444_128x128.jpg", 128, 128, 1, false); }
TEST(test_decode_422_odd) { check_jpeg("q422_517x389.jpg", 517, 389, 3, false); }
TEST(test_decode_420_1000x700) { check_jpeg("q420_1000x700.jpg", 1000, 700, 3, false); }
TEST(test_decode_tiny) { check_jpeg("q420_40x30.jpg", 40, 30, 0, false); }
TEST(test_decode_gray) { check_jpeg("qgray_200x150.jpg", 200, 150, 1, true); }
TEST(test_decode_restart_markers) { check_jpeg("qrst_257x193.jpg", 257, 193, 2, false); }

TEST(test_progressive_is_refused) {
    size_t n;
    uint8_t *d = load("qprog_300x300.jpg", &n);
    ASSERT_TRUE(d != NULL);
    if (d == NULL) return;
    memf_t m = {.d = d, .n = n};
    art_info_t info;
    ASSERT_EQ(ART_E_UNSUPPORTED, art_decode(mem_rd, &m, 0, (uint32_t)n, g_px, &info, NULL, NULL));
    free(d);
}

TEST(test_damaged_input) {
    size_t n;
    uint8_t *d = load("q420_600x600.jpg", &n);
    ASSERT_TRUE(d != NULL);
    if (d == NULL) return;
    art_info_t info;
    memf_t half = {.d = d, .n = n / 2}; // the card ends in the middle of the picture
    ASSERT_TRUE(art_decode(mem_rd, &half, 0, (uint32_t)n, g_px, &info, NULL, NULL) != ART_OK);
    memf_t cut = {.d = d, .n = n};
    ASSERT_TRUE(art_decode(mem_rd, &cut, 0, (uint32_t)n / 3, g_px, &info, NULL, NULL) != ART_OK); // declared shorter than it is
    uint8_t junk[300];
    memset(junk, 0x5A, sizeof junk);
    memf_t j = {.d = junk, .n = sizeof junk};
    ASSERT_EQ(ART_E_FORMAT, art_decode(mem_rd, &j, 0, sizeof junk, g_px, &info, NULL, NULL));
    ASSERT_EQ(ART_E_FORMAT, art_decode(mem_rd, &j, 0, 1, g_px, &info, NULL, NULL));
    ASSERT_EQ(ART_E_FORMAT, art_decode(mem_rd, &j, 0, 0, g_px, &info, NULL, NULL));
    // bit flips anywhere must end in an error or a picture, never a crash or an overrun (run under ASan in CI)
    uint8_t *w = malloc(n);
    for (size_t i = 0; i < n; i += 97) {
        memcpy(w, d, n);
        w[i] ^= 0xFF;
        memf_t f = {.d = w, .n = n};
        (void)art_decode(mem_rd, &f, 0, (uint32_t)n, g_px, &info, NULL, NULL);
    }
    free(w);
    free(d);
}

TEST(test_embedded_offset) {
    size_t n;
    uint8_t *d = load("q444_128x128.jpg", &n);
    ASSERT_TRUE(d != NULL);
    if (d == NULL) return;
    size_t pre = 1234, post = 777;
    uint8_t *buf = malloc(pre + n + post);
    memset(buf, 0xEE, pre + n + post);
    memcpy(buf + pre, d, n);
    memf_t m = {.d = buf, .n = pre + n + post};
    art_info_t info;
    ASSERT_EQ(ART_OK, art_decode(mem_rd, &m, pre, (uint32_t)n, g_px, &info, NULL, NULL));
    ASSERT_EQ(128, info.src_w);
    ASSERT_TRUE(m.total <= n); // never read outside the picture
    free(buf);
    free(d);
}

static int g_yield_calls, g_yield_limit;
static bool count_yield(void *ctx) {
    (void)ctx;
    return ++g_yield_calls < g_yield_limit;
}

TEST(test_yield_and_abort) {
    size_t n;
    uint8_t *d = load("q420_600x600.jpg", &n);
    ASSERT_TRUE(d != NULL);
    if (d == NULL) return;
    art_info_t info;
    memf_t m = {.d = d, .n = n};
    g_yield_calls = 0;
    g_yield_limit = 1 << 30;
    ASSERT_EQ(ART_OK, art_decode(mem_rd, &m, 0, (uint32_t)n, g_px, &info, count_yield, NULL));
    ASSERT_TRUE(g_yield_calls > (int)(n / 512)); // at least once per card read, and once per MCU
    for (int limit = 1; limit < 400; limit += 37) {
        memf_t a = {.d = d, .n = n};
        g_yield_calls = 0;
        g_yield_limit = limit;
        ASSERT_EQ(ART_E_ABORTED, art_decode(mem_rd, &a, 0, (uint32_t)n, g_px, &info, count_yield, NULL));
    }
    free(d);
}

// ---- which picture --------------------------------------------------------------------------------------------------

TEST(test_folder_rank_and_pick) {
    ASSERT_EQ(1, art_folder_rank("cover.jpg"));
    ASSERT_EQ(1, art_folder_rank("COVER.JPG"));
    ASSERT_EQ(2, art_folder_rank("Folder.Jpg"));
    ASSERT_EQ(3, art_folder_rank("front.jpg"));
    ASSERT_EQ(0, art_folder_rank("cover.png"));
    ASSERT_EQ(0, art_folder_rank("cover.jpeg"));
    ASSERT_EQ(0, art_folder_rank("mycover.jpg"));
    ASSERT_EQ(0, art_folder_rank("cover.jpg.bak"));
    ASSERT_EQ(0, art_folder_rank(""));
    ASSERT_EQ(0, art_folder_rank(NULL));
}

typedef struct {
    const char *const *names;
    int n, i;
} list_t;
static bool list_next(void *ctx, char *name, size_t cap) {
    list_t *l = ctx;
    if (l->i >= l->n) return false;
    snprintf(name, cap, "%s", l->names[l->i++]);
    return true;
}

TEST(test_folder_pick_order) {
    char best[64];
    const char *a[] = {"01 Song.flac", "Front.JPG", "folder.jpg", "notes.txt", "Cover.Jpg"};
    list_t l = {a, 5, 0};
    ASSERT_EQ(1, art_folder_pick(list_next, &l, best, sizeof best)); // cover beats folder beats front, whatever the listing order
    ASSERT_EQ(0, strcmp(best, "Cover.Jpg"));
    const char *b[] = {"front.jpg", "x.mp3", "FOLDER.JPG"};
    list_t l2 = {b, 3, 0};
    ASSERT_EQ(2, art_folder_pick(list_next, &l2, best, sizeof best));
    ASSERT_EQ(0, strcmp(best, "FOLDER.JPG"));
    const char *c[] = {"a.flac", "cover.png", "art.jpg"};
    list_t l3 = {c, 3, 0};
    ASSERT_EQ(0, art_folder_pick(list_next, &l3, best, sizeof best));
    ASSERT_EQ(0, (int)strlen(best));
    list_t l4 = {c, 0, 0};
    ASSERT_EQ(0, art_folder_pick(list_next, &l4, best, sizeof best));
}

TEST(test_source_order) {
    art_src_t o[2];
    art_ref_t jpg = {.offset = 100, .length = 5000, .kind = ART_JPEG, .type = 3};
    art_ref_t png = {.offset = 100, .length = 5000, .kind = ART_PNG, .type = 3};
    art_ref_t none = {0};
    ASSERT_EQ(2, art_sources(&jpg, true, o)); // embedded first, then the folder
    ASSERT_EQ(ART_SRC_EMBEDDED, o[0]);
    ASSERT_EQ(ART_SRC_FOLDER, o[1]);
    ASSERT_EQ(1, art_sources(&jpg, false, o));
    ASSERT_EQ(ART_SRC_EMBEDDED, o[0]);
    ASSERT_EQ(1, art_sources(&png, true, o)); // an embedded PNG cannot be shown: the cover file is tried
    ASSERT_EQ(ART_SRC_FOLDER, o[0]);
    ASSERT_EQ(0, art_sources(&png, false, o)); // PNG and nothing else: placeholder
    ASSERT_EQ(1, art_sources(&none, true, o));
    ASSERT_EQ(ART_SRC_FOLDER, o[0]);
    ASSERT_EQ(0, art_sources(&none, false, o));
    ASSERT_EQ(1, art_sources(NULL, true, o));
}

TEST(test_offer_rules) {
    art_ref_t best = {0};
    art_ref_t png3 = {.offset = 1, .length = 10, .kind = ART_PNG, .type = 3};
    art_ref_t jpg0 = {.offset = 2, .length = 10, .kind = ART_JPEG, .type = 0};
    art_ref_t jpg3 = {.offset = 3, .length = 10, .kind = ART_JPEG, .type = 3};
    art_ref_t jpg3b = {.offset = 4, .length = 10, .kind = ART_JPEG, .type = 3};
    art_ref_t oth = {.offset = 5, .length = 10, .kind = ART_OTHER, .type = 3};
    meta_art_offer(&best, &png3);
    ASSERT_EQ(1, best.offset);
    meta_art_offer(&best, &jpg0); // a JPEG beats a PNG even if it is not the front cover
    ASSERT_EQ(2, best.offset);
    meta_art_offer(&best, &jpg3); // a front cover beats another JPEG
    ASSERT_EQ(3, best.offset);
    meta_art_offer(&best, &jpg3b); // ties keep the first
    ASSERT_EQ(3, best.offset);
    meta_art_offer(&best, &oth);
    ASSERT_EQ(3, best.offset);
    art_ref_t b2 = {0};
    meta_art_offer(&b2, &oth);
    ASSERT_EQ(ART_OTHER, b2.kind);
}

// ---- picture positions in the tag readers ------------------------------------------------------------------------

typedef struct {
    uint8_t b[2048];
    size_t n;
} wbuf_t;
static void w8(wbuf_t *w, uint8_t v) { w->b[w->n++] = v; }
static void wbe32(wbuf_t *w, uint32_t v) {
    w8(w, (uint8_t)(v >> 24));
    w8(w, (uint8_t)(v >> 16));
    w8(w, (uint8_t)(v >> 8));
    w8(w, (uint8_t)v);
}
static void wmem(wbuf_t *w, const void *p, size_t n) {
    memcpy(w->b + w->n, p, n);
    w->n += n;
}
static const uint8_t k_png_magic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t k_jpg_magic[4] = {0xFF, 0xD8, 0xFF, 0xE0};

// A "file" whose header is real and whose big picture body is virtual zeros (never allocated); reads there are counted.
// FLAC: STREAMINFO + PICTURE blocks given by the caller.
static void flac_streaminfo(wbuf_t *w, bool last) {
    wmem(w, "fLaC", 4);
    w8(w, last ? 0x80 : 0x00);
    w8(w, 0);
    w8(w, 0);
    w8(w, 34);
    uint8_t si[34] = {0};
    si[0] = 0x10; // min block 4096
    si[1] = 0x00;
    si[2] = 0x10; // max block 4096
    si[3] = 0x00;
    // 44100 Hz (0x0AC44), 2 channels, 16 bits: rate<<12 | (ch-1)<<9 | (bits-1)<<4
    si[10] = 0x0A;
    si[11] = 0xC4;
    si[12] = 0x42;
    si[13] = 0xF0;
    wmem(w, si, 34);
}

// PICTURE block header (type byte + length) and the fields up to the image data; the caller appends the image bytes.
static void flac_picture_head(wbuf_t *w, bool last, uint32_t ptype, const char *mime, const char *desc, uint32_t data_len) {
    uint32_t body = 32 + (uint32_t)strlen(mime) + (uint32_t)strlen(desc) + data_len;
    w8(w, (uint8_t)(6 | (last ? 0x80 : 0)));
    w8(w, (uint8_t)(body >> 16));
    w8(w, (uint8_t)(body >> 8));
    w8(w, (uint8_t)body);
    wbe32(w, ptype);
    wbe32(w, (uint32_t)strlen(mime));
    wmem(w, mime, strlen(mime));
    wbe32(w, (uint32_t)strlen(desc));
    wmem(w, desc, strlen(desc));
    wbe32(w, 500);
    wbe32(w, 500);
    wbe32(w, 24);
    wbe32(w, 0);
    wbe32(w, data_len);
}

TEST(test_flac_big_png_is_skipped) {
    static wbuf_t w;
    w.n = 0;
    flac_streaminfo(&w, false);
    uint32_t png_len = 2u * 1024u * 1024u;
    flac_picture_head(&w, true, 3, "image/png", "", png_len);
    size_t body_at = w.n;
    wmem(&w, k_png_magic, 8); // the rest of the 2 MB PNG is virtual
    memf_t m = {.d = w.b, .n = body_at + png_len + 4096, .dlen = w.n};
    m.watch_lo = body_at;
    m.watch_hi = body_at + png_len;
    m.watch_skip = 4; // the four bytes that tell PNG from JPEG
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m, m.n, &fi));
    ASSERT_EQ(ART_PNG, fi.tags.art.kind);
    ASSERT_EQ(png_len, fi.tags.art.length);
    ASSERT_EQ(body_at, fi.tags.art.offset);
    ASSERT_EQ(3, fi.tags.art.type);
    ASSERT_EQ(0, m.watched); // the body was not read
    ASSERT_TRUE(m.total < 200);
    ASSERT_EQ(body_at + png_len, fi.audio_offset);
}

TEST(test_flac_picture_choice) {
    static wbuf_t w;
    w.n = 0;
    flac_streaminfo(&w, false);
    // 1) a back cover (type 4) JPEG, 2) the front cover as PNG, 3) a front cover JPEG: the last one wins
    flac_picture_head(&w, false, 4, "image/jpeg", "back", 4);
    size_t at1 = w.n;
    wmem(&w, k_jpg_magic, 4);
    flac_picture_head(&w, false, 3, "image/png", "", 8);
    wmem(&w, k_png_magic, 8);
    flac_picture_head(&w, true, 3, "image/jpeg", "front cover", 4);
    size_t at3 = w.n;
    wmem(&w, k_jpg_magic, 4);
    memf_t m = {.d = w.b, .n = w.n + 100, .dlen = w.n};
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m, m.n, &fi));
    ASSERT_EQ(ART_JPEG, fi.tags.art.kind);
    ASSERT_EQ(at3, fi.tags.art.offset);
    ASSERT_EQ(4, fi.tags.art.length);
    ASSERT_EQ(3, fi.tags.art.type);
    ASSERT_TRUE(at1 != at3);
    // the mime string lies: bytes say PNG
    w.n = 0;
    flac_streaminfo(&w, false);
    flac_picture_head(&w, true, 3, "image/jpeg", "", 8);
    wmem(&w, k_png_magic, 8);
    memf_t m2 = {.d = w.b, .n = w.n + 10, .dlen = w.n};
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m2, m2.n, &fi));
    ASSERT_EQ(ART_PNG, fi.tags.art.kind);
}

TEST(test_flac_picture_hostile) {
    static wbuf_t w;
    // picture lengths and string lengths that point past the block must be ignored, not followed
    w.n = 0;
    flac_streaminfo(&w, false);
    flac_picture_head(&w, true, 3, "image/jpeg", "", 4);
    wmem(&w, k_jpg_magic, 4);
    size_t dl_at = w.n - 4 - 4; // the data-length field sits just before the data
    w.b[dl_at] = 0x7F; // data length 0x7F000004 claims far more than the block holds
    memf_t m = {.d = w.b, .n = w.n + 10, .dlen = w.n};
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m, m.n, &fi));
    ASSERT_EQ(ART_NONE, fi.tags.art.kind);
    w.n = 0;
    flac_streaminfo(&w, false);
    flac_picture_head(&w, true, 3, "image/jpeg", "", 4);
    wmem(&w, k_jpg_magic, 4);
    w.b[42 + 4 + 4 + 3] = 0xFF; // mime length 255 (past the block start region)
    w.b[42 + 4 + 4 + 2] = 0xFF;
    memf_t m2 = {.d = w.b, .n = w.n + 10, .dlen = w.n};
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m2, m2.n, &fi));
    ASSERT_EQ(ART_NONE, fi.tags.art.kind);
}

// ID3v2: header, then frames.
static void id3_head(wbuf_t *w, uint8_t ver, uint8_t flags, uint32_t size) {
    wmem(w, "ID3", 3);
    w8(w, ver);
    w8(w, 0);
    w8(w, flags);
    w8(w, (uint8_t)((size >> 21) & 0x7F));
    w8(w, (uint8_t)((size >> 14) & 0x7F));
    w8(w, (uint8_t)((size >> 7) & 0x7F));
    w8(w, (uint8_t)(size & 0x7F));
}
static void id3_frame_head(wbuf_t *w, uint8_t ver, const char *id, uint32_t size, uint8_t flags2) {
    wmem(w, id, ver == 2 ? 3 : 4);
    if (ver == 2) {
        w8(w, (uint8_t)(size >> 16));
        w8(w, (uint8_t)(size >> 8));
        w8(w, (uint8_t)size);
        return;
    }
    if (ver == 4) {
        w8(w, (uint8_t)((size >> 21) & 0x7F));
        w8(w, (uint8_t)((size >> 14) & 0x7F));
        w8(w, (uint8_t)((size >> 7) & 0x7F));
        w8(w, (uint8_t)(size & 0x7F));
    } else {
        wbe32(w, size);
    }
    w8(w, 0);
    w8(w, flags2);
}

TEST(test_mp3_apic_v23_jpeg) {
    static wbuf_t w;
    w.n = 0;
    uint32_t img = 100000; // declared; only the first bytes are real
    const char *mime = "image/jpeg";
    uint32_t apic = 1 + (uint32_t)strlen(mime) + 1 + 1 + 6 + img; // enc, mime\0, type, "cover\0", data
    uint32_t tit = 1 + 5;
    id3_head(&w, 3, 0, 10 + tit + 10 + apic);
    id3_frame_head(&w, 3, "TIT2", tit, 0);
    w8(&w, 0);
    wmem(&w, "Hello", 5);
    id3_frame_head(&w, 3, "APIC", apic, 0);
    w8(&w, 0);
    wmem(&w, mime, strlen(mime) + 1);
    w8(&w, 3);
    wmem(&w, "cover\0", 6);
    size_t at = w.n;
    wmem(&w, k_jpg_magic, 4);
    size_t tag_end = 10 + 10 + tit + 10 + apic;
    memf_t m = {.d = w.b, .n = tag_end, .dlen = w.n};
    m.watch_lo = at;
    m.watch_hi = at + img;
    m.watch_skip = 4;
    mp3_info_t mi;
    (void)mp3_parse(mem_rd, &m, m.n, &mi); // no audio frames after the tag: the error does not matter, the tags are read first
    ASSERT_EQ(0, strcmp(mi.tags.title, "Hello"));
    ASSERT_EQ(ART_JPEG, mi.tags.art.kind);
    ASSERT_EQ(at, mi.tags.art.offset);
    ASSERT_EQ(img, mi.tags.art.length);
    ASSERT_EQ(3, mi.tags.art.type);
    ASSERT_TRUE(m.watched < 160); // only the look-ahead that holds mime and description, none of the picture proper
}

TEST(test_mp3_apic_png_1_5mb_skipped) {
    static wbuf_t w;
    w.n = 0;
    uint32_t img = 1500000;
    const char *mime = "image/png";
    uint32_t apic = 1 + (uint32_t)strlen(mime) + 1 + 1 + 1 + img;
    id3_head(&w, 3, 0, 10 + apic);
    id3_frame_head(&w, 3, "APIC", apic, 0);
    w8(&w, 0);
    wmem(&w, mime, strlen(mime) + 1);
    w8(&w, 3);
    w8(&w, 0);
    size_t at = w.n;
    wmem(&w, k_png_magic, 8);
    memf_t m = {.d = w.b, .n = at + img + 5000, .dlen = w.n};
    m.watch_lo = at;
    m.watch_hi = at + img;
    m.watch_skip = 4;
    mp3_info_t mi;
    (void)mp3_parse(mem_rd, &m, m.n, &mi);
    ASSERT_EQ(ART_PNG, mi.tags.art.kind);
    ASSERT_EQ(img, mi.tags.art.length);
    ASSERT_TRUE(m.watched < 160); // 1.5 MB body: only the mime/description look-ahead was read
}

TEST(test_mp3_apic_variants) {
    static wbuf_t w;
    // v2.4 with a data length indicator (4 extra bytes before the payload) and a UTF-16 description
    w.n = 0;
    uint32_t apic = 4 + 1 + 11 + 1 + 4 + 4; // dli, enc, "image/jpeg\0", type, description "\xFF\xFE" "a\0" "\0\0", data(4)
    apic = 4 + 1 + 11 + 1 + 6 + 4;
    id3_head(&w, 4, 0, 10 + apic);
    id3_frame_head(&w, 4, "APIC", apic, 0x01);
    wbe32(&w, 0);
    w8(&w, 1);
    wmem(&w, "image/jpeg\0", 11);
    w8(&w, 3);
    wmem(&w, "\xFF\xFE" "a\0" "\0\0", 6);
    size_t at = w.n;
    wmem(&w, k_jpg_magic, 4);
    memf_t m = {.d = w.b, .n = w.n + 3000, .dlen = w.n};
    mp3_info_t mi;
    (void)mp3_parse(mem_rd, &m, m.n, &mi);
    ASSERT_EQ(ART_JPEG, mi.tags.art.kind);
    ASSERT_EQ(at, mi.tags.art.offset);
    ASSERT_EQ(4, mi.tags.art.length);

    // v2.2 PIC: encoding, "JPG", type, description, data
    w.n = 0;
    uint32_t pic = 1 + 3 + 1 + 1 + 4;
    id3_head(&w, 2, 0, 6 + pic);
    id3_frame_head(&w, 2, "PIC", pic, 0);
    w8(&w, 0);
    wmem(&w, "JPG", 3);
    w8(&w, 0);
    w8(&w, 0);
    at = w.n;
    wmem(&w, k_jpg_magic, 4);
    memf_t m2 = {.d = w.b, .n = w.n + 3000, .dlen = w.n};
    (void)mp3_parse(mem_rd, &m2, m2.n, &mi);
    ASSERT_EQ(ART_JPEG, mi.tags.art.kind);
    ASSERT_EQ(at, mi.tags.art.offset);

    // a whole-tag unsynchronised v2.3 tag cannot be streamed: no picture, text still read
    w.n = 0;
    uint32_t a3 = 1 + 11 + 1 + 1 + 4;
    id3_head(&w, 3, 0x80, 10 + a3);
    id3_frame_head(&w, 3, "APIC", a3, 0);
    w8(&w, 0);
    wmem(&w, "image/jpeg\0", 11);
    w8(&w, 3);
    w8(&w, 0);
    wmem(&w, k_jpg_magic, 4);
    memf_t m3 = {.d = w.b, .n = w.n + 3000, .dlen = w.n};
    (void)mp3_parse(mem_rd, &m3, m3.n, &mi);
    ASSERT_EQ(ART_NONE, mi.tags.art.kind);

    // compressed frame (v2.3 flag 0x80): ignored
    w.n = 0;
    id3_head(&w, 3, 0, 10 + a3);
    id3_frame_head(&w, 3, "APIC", a3, 0x80);
    w8(&w, 0);
    wmem(&w, "image/jpeg\0", 11);
    w8(&w, 3);
    w8(&w, 0);
    wmem(&w, k_jpg_magic, 4);
    memf_t m4 = {.d = w.b, .n = w.n + 3000, .dlen = w.n};
    (void)mp3_parse(mem_rd, &m4, m4.n, &mi);
    ASSERT_EQ(ART_NONE, mi.tags.art.kind);

    // a frame that claims more than the tag holds
    w.n = 0;
    id3_head(&w, 3, 0, 10 + a3);
    id3_frame_head(&w, 3, "APIC", a3 + 5000, 0);
    w8(&w, 0);
    wmem(&w, "image/jpeg\0", 11);
    w8(&w, 3);
    w8(&w, 0);
    wmem(&w, k_jpg_magic, 4);
    memf_t m5 = {.d = w.b, .n = w.n + 3000, .dlen = w.n};
    (void)mp3_parse(mem_rd, &m5, m5.n, &mi);
    ASSERT_EQ(ART_NONE, mi.tags.art.kind);
}

// MP4: ftyp + moov/udta/meta/ilst/covr/data. There is no audio track, so mp4_parse reports that, but the tags are read on the way.
static void box_head(wbuf_t *w, uint32_t size, const char *type) {
    wbe32(w, size);
    wmem(w, type, 4);
}
TEST(test_mp4_covr) {
    static wbuf_t w;
    w.n = 0;
    box_head(&w, 16, "ftyp");
    wmem(&w, "M4A \0\0\0\0", 8);
    uint32_t png = 1000000, jpg = 4;
    uint32_t d_png = 16 + png, d_jpg = 16 + jpg;
    uint32_t covr = 8 + d_png + d_jpg;
    uint32_t ilst = 8 + covr;
    uint32_t meta = 12 + ilst;
    uint32_t udta = 8 + meta;
    uint32_t moov = 8 + udta;
    box_head(&w, moov, "moov");
    box_head(&w, udta, "udta");
    box_head(&w, meta, "meta");
    wbe32(&w, 0);
    box_head(&w, ilst, "ilst");
    box_head(&w, covr, "covr");
    box_head(&w, d_png, "data");
    wbe32(&w, 14); // type code 14 = PNG
    wbe32(&w, 0);
    size_t png_at = w.n;
    wmem(&w, k_png_magic, 8);
    // the PNG body is virtual: the file image continues at the second data box
    static wbuf_t tail;
    tail.n = 0;
    box_head(&tail, d_jpg, "data");
    wbe32(&tail, 13); // 13 = JPEG
    wbe32(&tail, 0);
    size_t jpg_rel = tail.n;
    wmem(&tail, k_jpg_magic, 4);

    // a file image with a hole: head | virtual PNG body | tail
    size_t hole_lo = png_at + 8, hole_hi = png_at + png;
    size_t total = hole_hi + tail.n;
    uint8_t *img = calloc(1, total);
    memcpy(img, w.b, w.n);
    memcpy(img + hole_hi, tail.b, tail.n);
    memf_t m = {.d = img, .n = total};
    m.watch_lo = hole_lo;
    m.watch_hi = hole_hi;
    mp4_info_t mp;
    meta_err_t e = mp4_parse(mem_rd, &m, total, &mp);
    ASSERT_EQ(META_E_NO_AUDIO, e);
    ASSERT_EQ(ART_JPEG, mp.tags.art.kind); // the JPEG beats the bigger PNG
    ASSERT_EQ(hole_hi + jpg_rel, mp.tags.art.offset);
    ASSERT_EQ(4, mp.tags.art.length);
    ASSERT_EQ(0, m.watched);
    free(img);
}

TEST(test_mp4_covr_png_only) {
    static wbuf_t w;
    w.n = 0;
    box_head(&w, 16, "ftyp");
    wmem(&w, "M4A \0\0\0\0", 8);
    uint32_t png = 2000000;
    uint32_t dd = 16 + png, covr = 8 + dd, ilst = 8 + covr, meta = 12 + ilst, udta = 8 + meta, moov = 8 + udta;
    box_head(&w, moov, "moov");
    box_head(&w, udta, "udta");
    box_head(&w, meta, "meta");
    wbe32(&w, 0);
    box_head(&w, ilst, "ilst");
    box_head(&w, covr, "covr");
    box_head(&w, dd, "data");
    wbe32(&w, 14);
    wbe32(&w, 0);
    size_t at = w.n;
    wmem(&w, k_png_magic, 8);
    memf_t m = {.d = w.b, .n = at + png, .dlen = w.n};
    m.watch_lo = at;
    m.watch_hi = at + png;
    m.watch_skip = 4;
    mp4_info_t mp;
    ASSERT_EQ(META_E_NO_AUDIO, mp4_parse(mem_rd, &m, m.n, &mp));
    ASSERT_EQ(ART_PNG, mp.tags.art.kind);
    ASSERT_EQ(png, mp.tags.art.length);
    ASSERT_EQ(at, mp.tags.art.offset);
    ASSERT_EQ(0, m.watched);
}

// The repository's own test files carry no picture: the new fields stay empty and nothing else changes.
TEST(test_assets_without_art) {
    size_t n;
    uint8_t *d = load("../audio/flac16_44k_st.flac", &n);
    ASSERT_TRUE(d != NULL);
    if (d == NULL) return;
    memf_t m = {.d = d, .n = n};
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m, n, &fi));
    ASSERT_EQ(ART_NONE, fi.tags.art.kind);
    free(d);
}

int main(void) {
    RUN(test_decode_420_600x600);
    RUN(test_decode_420_500x333);
    RUN(test_decode_444_128);
    RUN(test_decode_422_odd);
    RUN(test_decode_420_1000x700);
    RUN(test_decode_tiny);
    RUN(test_decode_gray);
    RUN(test_decode_restart_markers);
    RUN(test_progressive_is_refused);
    RUN(test_damaged_input);
    RUN(test_embedded_offset);
    RUN(test_yield_and_abort);
    RUN(test_folder_rank_and_pick);
    RUN(test_folder_pick_order);
    RUN(test_source_order);
    RUN(test_offer_rules);
    RUN(test_flac_big_png_is_skipped);
    RUN(test_flac_picture_choice);
    RUN(test_flac_picture_hostile);
    RUN(test_mp3_apic_v23_jpeg);
    RUN(test_mp3_apic_png_1_5mb_skipped);
    RUN(test_mp3_apic_variants);
    RUN(test_mp4_covr);
    RUN(test_mp4_covr_png_only);
    RUN(test_assets_without_art);
    RESULT();
}

// SPDX-License-Identifier: MPL-2.0
#include "art.h"

#include <stdlib.h>
#include <string.h>

#include "tjpgd.h"

#ifdef PICO_ON_DEVICE
/* The SDK links malloc through --wrap and the wrapper panics on exhaustion; art is optional, so use the real allocator. */
extern void *__real_malloc(size_t n);
#define ART_MALLOC __real_malloc
#else
#define ART_MALLOC malloc
#endif

const char *art_err_str(art_err_t e) {
    switch (e) {
    case ART_OK: return "ok";
    case ART_E_READ: return "cannot read the picture";
    case ART_E_FORMAT: return "not a readable JPEG";
    case ART_E_UNSUPPORTED: return "progressive or unsupported JPEG";
    case ART_E_TOO_BIG: return "picture too large";
    case ART_E_MEMORY: return "no memory for the decoder";
    case ART_E_ABORTED: return "interrupted";
    }
    return "?";
}

// ---- which picture --------------------------------------------------------------------------------------------

size_t art_sources(const art_ref_t *embedded, bool folder_has, art_src_t out[2]) {
    size_t n = 0;
    if (embedded != NULL && embedded->kind == ART_JPEG && embedded->length >= 4) out[n++] = ART_SRC_EMBEDDED;
    if (folder_has) out[n++] = ART_SRC_FOLDER;
    return n;
}

static bool name_is(const char *a, const char *lower) { // a (any case) equals lower (lower-case ASCII)
    for (; *lower != '\0'; a++, lower++) {
        char c = *a;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != *lower) return false;
    }
    return *a == '\0';
}

int art_folder_rank(const char *name) {
    if (name == NULL) return 0;
    if (name_is(name, "cover.jpg")) return 1;
    if (name_is(name, "folder.jpg")) return 2;
    if (name_is(name, "front.jpg")) return 3;
    return 0;
}

int art_folder_pick(art_name_next_fn next, void *ctx, char *best, size_t cap) {
    char name[128];
    int best_rank = 0;
    if (cap != 0) best[0] = '\0';
    while (next(ctx, name, sizeof name)) {
        int r = art_folder_rank(name);
        if (r != 0 && (best_rank == 0 || r < best_rank)) {
            best_rank = r;
            if (cap != 0) {
                strncpy(best, name, cap - 1);
                best[cap - 1] = '\0';
            }
            if (r == 1) break; // cannot be beaten
        }
    }
    return best_rank;
}

// ---- decoding -------------------------------------------------------------------------------------------------

typedef struct {
    meta_read_fn rd;
    void *rd_ctx;
    uint64_t base;
    uint32_t len, pos;
    art_yield_fn yield;
    void *yield_ctx;
    bool aborted, read_failed;
    // output
    uint16_t *dst;
    uint32_t sw, sh; // size after the decoder's own scaling
    uint32_t tw, th; // thumbnail size
    uint16_t *strip; // one MCU row of the scaled image, sw wide
    uint32_t strip_top;
    uint16_t *acc;   // running sums of R, G, B for the thumbnail row being built (tw * 3)
    uint8_t *colcnt; // source columns that land in each thumbnail column
    int32_t cur_row; // thumbnail row being accumulated, -1 = none yet
    uint32_t rows_in; // source rows accumulated into it
} job_t;

static size_t jd_in(JDEC *jd, uint8_t *buf, size_t n) {
    job_t *j = jd->device;
    if (j->yield != NULL && !j->yield(j->yield_ctx)) {
        j->aborted = true;
        return 0;
    }
    size_t left = j->len - j->pos;
    if (n > left) n = left;
    if (buf == NULL) { // skip: nothing is read
        j->pos += (uint32_t)n;
        return n;
    }
    if (n == 0) return 0;
    int32_t got = j->rd(j->rd_ctx, j->base + j->pos, buf, (uint32_t)n);
    if (got < 0) {
        j->read_failed = true;
        return 0;
    }
    j->pos += (uint32_t)got;
    return (size_t)got;
}

static void flush_row(job_t *j) {
    if (j->cur_row < 0 || j->rows_in == 0) return;
    uint16_t *d = j->dst + (uint32_t)j->cur_row * j->tw;
    for (uint32_t i = 0; i < j->tw; i++) {
        uint32_t n = (uint32_t)j->colcnt[i] * j->rows_in;
        uint32_t r = j->acc[i * 3] / n, g = j->acc[i * 3 + 1] / n, b = j->acc[i * 3 + 2] / n;
        d[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

// One finished MCU row: every source row goes into the thumbnail row it belongs to (a plain partition, each source pixel counted once).
static void consume_strip(job_t *j, uint32_t rows) {
    for (uint32_t r = 0; r < rows; r++) {
        uint32_t y = j->strip_top + r;
        int32_t row = (int32_t)(y * j->th / j->sh);
        if (row != j->cur_row) {
            flush_row(j);
            j->cur_row = row;
            j->rows_in = 0;
            memset(j->acc, 0, j->tw * 3u * sizeof j->acc[0]);
        }
        j->rows_in++;
        const uint16_t *s = j->strip + r * j->sw;
        for (uint32_t x = 0; x < j->sw; x++) {
            uint16_t *a = j->acc + (x * j->tw / j->sw) * 3u;
            uint16_t p = s[x];
            a[0] = (uint16_t)(a[0] + (p >> 11));
            a[1] = (uint16_t)(a[1] + ((p >> 5) & 63u));
            a[2] = (uint16_t)(a[2] + (p & 31u));
        }
    }
}

static int jd_out(JDEC *jd, void *bitmap, JRECT *rc) {
    job_t *j = jd->device;
    if (j->yield != NULL && !j->yield(j->yield_ctx)) {
        j->aborted = true;
        return 0;
    }
    uint32_t rx = (uint32_t)rc->right - rc->left + 1u, ry = (uint32_t)rc->bottom - rc->top + 1u;
    if (rc->left == 0) j->strip_top = rc->top;
    if (rc->right >= j->sw || rc->bottom >= j->sh) return 0; // cannot happen with a sane decoder; never write outside the strip
    const uint16_t *src = bitmap;
    for (uint32_t r = 0; r < ry; r++) {
        memcpy(j->strip + r * j->sw + rc->left, src + r * rx, rx * sizeof(uint16_t));
    }
    if ((uint32_t)rc->right + 1u == j->sw) consume_strip(j, ry);
    return 1;
}

art_err_t art_decode(meta_read_fn rd, void *rd_ctx, uint64_t offset, uint32_t length, uint16_t *px, art_info_t *info,
                     art_yield_fn yield, void *yield_ctx) {
    job_t j;
    memset(&j, 0, sizeof j);
    j.rd = rd;
    j.rd_ctx = rd_ctx;
    j.base = offset;
    j.len = length;
    j.yield = yield;
    j.yield_ctx = yield_ctx;
    j.dst = px;
    j.cur_row = -1;
    memset(info, 0, sizeof *info);

    void *pool = ART_MALLOC(ART_POOL_BYTES);
    if (pool == NULL) return ART_E_MEMORY;
    JDEC jd;
    art_err_t err = ART_OK;
    void *extra = NULL;
    JRESULT rc = jd_prepare(&jd, jd_in, pool, ART_POOL_BYTES, &j);
    if (rc != JDR_OK) {
        err = j.aborted ? ART_E_ABORTED : j.read_failed ? ART_E_READ : (rc == JDR_FMT3 || rc == JDR_MEM1 || rc == JDR_MEM2) ? ART_E_UNSUPPORTED : ART_E_FORMAT;
        goto out;
    }
    if (jd.width > ART_MAX_SRC_DIM || jd.height > ART_MAX_SRC_DIM) {
        err = ART_E_TOO_BIG;
        goto out;
    }
    // Largest decoder shrink that still leaves the long side at 64 pixels or more; the rest is done by averaging.
    uint32_t m = jd.width > jd.height ? jd.width : jd.height;
    uint8_t scale = 0;
    while (scale < 3 && (m >> (scale + 1)) >= ART_MAX_DIM) scale++;
    uint32_t mx = (uint32_t)jd.msx * 8u, my = (uint32_t)jd.msy * 8u;
    // The decoder scales each MCU separately and drops partial pixels at the right and bottom edge.
    j.sw = ((jd.width / mx * mx) >> scale) + ((jd.width % mx) >> scale);
    j.sh = ((jd.height / my * my) >> scale) + ((jd.height % my) >> scale);
    if (j.sw == 0 || j.sh == 0) {
        err = ART_E_FORMAT;
        goto out;
    }
    if (j.sw >= j.sh) {
        j.tw = j.sw > ART_MAX_DIM ? ART_MAX_DIM : j.sw;
        j.th = j.sw > ART_MAX_DIM ? j.sh * ART_MAX_DIM / j.sw : j.sh;
    } else {
        j.th = j.sh > ART_MAX_DIM ? ART_MAX_DIM : j.sh;
        j.tw = j.sh > ART_MAX_DIM ? j.sw * ART_MAX_DIM / j.sh : j.sw;
    }
    if (j.tw == 0) j.tw = 1;
    if (j.th == 0) j.th = 1;

    uint32_t strip_rows = my >> scale;
    if (strip_rows == 0) strip_rows = 1;
    size_t strip_bytes = (size_t)j.sw * strip_rows * sizeof(uint16_t);
    size_t acc_bytes = (size_t)ART_MAX_DIM * 3u * sizeof(uint16_t);
    extra = ART_MALLOC(strip_bytes + acc_bytes + ART_MAX_DIM);
    if (extra == NULL) {
        err = ART_E_MEMORY;
        goto out;
    }
    j.strip = extra;
    j.acc = (uint16_t *)((uint8_t *)extra + strip_bytes);
    j.colcnt = (uint8_t *)extra + strip_bytes + acc_bytes;
    memset(j.colcnt, 0, ART_MAX_DIM);
    for (uint32_t x = 0; x < j.sw; x++) {
        uint32_t i = x * j.tw / j.sw;
        if (j.colcnt[i] < 255) j.colcnt[i]++;
    }

    rc = jd_decomp(&jd, jd_out, scale);
    if (rc != JDR_OK) {
        err = j.aborted ? ART_E_ABORTED : j.read_failed ? ART_E_READ : ART_E_FORMAT;
        goto out;
    }
    flush_row(&j);
    if (j.cur_row != (int32_t)j.th - 1) { // the stream ended early without an error: do not show half a picture
        err = ART_E_FORMAT;
        goto out;
    }
    info->src_w = jd.width;
    info->src_h = jd.height;
    info->w = (uint16_t)j.tw;
    info->h = (uint16_t)j.th;
    info->scale = scale;
out:
    free(extra);
    free(pool);
    return err;
}

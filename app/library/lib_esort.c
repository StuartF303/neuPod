// SPDX-License-Identifier: MPL-2.0
// External merge sort of fixed-size records on the card, in a caller-supplied scratch (docs/adr/0017: "Sort"). Pass 1 fills the whole
// free scratch with records, heap-sorts them in place and writes a run. Then runs are merged, as many at a time as there are FIL objects
// (and one head record each) that fit in the scratch; FatFs' own sector buffer in each FIL is the read buffer, so no other buffer is
// needed. No recursion, no heap, no static state.
#include <stdio.h>
#include <string.h>

#include "lib_internal.h"

static void swap_rec(uint8_t *a, uint8_t *b, uint8_t *tmp, size_t rs) {
    memcpy(tmp, a, rs);
    memcpy(a, b, rs);
    memcpy(b, tmp, rs);
}

static void sift(uint8_t *a, size_t rs, size_t n, size_t i, lib_cmp_fn cmp, void *ctx, uint8_t *tmp) {
    for (;;) {
        size_t l = 2 * i + 1;
        if (l >= n) break;
        size_t m = l;
        if (l + 1 < n && cmp(a + (l + 1) * rs, a + l * rs, ctx) > 0) m = l + 1;
        if (cmp(a + m * rs, a + i * rs, ctx) <= 0) break;
        swap_rec(a + i * rs, a + m * rs, tmp, rs);
        i = m;
    }
}

static void heapsort(uint8_t *a, size_t rs, size_t n, lib_cmp_fn cmp, void *ctx, uint8_t *tmp) {
    if (n < 2) return;
    for (size_t i = n / 2; i-- > 0;) sift(a, rs, n, i, cmp, ctx, tmp);
    for (size_t end = n - 1; end > 0; end--) {
        swap_rec(a, a + end * rs, tmp, rs);
        sift(a, rs, end, 0, cmp, ctx, tmp);
    }
}

static void run_path(char *out, size_t cap, const char *dir, uint32_t id) { snprintf(out, cap, "%s/s%u", dir, (unsigned)id); }

static lib_err_t touch_empty(const char *path) {
    FIL f;
    lib_err_t e = lib_fopen(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (e != LIB_OK) return e;
    return f_close(&f) == FR_OK ? LIB_OK : LIB_E_IO;
}

lib_err_t lib_esort(lib_arena_t *ar, const char *work_dir, const char *src, const char *dst, uint32_t rs, lib_cmp_fn cmp, void *ctx,
                    lib_sort_stats_t *st) {
    size_t mark = ar->used;
    lib_err_t res = LIB_OK;
    char path[96], path2[96];
    FIL *fin = lib_arena_alloc(ar, sizeof(FIL)), *fout = lib_arena_alloc(ar, sizeof(FIL));
    uint8_t *tmp = lib_arena_alloc(ar, rs);
    if (fin == NULL || fout == NULL || tmp == NULL) {
        ar->used = mark;
        return LIB_E_SCRATCH;
    }
    f_unlink(dst);

    /* pass 1: runs */
    size_t chunk_recs = (lib_arena_free(ar) & ~(size_t)7u) / rs;
    uint32_t nruns = 0;
    if (chunk_recs < 32) {
        ar->used = mark;
        return LIB_E_SCRATCH;
    }
    size_t mark_buf = ar->used;
    uint8_t *buf = lib_arena_alloc(ar, chunk_recs * rs);
    if (buf == NULL) {
        ar->used = mark;
        return LIB_E_SCRATCH;
    }
    if (f_open(fin, src, FA_READ) != FR_OK) {
        ar->used = mark;
        return LIB_E_IO;
    }
    for (;;) {
        UINT got = 0;
        FRESULT fr = f_read(fin, buf, (UINT)(chunk_recs * rs), &got);
        if (fr != FR_OK) {
            res = LIB_E_IO;
            break;
        }
        size_t n = got / rs;
        if (n == 0) break;
        heapsort(buf, rs, n, cmp, ctx, tmp);
        run_path(path, sizeof path, work_dir, nruns);
        UINT wr = 0;
        fr = f_open(fout, path, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr == FR_OK) fr = f_write(fout, buf, (UINT)(n * rs), &wr);
        if (fr == FR_OK && wr != n * rs) fr = FR_DENIED;
        FRESULT fc = fr == FR_OK ? f_close(fout) : (f_close(fout), fr);
        if (fr != FR_OK || fc != FR_OK) {
            res = lib_map_fr(fr != FR_OK ? fr : fc);
            break;
        }
        nruns++;
        if (got < chunk_recs * rs) break;
    }
    f_close(fin);
    ar->used = mark_buf; // the chunk buffer is free again for the merge
    if (res != LIB_OK) {
        ar->used = mark;
        return res;
    }
    if (st != NULL) st->runs += nruns;
    if (nruns == 0) {
        ar->used = mark;
        return touch_empty(dst);
    }

    /* merge passes: runs are a queue of consecutive ids; the oldest `fan` are merged into a new one at the tail */
    size_t per = sizeof(FIL) + rs + 16;
    size_t fan = lib_arena_free(ar) / per;
    if (fan > 40) fan = 40;
    if (nruns > 1 && fan < 2) {
        ar->used = mark;
        return LIB_E_SCRATCH;
    }
    uint32_t head = 0, tail = nruns;
    while (tail - head > 1 && res == LIB_OK) {
        uint32_t g = tail - head < fan ? tail - head : (uint32_t)fan;
        bool last = tail - head <= fan;
        size_t mark2 = ar->used;
        FIL *in = lib_arena_alloc(ar, g * sizeof(FIL));
        uint8_t *heads = lib_arena_alloc(ar, (size_t)g * rs);
        if (in == NULL || heads == NULL) {
            res = LIB_E_SCRATCH;
            break;
        }
        if (last) snprintf(path2, sizeof path2, "%s", dst);
        else run_path(path2, sizeof path2, work_dir, tail);
        uint64_t alive = 0;
        uint32_t opened = 0;
        for (uint32_t i = 0; i < g && res == LIB_OK; i++) {
            run_path(path, sizeof path, work_dir, head + i);
            if (f_open(&in[i], path, FA_READ) != FR_OK) {
                res = LIB_E_IO;
                break;
            }
            opened++;
            UINT got = 0;
            if (f_read(&in[i], heads + (size_t)i * rs, rs, &got) != FR_OK) res = LIB_E_IO;
            else if (got == rs) alive |= 1ull << i;
        }
        if (res == LIB_OK) res = lib_fopen(fout, path2, FA_CREATE_ALWAYS | FA_WRITE);
        bool out_open = res == LIB_OK;
        while (res == LIB_OK && alive != 0) {
            uint32_t m = 64;
            for (uint32_t i = 0; i < g; i++) {
                if (!(alive & (1ull << i))) continue;
                if (m == 64 || cmp(heads + (size_t)i * rs, heads + (size_t)m * rs, ctx) < 0) m = i;
            }
            UINT wr = 0;
            FRESULT fr = f_write(fout, heads + (size_t)m * rs, rs, &wr);
            if (fr != FR_OK || wr != rs) {
                res = fr != FR_OK ? lib_map_fr(fr) : LIB_E_FULL;
                break;
            }
            UINT got = 0;
            if (f_read(&in[m], heads + (size_t)m * rs, rs, &got) != FR_OK) res = LIB_E_IO;
            else if (got != rs) alive &= ~(1ull << m);
        }
        if (out_open) {
            FRESULT fc = f_close(fout);
            if (res == LIB_OK && fc != FR_OK) res = lib_map_fr(fc);
        }
        for (uint32_t i = 0; i < opened; i++) f_close(&in[i]);
        if (res == LIB_OK) {
            for (uint32_t i = 0; i < g; i++) {
                run_path(path, sizeof path, work_dir, head + i);
                f_unlink(path);
            }
        }
        ar->used = mark2;
        if (st != NULL) st->merges++;
        head += g;
        if (last) break;
        tail++;
    }
    if (res == LIB_OK && nruns == 1) { // a single run is the result
        run_path(path, sizeof path, work_dir, 0);
        if (f_rename(path, dst) != FR_OK) res = LIB_E_IO;
    }
    ar->used = mark;
    return res;
}

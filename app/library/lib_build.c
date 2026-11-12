// SPDX-License-Identifier: MPL-2.0
// Library scanner (docs/adr/0017-music-library-index.md, "Scanner"): walk the music folder, parse each file's tags, intern the
// strings, sort, write the table set into <base>/lib.tmp/, write the manifest last, swap it in.
//
// How strings get ids without a hash table. While walking, the string pool is append-only with a one-entry cache per kind (albums come
// in folder order, so an artist or album repeats back to back: those repeats are not written twice). Entities (artists, albums,
// genres) then get ids by sorting "occurrences" - one per change of value along the walk - on the card, collapsing equal keys, and
// writing the table in sorted order; the ids of the occurrences are sorted back into walk order so a second pass over the walk
// (which makes the same decisions) can read each track's ids sequentially. Peak RAM is the caller's scratch.
#include <stdio.h>
#include <string.h>

#include "hal_storage.h"
#include "lib_internal.h"
#include "lib_store.h"
#include "meta.h"
#include "wav.h"

#define MAX_DEPTH 12u
#define TAG_SCRATCH 4096u /* FLAC comment blocks up to this size are read; larger ones are skipped (their tags are lost) */
#define POOL_STR LIB_TEXT_MAX /* a pool string is at most 64 characters + NUL */
#define PATH_MAX_ 256u
#define PROV_REC 24u /* provisional per-track strings: aa, album, genre, sort_title, sort_aa, sort_album (offsets in strings.str) */
#define ENT_REC 40u  /* group u32, key[24], src_off u32, name_off u32, seq u32 */
#define KEY_REC 32u  /* key[24], src_off u32, id u32 */
#define ALB_REC 44u  /* album u32, disc<<16|track u32, key[24], src_off u32, id u32, genre id u32 (LIB_NONE = none) */
#define GEN_REC 12u  /* genre u32, position in by_album u32, track id u32 */

typedef union {
    flac_info_t flac;
    mp3_info_t mp3;
    mp4_info_t mp4;
    wav_info_t wav;
} parse_t;

typedef struct {
    char s[POOL_STR];
    uint32_t off;
    bool valid;
} slot_t;
enum { PS_ARTIST, PS_AA, PS_ALBUM, PS_GENRE, PS_SORT_AA, PS_SORT_ALBUM, PS_N };

/* read access to strings.str while sorting: two slots, for the two operands of a comparison */
typedef struct {
    FIL *f;
    bool open;
    uint32_t off[2];
    char s[2][POOL_STR];
    bool valid[2];
} pool_t;

typedef struct {
    const lib_build_cfg_t *cfg;
    lib_arena_t *ar;
    lib_sort_stats_t sst;
    char base[48], tmp[64], work[64], root[96];
    char path[PATH_MAX_];
    FILINFO fno;
    FIL *fdirs, *fnames, *ftracks, *fstr, *fprov, *ffile;
    uint8_t *tagbuf;
    audio_tags_ext_t *ext; /* the library tags the parsers fill (meta_set_tags_ext) */
    parse_t *parse;
    uint32_t n_dirs, names_len, str_len, en_len, n_tracks, n_playable, files_done;
    slot_t slot[PS_N];
    uint32_t jnext[LIB_LISTS], jrows[LIB_LISTS];
    lib_manifest_t m;
} bctx_t;

/* ---- small helpers ------------------------------------------------------------------------------------------------- */

static void tpath(const bctx_t *c, char *out, const char *leaf) { snprintf(out, 96, "%s/%s", c->tmp, leaf); }
static void wpath(const bctx_t *c, char *out, const char *leaf) { snprintf(out, 96, "%s/%s", c->work, leaf); }

static void cut(char *dst, size_t cap, const char *s) { // UTF-8 copy cut at a character boundary
    size_t n = strlen(s);
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static bool ieq_str(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
}

static bool has_ext(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    if (n < e) return false;
    return ieq_str(name + n - e, ext);
}

static uint32_t dos_to_unix(WORD d, WORD t) {
    int y = 1980 + (d >> 9), mo = (d >> 5) & 15, da = d & 31;
    if (mo < 1 || mo > 12) return 0;
    // days from civil (Howard Hinnant)
    int yy = mo <= 2 ? y - 1 : y;
    int era = yy / 400, yoe = yy - era * 400;
    int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + da - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + doe - 719468;
    return (uint32_t)(days * 86400L + ((t >> 11) & 31) * 3600L + ((t >> 5) & 63) * 60L + (t & 31) * 2L);
}

static lib_err_t wr(FIL *f, const void *p, uint32_t n) {
    UINT w = 0;
    FRESULT fr = f_write(f, p, n, &w);
    if (fr != FR_OK) return lib_map_fr(fr);
    return w == n ? LIB_OK : LIB_E_FULL;
}

static lib_err_t rd(FIL *f, void *p, uint32_t n, bool *eof) { // exactly n bytes, or *eof at a clean end
    UINT g = 0;
    FRESULT fr = f_read(f, p, n, &g);
    if (fr != FR_OK) return LIB_E_IO;
    if (g == 0 && eof != NULL) {
        *eof = true;
        return LIB_OK;
    }
    return g == n ? LIB_OK : LIB_E_FORMAT;
}

static lib_err_t open_out(const bctx_t *c, FIL *f, const char *leaf) { // header placeholder written; payload follows
    char p[96];
    uint8_t zero[LIB_HDR] = {0};
    tpath(c, p, leaf);
    lib_err_t e = lib_fopen(f, p, FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
    return e != LIB_OK ? e : wr(f, zero, LIB_HDR);
}

static lib_err_t close_f(FIL *f) {
    FRESULT fr = f_sync(f);
    FRESULT fc = f_close(f);
    return fr != FR_OK ? lib_map_fr(fr) : fc != FR_OK ? lib_map_fr(fc) : LIB_OK;
}

/* ---- string pool (strings.str) ------------------------------------------------------------------------------------- */

static lib_err_t pool_add(bctx_t *c, int slot, const char *s, uint32_t *off) {
    if (slot >= 0 && c->slot[slot].valid && strcmp(c->slot[slot].s, s) == 0) {
        *off = c->slot[slot].off;
        return LIB_OK;
    }
    size_t n = strlen(s) + 1;
    lib_err_t e = wr(c->fstr, s, (uint32_t)n);
    if (e != LIB_OK) return e;
    *off = c->str_len;
    c->str_len += (uint32_t)n;
    if (slot >= 0) {
        cut(c->slot[slot].s, POOL_STR, s);
        c->slot[slot].off = *off;
        c->slot[slot].valid = true;
    }
    return LIB_OK;
}

static const char *pool_get(pool_t *p, int which, uint32_t off) {
    if (off == LIB_NONE) return "";
    if (p->valid[which] && p->off[which] == off) return p->s[which];
    UINT got = 0;
    p->valid[which] = false;
    p->s[which][0] = '\0';
    if (f_lseek(p->f, LIB_HDR + off) != FR_OK || f_read(p->f, p->s[which], POOL_STR - 1, &got) != FR_OK) return "";
    p->s[which][got] = '\0'; // strings are NUL-terminated in the pool; a short read at its end is cut here
    p->off[which] = off;
    p->valid[which] = true;
    return p->s[which];
}

static void key24(uint8_t *key, const char *name, const char *sort) {
    memset(key, 0, LIB_KEY_PREFIX);
    uint8_t k[LIB_KEY_PREFIX];
    size_t n = lib_collate_key(name, sort, k, sizeof k);
    memcpy(key, k, n < LIB_KEY_PREFIX ? n : LIB_KEY_PREFIX);
}

// Keys equal over their 24-byte prefix: when the prefix is full the names may differ beyond it, so compare the full keys from the pool.
static int key_tie(const uint8_t *ka, uint32_t src_a, uint32_t src_b, pool_t *p) {
    if (ka[LIB_KEY_PREFIX - 1] == 0 || src_a == src_b) return 0;
    uint8_t fa[LIB_KEY_MAX], fb[LIB_KEY_MAX];
    size_t na = lib_collate_key(pool_get(p, 0, src_a), NULL, fa, sizeof fa), nb = lib_collate_key(pool_get(p, 1, src_b), NULL, fb, sizeof fb);
    size_t n = na < nb ? na : nb;
    int r = memcmp(fa, fb, n);
    if (r != 0) return r < 0 ? -1 : 1;
    return na == nb ? 0 : (na < nb ? -1 : 1);
}

static int cmp_u32(uint32_t a, uint32_t b) { return a < b ? -1 : a > b ? 1 : 0; }

/* entity occurrences: group, key, ... seq */
static int ent_cmp_key(const uint8_t *a, const uint8_t *b, pool_t *p) {
    int r = cmp_u32(ld32(a), ld32(b));
    if (r != 0) return r;
    r = memcmp(a + 4, b + 4, LIB_KEY_PREFIX);
    if (r != 0) return r < 0 ? -1 : 1;
    return key_tie(a + 4, ld32(a + 28), ld32(b + 28), p);
}
static int ent_cmp(const void *a, const void *b, void *ctx) {
    int r = ent_cmp_key(a, b, ctx);
    return r != 0 ? r : cmp_u32(ld32((const uint8_t *)a + 36), ld32((const uint8_t *)b + 36));
}
static int key_cmp(const void *a, const void *b, void *ctx) { // KEY_REC: key, src, id
    const uint8_t *x = a, *y = b;
    int r = memcmp(x, y, LIB_KEY_PREFIX);
    if (r != 0) return r < 0 ? -1 : 1;
    r = key_tie(x, ld32(x + 24), ld32(y + 24), ctx);
    return r != 0 ? r : cmp_u32(ld32(x + 28), ld32(y + 28));
}
static int alb_cmp(const void *a, const void *b, void *ctx) { // ALB_REC: album, disc/track, key, src, id
    const uint8_t *x = a, *y = b;
    int r = cmp_u32(ld32(x), ld32(y));
    if (r == 0) r = cmp_u32(ld32(x + 4), ld32(y + 4));
    if (r == 0) {
        r = memcmp(x + 8, y + 8, LIB_KEY_PREFIX);
        r = r < 0 ? -1 : r > 0 ? 1 : 0;
    }
    if (r == 0) r = key_tie(x + 8, ld32(x + 32), ld32(y + 32), ctx);
    return r != 0 ? r : cmp_u32(ld32(x + 36), ld32(y + 36));
}
static int pair_cmp(const void *a, const void *b, void *ctx) {
    (void)ctx;
    int r = cmp_u32(ld32(a), ld32(b));
    return r != 0 ? r : cmp_u32(ld32((const uint8_t *)a + 4), ld32((const uint8_t *)b + 4));
}
static int gen_cmp(const void *a, const void *b, void *ctx) { // GEN_REC: genre, album-order position (unique)
    (void)ctx;
    int r = cmp_u32(ld32(a), ld32(b));
    return r != 0 ? r : cmp_u32(ld32((const uint8_t *)a + 4), ld32((const uint8_t *)b + 4));
}
static int map_cmp(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return cmp_u32(ld32(a), ld32(b));
}

/* ---- letter jumps -------------------------------------------------------------------------------------------------- */

static void jump_feed(bctx_t *c, lib_list_t l, uint32_t row, unsigned bucket) {
    while (c->jnext[l] <= bucket && c->jnext[l] < LIB_BUCKETS) c->m.jump[l][c->jnext[l]++] = row;
    c->jrows[l] = row + 1;
}
static void jump_done(bctx_t *c, lib_list_t l) {
    while (c->jnext[l] < LIB_BUCKETS) c->m.jump[l][c->jnext[l]++] = c->jrows[l];
}

/* ---- pass 1: walk -------------------------------------------------------------------------------------------------- */

static int32_t file_rd(void *ctx, uint64_t off, void *dst, uint32_t n) {
    FIL *f = ctx;
    UINT got = 0;
    if (f->fptr != off && f_lseek(f, (FSIZE_t)off) != FR_OK) return -1;
    if (f_read(f, dst, n, &got) != FR_OK) return -1;
    return (int32_t)got;
}

static uint8_t play_from_meta(meta_err_t e) {
    switch (e) {
    case META_OK: return LIB_PLAY_OK;
    case META_E_CHANNELS: return LIB_PLAY_CHANNELS;
    case META_E_RATE: return LIB_PLAY_RATE;
    case META_E_BITS:
    case META_E_ALAC32: return LIB_PLAY_BITS;
    case META_E_BLOCKSIZE: return LIB_PLAY_BLOCK;
    case META_E_AAC:
    case META_E_CODEC: return LIB_PLAY_CODEC;
    default: return LIB_PLAY_DAMAGED; // META_E_READ, META_E_FORMAT, META_E_NO_AUDIO
    }
}

static uint8_t play_from_wav(wav_err_t e) {
    switch (e) {
    case WAV_OK: return LIB_PLAY_OK;
    case WAV_E_CHANNELS: return LIB_PLAY_CHANNELS;
    case WAV_E_BITS: return LIB_PLAY_BITS;
    case WAV_E_RATE: return LIB_PLAY_RATE;
    case WAV_E_NOT_PCM: return LIB_PLAY_CODEC;
    default: return LIB_PLAY_DAMAGED;
    }
}

static lib_err_t names_add(bctx_t *c, const char *s, uint32_t *off) {
    size_t n = strlen(s) + 1;
    lib_err_t e = wr(c->fnames, s, (uint32_t)n);
    *off = c->names_len;
    c->names_len += (uint32_t)n;
    return e;
}

static lib_err_t dir_add(bctx_t *c, uint32_t parent, const char *name, uint32_t *id) {
    uint32_t off;
    lib_err_t e = names_add(c, name, &off);
    if (e != LIB_OK) return e;
    uint8_t rec[LIB_REC_DIR];
    st32(rec, parent);
    st32(rec + 4, off);
    e = wr(c->fdirs, rec, sizeof rec);
    *id = c->n_dirs++;
    return e;
}

// One audio file at c->path (name in c->fno): parse it, append its strings, its provisional record and its track record.
static lib_err_t index_file(bctx_t *c, uint32_t dir_id, const char *leaf) {
    const char *name = c->fno.fname;
    parse_t *pp = c->parse;
    const audio_tags_t *t = NULL;
    uint8_t codec = LIB_CODEC_NONE, bits = 0, play = LIB_PLAY_DAMAGED;
    uint32_t rate = 0;
    uint64_t frames = 0;
    static const audio_tags_t no_tags;
    memset(pp, 0, sizeof *pp);
    memset(c->ext, 0, sizeof *c->ext);
    const audio_tags_ext_t *x = c->ext;
    FIL *f = c->ffile;
    if (f_open(f, c->path, FA_READ) == FR_OK) {
        uint64_t size = f_size(f);
        if (has_ext(name, ".wav")) {
            wav_err_t e = wav_parse(file_rd, f, size, &pp->wav);
            codec = LIB_CODEC_WAV;
            play = play_from_wav(e);
            rate = pp->wav.rate;
            bits = pp->wav.bits;
            frames = pp->wav.frames;
        } else if (has_ext(name, ".flac")) {
            meta_err_t e = flac_parse(file_rd, f, size, &pp->flac);
            codec = LIB_CODEC_FLAC;
            play = play_from_meta(e);
            t = &pp->flac.tags;
            rate = pp->flac.rate;
            bits = pp->flac.bits;
            frames = pp->flac.total_frames;
        } else if (has_ext(name, ".mp3")) {
            meta_err_t e = mp3_parse(file_rd, f, size, &pp->mp3);
            codec = LIB_CODEC_MP3;
            play = play_from_meta(e);
            t = &pp->mp3.tags;
            rate = pp->mp3.rate;
            bits = 16;
            frames = pp->mp3.total_frames;
        } else { // .m4a / .mp4
            meta_err_t e = mp4_parse(file_rd, f, size, &pp->mp4);
            codec = e == META_E_AAC ? LIB_CODEC_AAC : pp->mp4.is_alac ? LIB_CODEC_ALAC : LIB_CODEC_NONE;
            play = play_from_meta(e);
            t = &pp->mp4.tags;
            rate = pp->mp4.rate;
            bits = pp->mp4.bits;
            frames = pp->mp4.total_frames;
        }
        f_close(f);
    }
    if (t == NULL) t = &no_tags;

    char title[POOL_STR], artist[POOL_STR], aa[POOL_STR], album[POOL_STR], genre[POOL_STR];
    cut(title, sizeof title, t->title[0] ? t->title : name);
    cut(artist, sizeof artist, t->artist[0] ? t->artist : "Unknown Artist");
    cut(aa, sizeof aa, x->album_artist[0] ? x->album_artist : artist);
    cut(album, sizeof album, t->album[0] ? t->album : leaf);
    cut(genre, sizeof genre, x->genre);
    const char *sort_aa = x->sort_album_artist[0] ? x->sort_album_artist : (!x->album_artist[0] ? x->sort_artist : "");
    bool comp = x->compilation || ieq_str(aa, "Various Artists"); // D4: compilations (flag or "Various Artists") are their own list

    uint32_t o_title, o_artist, o_aa, o_album, o_genre = LIB_NONE, o_st = LIB_NONE, o_saa = LIB_NONE, o_sal = LIB_NONE;
    lib_err_t e = pool_add(c, -1, title, &o_title); // titles are not repeated: no cache
    if (e == LIB_OK) e = pool_add(c, PS_ARTIST, artist, &o_artist);
    if (e == LIB_OK) e = pool_add(c, PS_AA, aa, &o_aa);
    if (e == LIB_OK) e = pool_add(c, PS_ALBUM, album, &o_album);
    if (e == LIB_OK && genre[0]) e = pool_add(c, PS_GENRE, genre, &o_genre);
    if (e == LIB_OK && x->sort_title[0]) e = pool_add(c, -1, x->sort_title, &o_st);
    if (e == LIB_OK && sort_aa[0]) e = pool_add(c, PS_SORT_AA, sort_aa, &o_saa);
    if (e == LIB_OK && x->sort_album[0]) e = pool_add(c, PS_SORT_ALBUM, x->sort_album, &o_sal);
    uint32_t o_name = 0;
    if (e == LIB_OK) e = names_add(c, name, &o_name);
    if (e != LIB_OK) return e;

    uint8_t pv[PROV_REC], tr[LIB_REC_TRACK];
    st32(pv, o_aa);
    st32(pv + 4, o_album);
    st32(pv + 8, o_genre);
    st32(pv + 12, o_st);
    st32(pv + 16, o_saa);
    st32(pv + 20, o_sal);
    memset(tr, 0, sizeof tr);
    st32(tr + TR_NAME, o_name);
    st32(tr + TR_TITLE, o_title);
    st32(tr + TR_ARTIST, o_artist);
    st32(tr + TR_DIR, dir_id);
    st32(tr + TR_AARTIST, LIB_NONE);
    st32(tr + TR_ALBUM, LIB_NONE);
    st32(tr + TR_SIZE, c->fno.fsize > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)c->fno.fsize);
    st32(tr + TR_MTIME, dos_to_unix(c->fno.fdate, c->fno.ftime));
    st32(tr + TR_DUR, rate != 0 ? (uint32_t)(frames / rate) : 0);
    st16(tr + TR_GENRE, LIB_GENRE_NONE);
    st16(tr + TR_YEAR, x->year);
    st16(tr + TR_TRACK, t->track);
    tr[TR_DISC] = x->disc > 255 ? 255 : (uint8_t)x->disc;
    tr[TR_FLAGS] = comp ? 1 : 0;
    tr[TR_CODEC] = codec;
    tr[TR_BITS] = bits;
    unsigned ri = 0;
    for (unsigned i = 1; i < 12; i++) {
        if (lib_rates[i] == rate) ri = i;
    }
    tr[TR_RATE] = (uint8_t)ri;
    tr[TR_PLAY] = play;
    e = wr(c->fprov, pv, sizeof pv);
    if (e == LIB_OK) e = wr(c->ftracks, tr, sizeof tr);
    c->n_tracks++;
    if (play == LIB_PLAY_OK) c->n_playable++;
    return e;
}

static lib_err_t walk_dir(bctx_t *c, uint32_t dir_id, unsigned depth) {
    DIR d;
    if (f_opendir(&d, c->path) != FR_OK) return LIB_OK; // an unreadable folder is skipped, like a missing one
    size_t dirlen = strlen(c->path);
    lib_err_t res = LIB_OK;
    for (;;) {
        FRESULT fr = f_readdir(&d, &c->fno);
        if (fr != FR_OK) {
            res = LIB_E_IO;
            break;
        }
        const char *name = c->fno.fname;
        if (name[0] == '\0') break;
        if ((c->fno.fattrib & (AM_HID | AM_SYS)) || name[0] == '.') continue; // System Volume Information, ._AppleDouble, .Trashes ...
        size_t nl = strlen(name);
        if (dirlen + 1 + nl >= sizeof c->path) continue;
        if (c->fno.fattrib & AM_DIR) {
            if (depth >= MAX_DEPTH) continue;
            uint32_t id;
            res = dir_add(c, dir_id, name, &id);
            if (res != LIB_OK) break;
            c->path[dirlen] = '/';
            memcpy(c->path + dirlen + 1, name, nl + 1);
            res = walk_dir(c, id, depth + 1);
            c->path[dirlen] = '\0';
            if (res != LIB_OK) break;
        } else if (has_ext(name, ".wav") || has_ext(name, ".flac") || has_ext(name, ".mp3") || has_ext(name, ".m4a") || has_ext(name, ".mp4")) {
            const char *leaf = strrchr(c->path, '/');
            leaf = leaf != NULL ? leaf + 1 : c->path;
            char leafbuf[POOL_STR];
            cut(leafbuf, sizeof leafbuf, leaf); // (the file name is appended to the path next: keep the folder's own name)
            c->path[dirlen] = '/';
            memcpy(c->path + dirlen + 1, name, nl + 1);
            res = index_file(c, dir_id, leafbuf);
            c->path[dirlen] = '\0';
            c->files_done++;
            if (res != LIB_OK) break;
            if (c->cfg->progress != NULL && !c->cfg->progress(c->cfg->user, c->files_done)) {
                res = LIB_E_CANCELLED;
                break;
            }
        }
    }
    f_closedir(&d);
    return res;
}

static lib_err_t stage_walk(bctx_t *c) {
    size_t mark = c->ar->used;
    lib_err_t e = LIB_OK;
    c->fdirs = lib_arena_alloc(c->ar, sizeof(FIL));
    c->fnames = lib_arena_alloc(c->ar, sizeof(FIL));
    c->ftracks = lib_arena_alloc(c->ar, sizeof(FIL));
    c->fstr = lib_arena_alloc(c->ar, sizeof(FIL));
    c->fprov = lib_arena_alloc(c->ar, sizeof(FIL));
    c->ffile = lib_arena_alloc(c->ar, sizeof(FIL));
    c->tagbuf = lib_arena_alloc(c->ar, TAG_SCRATCH);
    c->ext = lib_arena_alloc(c->ar, sizeof *c->ext);
    c->parse = lib_arena_alloc(c->ar, sizeof(parse_t));
    if (c->parse == NULL || c->ext == NULL) {
        c->ar->used = mark;
        return LIB_E_SCRATCH;
    }
    char p[96];
    e = open_out(c, c->fdirs, lib_files[LF_DIRS].name);
    if (e == LIB_OK) e = open_out(c, c->fnames, lib_files[LF_NAMES].name);
    if (e == LIB_OK) e = open_out(c, c->fstr, lib_files[LF_STRINGS].name);
    if (e == LIB_OK) {
        wpath(c, p, "tracks.pre"); // work files have no header
        e = lib_fopen(c->ftracks, p, FA_CREATE_ALWAYS | FA_WRITE);
    }
    if (e == LIB_OK) {
        wpath(c, p, "prov");
        e = lib_fopen(c->fprov, p, FA_CREATE_ALWAYS | FA_WRITE);
    }
    if (e == LIB_OK) {
        meta_set_scratch(c->tagbuf, TAG_SCRATCH);
        meta_set_tags_ext(c->ext);
        uint32_t root_id;
        FILINFO fi;
        snprintf(c->path, sizeof c->path, "%s", c->root);
        e = dir_add(c, LIB_NONE, c->root, &root_id);
        if (e == LIB_OK && f_stat(c->path, &fi) == FR_OK && (fi.fattrib & AM_DIR)) e = walk_dir(c, root_id, 0); // no folder = an empty library
        meta_set_scratch(NULL, 0);
        meta_set_tags_ext(NULL);
    }
    lib_err_t e1 = close_f(c->fdirs), e2 = close_f(c->fnames), e3 = close_f(c->fstr), e4 = close_f(c->ftracks), e5 = close_f(c->fprov);
    if (e == LIB_OK) e = e1 != LIB_OK ? e1 : e2 != LIB_OK ? e2 : e3 != LIB_OK ? e3 : e4 != LIB_OK ? e4 : e5;
    c->ar->used = mark;
    c->fdirs = c->fnames = c->ftracks = c->fstr = c->fprov = c->ffile = NULL;
    return e;
}

/* ---- passes over the walk: entities -------------------------------------------------------------------------------- */

typedef struct {
    FIL *map; /* sorted (seq, id) records for replay; NULL while generating */
    bool have;
    uint32_t k[3];
    uint32_t n; /* occurrences so far */
    uint32_t id;
} occ_t;

// True when (a, b, c) starts a new occurrence (it differs from the last one seen). With a map, `id` is then the occurrence's entity id.
static lib_err_t occ_step(occ_t *o, uint32_t a, uint32_t b, uint32_t c, bool *is_new) {
    *is_new = !(o->have && o->k[0] == a && o->k[1] == b && o->k[2] == c);
    if (!*is_new) return LIB_OK;
    o->have = true;
    o->k[0] = a;
    o->k[1] = b;
    o->k[2] = c;
    if (o->map != NULL) {
        uint8_t rec[8];
        lib_err_t e = rd(o->map, rec, 8, NULL);
        if (e != LIB_OK || ld32(rec) != o->n) return LIB_E_FORMAT;
        o->id = ld32(rec + 4);
    }
    o->n++;
    return LIB_OK;
}

typedef enum { K_ARTIST, K_ALBUM, K_GENRE } ekind_t;

// The two files every entity pass reads in step: tracks.pre (48 B) and prov (24 B).
typedef struct {
    FIL *pre, *prov;
    uint8_t tr[LIB_REC_TRACK], pv[PROV_REC];
} walk_in_t;

static lib_err_t walk_in_open(bctx_t *c, walk_in_t *w) {
    char p[96];
    w->pre = lib_arena_alloc(c->ar, sizeof(FIL));
    w->prov = lib_arena_alloc(c->ar, sizeof(FIL));
    if (w->prov == NULL) return LIB_E_SCRATCH;
    wpath(c, p, "tracks.pre");
    if (f_open(w->pre, p, FA_READ) != FR_OK) return LIB_E_IO;
    wpath(c, p, "prov");
    if (f_open(w->prov, p, FA_READ) != FR_OK) {
        f_close(w->pre);
        return LIB_E_IO;
    }
    return LIB_OK;
}
static void walk_in_close(walk_in_t *w) {
    f_close(w->pre);
    f_close(w->prov);
}
static lib_err_t walk_in_next(walk_in_t *w, bool *eof) {
    lib_err_t e = rd(w->pre, w->tr, LIB_REC_TRACK, eof);
    if (e != LIB_OK || *eof) return e;
    return rd(w->prov, w->pv, PROV_REC, NULL);
}

static lib_err_t open_replay(bctx_t *c, occ_t *o, const char *leaf) {
    char p[96];
    memset(o, 0, sizeof *o);
    o->map = lib_arena_alloc(c->ar, sizeof(FIL));
    if (o->map == NULL) return LIB_E_SCRATCH;
    wpath(c, p, leaf);
    return f_open(o->map, p, FA_READ) == FR_OK ? LIB_OK : LIB_E_IO;
}

static lib_err_t pool_open(bctx_t *c, pool_t *p) {
    char path[96];
    memset(p, 0, sizeof *p);
    p->f = lib_arena_alloc(c->ar, sizeof(FIL));
    if (p->f == NULL) return LIB_E_SCRATCH;
    tpath(c, path, lib_files[LF_STRINGS].name);
    lib_err_t e = lib_fopen(p->f, path, FA_READ);
    p->open = e == LIB_OK;
    return e;
}

static void pool_close(pool_t *p) {
    if (p != NULL && p->open) f_close(p->f);
    if (p != NULL) p->open = false;
}

static void ent_fill(uint8_t *rec, uint32_t group, uint32_t name_off, uint32_t sort_off, pool_t *p, uint32_t seq) {
    uint32_t src = sort_off != LIB_NONE ? sort_off : name_off;
    st32(rec, group);
    key24(rec + 4, pool_get(p, 0, src), NULL);
    st32(rec + 28, src);
    st32(rec + 32, name_off);
    st32(rec + 36, seq);
}

// One entity kind: occurrences (one per change of value along the walk) -> sorted -> table + (seq, id) map -> map sorted by seq.
static lib_err_t stage_entity(bctx_t *c, ekind_t kind) {
    static const char *const names[] = {"artist", "album", "genre"};
    static const unsigned files[] = {LF_ARTISTS, LF_ALBUMS, LF_GENRES};
    size_t mark = c->ar->used;
    char occ_p[96], sorted_p[96], map_p[96], mapsorted_p[96], tmpn[24];
    snprintf(tmpn, sizeof tmpn, "%s.occ", names[kind]);
    wpath(c, occ_p, tmpn);
    snprintf(tmpn, sizeof tmpn, "%s.sorted", names[kind]);
    wpath(c, sorted_p, tmpn);
    snprintf(tmpn, sizeof tmpn, "%s.map", names[kind]);
    wpath(c, map_p, tmpn);
    snprintf(tmpn, sizeof tmpn, "%s.map.sorted", names[kind]);
    wpath(c, mapsorted_p, tmpn);

    lib_err_t e = LIB_OK;
    pool_t *pool = lib_arena_alloc(c->ar, sizeof(pool_t));
    FIL *fo = lib_arena_alloc(c->ar, sizeof(FIL));
    walk_in_t *w = lib_arena_alloc(c->ar, sizeof(walk_in_t));
    occ_t *self = lib_arena_alloc(c->ar, sizeof(occ_t)), *art = lib_arena_alloc(c->ar, sizeof(occ_t));
    if (pool == NULL || fo == NULL || w == NULL || self == NULL || art == NULL) {
        pool_close(pool);
        c->ar->used = mark;
        return LIB_E_SCRATCH;
    }
    pool->open = false; // (pool_close() before pool_open() must be harmless)
    memset(self, 0, sizeof *self);
    e = pool_open(c, pool);
    if (e == LIB_OK) e = walk_in_open(c, w);
    if (e != LIB_OK) {
        pool_close(pool);
        c->ar->used = mark;
        return e;
    }
    bool art_open = false;
    if (kind == K_ALBUM) {
        e = open_replay(c, art, "artist.map.sorted");
        art_open = e == LIB_OK;
    }
    if (e == LIB_OK) e = lib_fopen(fo, occ_p, FA_CREATE_ALWAYS | FA_WRITE);
    bool fo_open = e == LIB_OK;
    while (e == LIB_OK) {
        bool eof = false;
        e = walk_in_next(w, &eof);
        if (e != LIB_OK || eof) break;
        bool comp = w->tr[TR_FLAGS] & 1;
        uint32_t aa = ld32(w->pv), album = ld32(w->pv + 4), genre = ld32(w->pv + 8), saa = ld32(w->pv + 16), sal = ld32(w->pv + 20);
        uint32_t group = 0, name = 0, sort = LIB_NONE, a = 0, b = 0, cc = 0;
        bool take = true;
        if (kind == K_ARTIST) {
            take = !comp;
            name = aa;
            sort = saa;
            a = aa;
            b = saa;
        } else if (kind == K_GENRE) {
            take = genre != LIB_NONE;
            name = genre;
            a = genre;
        } else {
            group = LIB_NONE;
            if (!comp) { // the album artist's id, from the artist pass
                bool nw;
                e = occ_step(art, aa, saa, 0, &nw);
                if (e != LIB_OK) break;
                group = art->id;
            }
            name = album;
            sort = sal;
            a = group;
            b = album;
            cc = sal;
        }
        if (!take) continue;
        bool nw;
        e = occ_step(self, a, b, cc, &nw);
        if (e != LIB_OK || !nw) continue;
        uint8_t rec[ENT_REC];
        ent_fill(rec, group, name, sort, pool, self->n - 1);
        e = wr(fo, rec, sizeof rec);
    }
    if (fo_open) {
        lib_err_t ec = close_f(fo);
        if (e == LIB_OK) e = ec;
    }
    if (art_open) f_close(art->map);
    walk_in_close(w);
    if (e != LIB_OK) {
        pool_close(pool);
        c->ar->used = mark;
        return e;
    }

    // sort the occurrences, then collapse equal keys into table rows
    e = lib_esort(c->ar, c->work, occ_p, sorted_p, ENT_REC, ent_cmp, pool, &c->sst);
    (void)lib_unlink(occ_p);
    FIL *fin = lib_arena_alloc(c->ar, sizeof(FIL)), *fmap = lib_arena_alloc(c->ar, sizeof(FIL)), *ftab = lib_arena_alloc(c->ar, sizeof(FIL));
    FIL *fkey = lib_arena_alloc(c->ar, sizeof(FIL));
    if (fkey == NULL) e = LIB_E_SCRATCH;
    if (e == LIB_OK) e = open_out(c, ftab, lib_files[files[kind]].name);
    bool ftab_open = e == LIB_OK;
    if (e == LIB_OK) e = lib_fopen(fin, sorted_p, FA_READ);
    bool fin_open = e == LIB_OK;
    if (e == LIB_OK) e = lib_fopen(fmap, map_p, FA_CREATE_ALWAYS | FA_WRITE);
    bool fmap_open = e == LIB_OK;
    FIL *fen = lib_arena_alloc(c->ar, sizeof(FIL)); // enames.str: the names of each kind in sorted order (artists, then albums, then genres)
    bool fen_open = false;
    if (fen == NULL && e == LIB_OK) e = LIB_E_SCRATCH;
    if (e == LIB_OK) {
        char pn[96];
        tpath(c, pn, lib_files[LF_ENAMES].name);
        if (kind == K_ARTIST) e = open_out(c, fen, lib_files[LF_ENAMES].name);
        else e = lib_fopen(fen, pn, FA_WRITE | FA_OPEN_APPEND);
        fen_open = e == LIB_OK;
    }
    char key_p[96];
    wpath(c, key_p, "albname.rec");
    bool fkey_open = false;
    if (e == LIB_OK && kind == K_ALBUM) {
        e = lib_fopen(fkey, key_p, FA_CREATE_ALWAYS | FA_WRITE);
        fkey_open = e == LIB_OK;
    }
    uint8_t prev[ENT_REC], cur[ENT_REC];
    uint32_t id = 0, comp_first = LIB_NONE;
    bool have_prev = false;
    while (e == LIB_OK) {
        bool eof = false;
        e = rd(fin, cur, ENT_REC, &eof);
        if (e != LIB_OK || eof) break;
        if (!have_prev || ent_cmp_key(prev, cur, pool) != 0) {
            uint8_t row[LIB_REC_ALBUM] = {0};
            uint32_t rs = kind == K_ARTIST ? LIB_REC_ARTIST : LIB_REC_ALBUM; /* albums and genres are both 20 B */
            const char *nm = pool_get(pool, 0, ld32(cur + 32));
            st32(row, c->en_len);
            e = wr(fen, nm, (uint32_t)strlen(nm) + 1);
            if (e != LIB_OK) break;
            c->en_len += (uint32_t)strlen(nm) + 1;
            memcpy(row + 4, cur + 4, 8);
            if (kind == K_ALBUM) st32(row + 16, ld32(cur));
            e = wr(ftab, row, rs);
            if (e != LIB_OK) break;
            if (kind == K_ARTIST) jump_feed(c, LIB_LIST_ARTISTS, id, lib_collate_bucket(cur + 4));
            if (kind == K_GENRE) jump_feed(c, LIB_LIST_GENRES, id, lib_collate_bucket(cur + 4));
            if (kind == K_ALBUM) {
                if (ld32(cur) == LIB_NONE) {
                    if (comp_first == LIB_NONE) comp_first = id;
                    jump_feed(c, LIB_LIST_COMPILATIONS, id - comp_first, lib_collate_bucket(cur + 4));
                }
                uint8_t kr[KEY_REC];
                memcpy(kr, cur + 4, LIB_KEY_PREFIX);
                st32(kr + 24, ld32(cur + 28));
                st32(kr + 28, id);
                e = wr(fkey, kr, sizeof kr);
                if (e != LIB_OK) break;
            }
            memcpy(prev, cur, ENT_REC);
            have_prev = true;
            id++;
        }
        uint8_t mr[8];
        st32(mr, ld32(cur + 36));
        st32(mr + 4, id - 1);
        e = wr(fmap, mr, sizeof mr);
    }
    if (e == LIB_OK) { // sentinel row: its first-child field is patched when the children are known
        uint8_t row[LIB_REC_ALBUM] = {0};
        e = wr(ftab, row, kind == K_ARTIST ? LIB_REC_ARTIST : LIB_REC_ALBUM);
    }
    if (fkey_open) {
        lib_err_t ec = close_f(fkey);
        if (e == LIB_OK) e = ec;
    }
    if (fen_open) {
        lib_err_t ec = close_f(fen);
        if (e == LIB_OK) e = ec;
    }
    if (fmap_open) {
        lib_err_t ec = close_f(fmap);
        if (e == LIB_OK) e = ec;
    }
    if (fin_open) f_close(fin);
    if (ftab_open) {
        lib_err_t ec = close_f(ftab);
        if (e == LIB_OK) e = ec;
    }
    (void)lib_unlink(sorted_p);
    if (e == LIB_OK) {
        if (kind == K_ARTIST) c->m.artists = id;
        if (kind == K_GENRE) c->m.genres = id;
        if (kind == K_ALBUM) {
            c->m.albums = id;
            c->m.comp_first = comp_first == LIB_NONE ? id : comp_first;
            jump_done(c, LIB_LIST_COMPILATIONS);
        }
        if (kind == K_ARTIST) jump_done(c, LIB_LIST_ARTISTS);
        if (kind == K_GENRE) jump_done(c, LIB_LIST_GENRES);
        e = lib_esort(c->ar, c->work, map_p, mapsorted_p, 8, map_cmp, NULL, &c->sst);
        (void)lib_unlink(map_p);
    }
    pool_close(pool);
    c->ar->used = mark;
    return e;
}

/* ---- tracks (ids filled in), and the order files' sort records ------------------------------------------------------ */

static lib_err_t stage_tracks(bctx_t *c) {
    size_t mark = c->ar->used;
    char p_title[96], p_alb[96], p_pair[96];
    wpath(c, p_title, "title.rec");
    wpath(c, p_alb, "album.rec");
    wpath(c, p_pair, "pair.rec");
    pool_t *pool = lib_arena_alloc(c->ar, sizeof(pool_t));
    walk_in_t *w = lib_arena_alloc(c->ar, sizeof(walk_in_t));
    occ_t *oa = lib_arena_alloc(c->ar, sizeof(occ_t)), *ob = lib_arena_alloc(c->ar, sizeof(occ_t)), *og = lib_arena_alloc(c->ar, sizeof(occ_t));
    FIL *ft = lib_arena_alloc(c->ar, sizeof(FIL)), *fti = lib_arena_alloc(c->ar, sizeof(FIL)), *fal = lib_arena_alloc(c->ar, sizeof(FIL)),
        *fpa = lib_arena_alloc(c->ar, sizeof(FIL));
    if (pool == NULL || w == NULL || oa == NULL || ob == NULL || og == NULL || ft == NULL || fti == NULL || fal == NULL || fpa == NULL) {
        pool_close(pool);
        c->ar->used = mark;
        return LIB_E_SCRATCH;
    }
    pool->open = false;
    lib_err_t e = pool_open(c, pool);
    if (e == LIB_OK) e = walk_in_open(c, w);
    if (e != LIB_OK) {
        pool_close(pool);
        c->ar->used = mark;
        return e;
    }
    bool oa_o = false, ob_o = false, og_o = false;
    e = open_replay(c, oa, "artist.map.sorted");
    oa_o = e == LIB_OK;
    if (e == LIB_OK) e = open_replay(c, ob, "album.map.sorted");
    ob_o = e == LIB_OK;
    if (e == LIB_OK) e = open_replay(c, og, "genre.map.sorted");
    og_o = e == LIB_OK;
    if (e == LIB_OK) e = open_out(c, ft, lib_files[LF_TRACKS].name);
    bool ft_o = e == LIB_OK;
    bool fti_o = false, fal_o = false, fpa_o = false;
    if (e == LIB_OK) e = lib_fopen(fti, p_title, FA_CREATE_ALWAYS | FA_WRITE);
    fti_o = e == LIB_OK;
    if (e == LIB_OK) e = lib_fopen(fal, p_alb, FA_CREATE_ALWAYS | FA_WRITE);
    fal_o = e == LIB_OK;
    if (e == LIB_OK) e = lib_fopen(fpa, p_pair, FA_CREATE_ALWAYS | FA_WRITE);
    fpa_o = e == LIB_OK;
    uint32_t id = 0, last_g = LIB_NONE, last_a = LIB_NONE;
    while (e == LIB_OK) {
        bool eof = false;
        e = walk_in_next(w, &eof);
        if (e != LIB_OK || eof) break;
        bool comp = w->tr[TR_FLAGS] & 1, nw;
        uint32_t aa = ld32(w->pv), album = ld32(w->pv + 4), genre = ld32(w->pv + 8), st = ld32(w->pv + 12), saa = ld32(w->pv + 16),
                 sal = ld32(w->pv + 20);
        uint32_t aid = LIB_NONE, alb, gid = LIB_NONE;
        if (!comp) {
            e = occ_step(oa, aa, saa, 0, &nw);
            if (e != LIB_OK) break;
            aid = oa->id;
        }
        e = occ_step(ob, aid, album, sal, &nw);
        if (e != LIB_OK) break;
        alb = ob->id;
        if (genre != LIB_NONE) {
            e = occ_step(og, genre, 0, 0, &nw);
            if (e != LIB_OK) break;
            gid = og->id;
        }
        st32(w->tr + TR_AARTIST, aid);
        st32(w->tr + TR_ALBUM, alb);
        st16(w->tr + TR_GENRE, gid == LIB_NONE || gid >= LIB_GENRE_NONE ? LIB_GENRE_NONE : gid);
        e = wr(ft, w->tr, LIB_REC_TRACK);
        if (e != LIB_OK) break;
        if (gid != LIB_NONE && aid != LIB_NONE && (gid != last_g || aid != last_a)) {
            uint8_t pr[8];
            st32(pr, gid);
            st32(pr + 4, aid);
            last_g = gid;
            last_a = aid;
            e = wr(fpa, pr, sizeof pr);
            if (e != LIB_OK) break;
        }
        uint32_t src = st != LIB_NONE ? st : ld32(w->tr + TR_TITLE);
        uint8_t kr[KEY_REC], ar[ALB_REC];
        key24(kr, pool_get(pool, 0, src), NULL);
        st32(kr + 24, src);
        st32(kr + 28, id);
        e = wr(fti, kr, sizeof kr);
        if (e != LIB_OK) break;
        st32(ar, alb);
        st32(ar + 4, ((uint32_t)w->tr[TR_DISC] << 16) | ld16(w->tr + TR_TRACK));
        memcpy(ar + 8, kr, LIB_KEY_PREFIX);
        st32(ar + 32, src);
        st32(ar + 36, id);
        st32(ar + 40, gid == LIB_NONE || gid >= LIB_GENRE_NONE ? LIB_NONE : gid);
        e = wr(fal, ar, sizeof ar);
        id++;
    }
    lib_err_t ec;
    if (fpa_o) { ec = close_f(fpa); if (e == LIB_OK) e = ec; }
    if (fal_o) { ec = close_f(fal); if (e == LIB_OK) e = ec; }
    if (fti_o) { ec = close_f(fti); if (e == LIB_OK) e = ec; }
    if (ft_o) { ec = close_f(ft); if (e == LIB_OK) e = ec; }
    if (oa_o) f_close(oa->map);
    if (ob_o) f_close(ob->map);
    if (og_o) f_close(og->map);
    walk_in_close(w);
    pool_close(pool);
    c->ar->used = mark;
    return e;
}

/* ---- orders ---------------------------------------------------------------------------------------------------------- */

static lib_err_t patch32(FIL *f, uint32_t file_off, uint32_t v) {
    uint8_t b[4];
    st32(b, v);
    if (f_lseek(f, file_off) != FR_OK) return LIB_E_IO;
    return wr(f, b, 4);
}

// Sorted KEY_REC file -> a u32 id file (by_title / by_album_name) plus the letter jumps of the list.
static lib_err_t finish_idlist(bctx_t *c, const char *sorted_p, unsigned lf, lib_list_t list) {
    size_t mark = c->ar->used;
    FIL *fin = lib_arena_alloc(c->ar, sizeof(FIL)), *fo = lib_arena_alloc(c->ar, sizeof(FIL));
    if (fo == NULL) return LIB_E_SCRATCH;
    lib_err_t e = open_out(c, fo, lib_files[lf].name);
    bool fo_o = e == LIB_OK, fin_o = false;
    if (e == LIB_OK) e = lib_fopen(fin, sorted_p, FA_READ);
    fin_o = e == LIB_OK;
    uint32_t row = 0;
    uint8_t* buf = lib_arena_alloc(c->ar, 2048);
    size_t fill = 0;
    if (buf == NULL) e = LIB_E_SCRATCH;
    while (e == LIB_OK) {
        uint8_t kr[KEY_REC];
        bool eof = false;
        e = rd(fin, kr, KEY_REC, &eof);
        if (e != LIB_OK || eof) break;
        jump_feed(c, list, row++, lib_collate_bucket(kr));
        if (fill + 4 > 2048) {
            e = wr(fo, buf, (uint32_t)fill);
            fill = 0;
            if (e != LIB_OK) break;
        }
        memcpy(buf + fill, kr + 28, 4);
        fill += 4;
    }
    if (e == LIB_OK && fill > 0) e = wr(fo, buf, (uint32_t)fill);
    jump_done(c, list);
    if (fin_o) f_close(fin);
    if (fo_o) {
        lib_err_t ec = close_f(fo);
        if (e == LIB_OK) e = ec;
    }
    c->ar->used = mark;
    return e;
}

static lib_err_t stage_orders(bctx_t *c) {
    size_t mark = c->ar->used;
    pool_t *pool = lib_arena_alloc(c->ar, sizeof(pool_t));
    char p_title[96], p_alb[96], p_pair[96], s_title[96], s_alb[96], s_pair[96], s_an[96], an[96], p_gen[96], s_gen[96];
    wpath(c, p_title, "title.rec");
    wpath(c, p_alb, "album.rec");
    wpath(c, p_pair, "pair.rec");
    wpath(c, s_title, "title.sorted");
    wpath(c, s_alb, "album.sorted");
    wpath(c, s_pair, "pair.sorted");
    wpath(c, an, "albname.rec");
    wpath(c, s_an, "albname.sorted");
    wpath(c, p_gen, "gen.rec");
    wpath(c, s_gen, "gen.sorted");
    if (pool == NULL) {
        pool_close(pool);
        c->ar->used = mark;
        return LIB_E_SCRATCH;
    }
    lib_err_t e = pool_open(c, pool);
    if (e != LIB_OK) {
        pool_close(pool);
        c->ar->used = mark;
        return e;
    }
    /* songs by title */
    e = lib_esort(c->ar, c->work, p_title, s_title, KEY_REC, key_cmp, pool, &c->sst);
    (void)lib_unlink(p_title);
    if (e == LIB_OK) e = finish_idlist(c, s_title, LF_BY_TITLE, LIB_LIST_SONGS);
    (void)lib_unlink(s_title);
    /* albums by name */
    if (e == LIB_OK) e = lib_esort(c->ar, c->work, an, s_an, KEY_REC, key_cmp, pool, &c->sst);
    (void)lib_unlink(an);
    if (e == LIB_OK) e = finish_idlist(c, s_an, LF_BY_ALBUM_NAME, LIB_LIST_ALBUMS);
    (void)lib_unlink(s_an);
    /* tracks in album order; patches each album's first position */
    if (e == LIB_OK) e = lib_esort(c->ar, c->work, p_alb, s_alb, ALB_REC, alb_cmp, pool, &c->sst);
    (void)lib_unlink(p_alb);
    if (e == LIB_OK) {
        FIL *fin = lib_arena_alloc(c->ar, sizeof(FIL)), *fo = lib_arena_alloc(c->ar, sizeof(FIL)), *fa = lib_arena_alloc(c->ar, sizeof(FIL));
        FIL *fgn = lib_arena_alloc(c->ar, sizeof(FIL));
        char pa[96];
        tpath(c, pa, lib_files[LF_ALBUMS].name);
        if (fgn == NULL) e = LIB_E_SCRATCH;
        if (e == LIB_OK) e = open_out(c, fo, lib_files[LF_BY_ALBUM].name);
        bool fo_o = e == LIB_OK, fin_o = false, fa_o = false, fgn_o = false;
        if (e == LIB_OK) e = lib_fopen(fin, s_alb, FA_READ);
        fin_o = e == LIB_OK;
        if (e == LIB_OK) e = lib_fopen(fa, pa, FA_READ | FA_WRITE);
        fa_o = e == LIB_OK;
        if (e == LIB_OK) e = lib_fopen(fgn, p_gen, FA_CREATE_ALWAYS | FA_WRITE);
        fgn_o = e == LIB_OK;
        uint32_t pos = 0, last = LIB_NONE;
        while (e == LIB_OK) {
            uint8_t ar[ALB_REC];
            bool eof = false;
            e = rd(fin, ar, ALB_REC, &eof);
            if (e != LIB_OK || eof) break;
            uint32_t album = ld32(ar);
            if (album != last) {
                e = patch32(fa, LIB_HDR + album * LIB_REC_ALBUM + 12, pos);
                last = album;
                if (e != LIB_OK) break;
            }
            if (e == LIB_OK) e = wr(fo, ar + 36, 4);
            if (e == LIB_OK && ld32(ar + 40) != LIB_NONE) { // the genre order is this order, stably grouped by genre
                uint8_t gr[GEN_REC];
                st32(gr, ld32(ar + 40));
                st32(gr + 4, pos);
                st32(gr + 8, ld32(ar + 36));
                e = wr(fgn, gr, sizeof gr);
            }
            pos++;
        }
        if (fgn_o) {
            lib_err_t ec = close_f(fgn);
            if (e == LIB_OK) e = ec;
        }
        if (e == LIB_OK) e = patch32(fa, LIB_HDR + c->m.albums * LIB_REC_ALBUM + 12, pos); // sentinel: the total
        if (fa_o) {
            lib_err_t ec = close_f(fa);
            if (e == LIB_OK) e = ec;
        }
        if (fin_o) f_close(fin);
        if (fo_o) {
            lib_err_t ec = close_f(fo);
            if (e == LIB_OK) e = ec;
        }
    }
    (void)lib_unlink(s_alb);
    /* genre -> tracks (play a genre): GEN_REC sorted by (genre, album-order position) */
    if (e == LIB_OK) e = lib_esort(c->ar, c->work, p_gen, s_gen, GEN_REC, gen_cmp, NULL, &c->sst);
    (void)lib_unlink(p_gen);
    if (e == LIB_OK) {
        FIL *fin = lib_arena_alloc(c->ar, sizeof(FIL)), *fo = lib_arena_alloc(c->ar, sizeof(FIL)), *fg = lib_arena_alloc(c->ar, sizeof(FIL));
        char pg[96];
        tpath(c, pg, lib_files[LF_GENRES].name);
        if (fg == NULL) e = LIB_E_SCRATCH;
        if (e == LIB_OK) e = open_out(c, fo, lib_files[LF_BY_GENRE].name);
        bool fo_o = e == LIB_OK, fin_o = false, fg_o = false;
        if (e == LIB_OK) e = lib_fopen(fin, s_gen, FA_READ);
        fin_o = e == LIB_OK;
        if (e == LIB_OK) e = lib_fopen(fg, pg, FA_READ | FA_WRITE);
        fg_o = e == LIB_OK;
        uint32_t pos = 0, next_g = 0;
        while (e == LIB_OK) {
            uint8_t gr[GEN_REC];
            bool eof = false;
            e = rd(fin, gr, GEN_REC, &eof);
            if (e != LIB_OK || eof) break;
            uint32_t g = ld32(gr);
            while (next_g <= g && e == LIB_OK) { // every genre up to this one starts here (a genre cannot be empty, but stay safe)
                e = patch32(fg, LIB_HDR + next_g * LIB_REC_GENRE + 16, pos);
                next_g++;
            }
            if (e == LIB_OK) e = wr(fo, gr + 8, 4);
            pos++;
        }
        while (e == LIB_OK && next_g <= c->m.genres) { // the rest, sentinel included
            e = patch32(fg, LIB_HDR + next_g * LIB_REC_GENRE + 16, pos);
            next_g++;
        }
        c->m.genre_tracks = pos;
        if (fg_o) {
            lib_err_t ec = close_f(fg);
            if (e == LIB_OK) e = ec;
        }
        if (fin_o) f_close(fin);
        if (fo_o) {
            lib_err_t ec = close_f(fo);
            if (e == LIB_OK) e = ec;
        }
    }
    (void)lib_unlink(s_gen);
    /* genre -> artists */
    if (e == LIB_OK) e = lib_esort(c->ar, c->work, p_pair, s_pair, 8, pair_cmp, NULL, &c->sst);
    (void)lib_unlink(p_pair);
    if (e == LIB_OK) {
        FIL *fin = lib_arena_alloc(c->ar, sizeof(FIL)), *fo = lib_arena_alloc(c->ar, sizeof(FIL)), *fg = lib_arena_alloc(c->ar, sizeof(FIL));
        char pg[96];
        tpath(c, pg, lib_files[LF_GENRES].name);
        if (fg == NULL) e = LIB_E_SCRATCH;
        if (e == LIB_OK) e = open_out(c, fo, lib_files[LF_GENRE_ARTISTS].name);
        bool fo_o = e == LIB_OK, fin_o = false, fg_o = false;
        if (e == LIB_OK) e = lib_fopen(fin, s_pair, FA_READ);
        fin_o = e == LIB_OK;
        if (e == LIB_OK) e = lib_fopen(fg, pg, FA_READ | FA_WRITE);
        fg_o = e == LIB_OK;
        uint32_t pos = 0, next_g = 0, last_g = LIB_NONE, last_a = LIB_NONE;
        while (e == LIB_OK) {
            uint8_t pr[8];
            bool eof = false;
            e = rd(fin, pr, 8, &eof);
            if (e != LIB_OK || eof) break;
            uint32_t g = ld32(pr), a = ld32(pr + 4);
            if (g == last_g && a == last_a) continue; // one link per (genre, artist)
            last_g = g;
            last_a = a;
            while (next_g <= g && e == LIB_OK) { // empty genres (only compilation tracks) get the same start as the next one
                e = patch32(fg, LIB_HDR + next_g * LIB_REC_GENRE + 12, pos);
                next_g++;
            }
            if (e == LIB_OK) e = wr(fo, pr + 4, 4);
            pos++;
        }
        while (e == LIB_OK && next_g <= c->m.genres) { // the rest, sentinel included
            e = patch32(fg, LIB_HDR + next_g * LIB_REC_GENRE + 12, pos);
            next_g++;
        }
        c->m.genre_artist_links = pos;
        if (fg_o) {
            lib_err_t ec = close_f(fg);
            if (e == LIB_OK) e = ec;
        }
        if (fin_o) f_close(fin);
        if (fo_o) {
            lib_err_t ec = close_f(fo);
            if (e == LIB_OK) e = ec;
        }
    }
    (void)lib_unlink(s_pair);
    /* artists' first album: albums are in artist order */
    if (e == LIB_OK) {
        FIL *fa = lib_arena_alloc(c->ar, sizeof(FIL)), *fr = lib_arena_alloc(c->ar, sizeof(FIL));
        char pa[96], pr[96];
        tpath(c, pa, lib_files[LF_ARTISTS].name);
        tpath(c, pr, lib_files[LF_ALBUMS].name);
        if (fr == NULL) e = LIB_E_SCRATCH;
        if (e == LIB_OK) e = lib_fopen(fa, pa, FA_READ | FA_WRITE);
        bool fa_o = e == LIB_OK, fr_o = false;
        if (e == LIB_OK) e = lib_fopen(fr, pr, FA_READ);
        fr_o = e == LIB_OK;
        if (e == LIB_OK) e = f_lseek(fr, LIB_HDR) == FR_OK ? LIB_OK : LIB_E_IO;
        uint32_t last = LIB_NONE;
        for (uint32_t i = 0; e == LIB_OK && i < c->m.albums; i++) {
            uint8_t rec[LIB_REC_ALBUM];
            e = rd(fr, rec, sizeof rec, NULL);
            if (e != LIB_OK) break;
            uint32_t a = ld32(rec + 16);
            if (a == LIB_NONE || a == last) continue;
            e = patch32(fa, LIB_HDR + a * LIB_REC_ARTIST + 12, i);
            last = a;
        }
        if (e == LIB_OK) e = patch32(fa, LIB_HDR + c->m.artists * LIB_REC_ARTIST + 12, c->m.comp_first);
        if (fr_o) f_close(fr);
        if (fa_o) {
            lib_err_t ec = close_f(fa);
            if (e == LIB_OK) e = ec;
        }
    }
    pool_close(pool);
    c->ar->used = mark;
    return e;
}

/* ---- headers, manifest ---------------------------------------------------------------------------------------------- */

static lib_err_t finalize_file(bctx_t *c, unsigned idx) {
    char p[96];
    size_t mark = c->ar->used;
    FIL *f = lib_arena_alloc(c->ar, sizeof(FIL));
    uint8_t *buf = lib_arena_alloc(c->ar, 2048);
    if (buf == NULL) return LIB_E_SCRATCH;
    tpath(c, p, lib_files[idx].name);
    lib_err_t oe = lib_fopen(f, p, FA_READ | FA_WRITE);
    if (oe != LIB_OK) {
        c->ar->used = mark;
        return oe;
    }
    FSIZE_t size = f_size(f);
    lib_err_t e = size >= LIB_HDR ? LIB_OK : LIB_E_FORMAT;
    uint32_t crc = 0;
    if (e == LIB_OK) e = f_lseek(f, LIB_HDR) == FR_OK ? LIB_OK : LIB_E_IO;
    while (e == LIB_OK) {
        UINT got = 0;
        if (f_read(f, buf, 2048, &got) != FR_OK) {
            e = LIB_E_IO;
            break;
        }
        if (got == 0) break;
        crc = lib_crc32(crc, buf, got);
    }
    uint32_t payload = (uint32_t)(size - LIB_HDR), rec = lib_files[idx].rec;
    uint32_t count = payload / rec;
    if (e == LIB_OK && (payload % rec != 0 || count < lib_files[idx].sentinel)) e = LIB_E_FORMAT;
    count -= lib_files[idx].sentinel;
    if (e == LIB_OK) {
        uint8_t h[LIB_HDR];
        st32(h, lib_magic(idx));
        st32(h + 4, LIB_VERSION);
        st32(h + 8, count);
        st32(h + 12, crc);
        e = f_lseek(f, 0) == FR_OK ? wr(f, h, LIB_HDR) : LIB_E_IO;
    }
    lib_err_t ec = close_f(f);
    if (e == LIB_OK) e = ec;
    c->m.file_crc[idx] = crc;
    c->m.file_size[idx] = (uint32_t)size;
    c->ar->used = mark;
    return e;
}

static lib_err_t write_manifest(bctx_t *c) {
    size_t mark = c->ar->used;
    size_t n = lib_manifest_bytes();
    uint8_t *buf = lib_arena_alloc(c->ar, n);
    FIL *f = lib_arena_alloc(c->ar, sizeof(FIL));
    if (f == NULL) return LIB_E_SCRATCH;
    lib_manifest_encode(&c->m, buf);
    char p[96];
    tpath(c, p, lib_files[LF_MANIFEST].name);
    lib_err_t e = lib_fopen(f, p, FA_CREATE_ALWAYS | FA_WRITE);
    if (e == LIB_OK) {
        e = wr(f, buf, (uint32_t)n);
        lib_err_t ec = close_f(f);
        if (e == LIB_OK) e = ec;
    }
    c->ar->used = mark;
    return e;
}

/* ---- entry ---------------------------------------------------------------------------------------------------------- */

static lib_err_t build_inner(bctx_t *c, int *step) {
    // A swap cut by a power loss must be settled first: the swap deletes lib.old, which may still be the only valid library.
    lib_err_t r = lib_recover_locked(c->base);
    if (r != LIB_OK && r != LIB_E_NONE) return r; // could not settle the last swap: building now could delete the only good library
    lib_err_t e = lib_rmtree(c->tmp);
    if (e == LIB_OK) e = lib_rmtree(c->work);
    if (e != LIB_OK) return e;
    e = lib_mkdir(c->base);
    if (e == LIB_OK) e = lib_mkdir(c->tmp);
    if (e == LIB_OK) e = lib_mkdir(c->work);
    if (e != LIB_OK) return e;

    char lib[64];
    lib_path(lib, sizeof lib, c->base, "lib");
    lib_manifest_t old;
    c->m.generation = lib_manifest_load(lib, false, &old) == LIB_OK ? old.generation + 1 : 1;

    e = stage_walk(c);
    if (e == LIB_OK) e = stage_entity(c, K_ARTIST);
    if (e == LIB_OK) e = stage_entity(c, K_ALBUM);
    if (e == LIB_OK) e = stage_entity(c, K_GENRE);
    if (e == LIB_OK) e = stage_tracks(c);
    if (e == LIB_OK) e = stage_orders(c);
    if (e == LIB_OK) {
        e = lib_rmtree(c->work);
    }
    if (e != LIB_OK) return e;
    for (unsigned i = 0; i < LIB_FILES && e == LIB_OK; i++) e = finalize_file(c, i);
    if (e != LIB_OK) return e;
    c->m.tracks = c->n_tracks;
    c->m.playable = c->n_playable;
    c->m.dirs = c->n_dirs;
    c->m.decoder_version = c->cfg->decoder_version != 0 ? c->cfg->decoder_version : LIB_DECODER_SET_VERSION;
    c->m.fingerprint = c->cfg->fingerprint;
    *step = 1;
    if (LIB_STOP_AT(LIB_TEST_STEP(c->cfg), 1)) return LIB_E_TEST_STOP_;
    e = write_manifest(c);
    if (e != LIB_OK) return e;
    *step = 2;
    if (LIB_STOP_AT(LIB_TEST_STEP(c->cfg), 2)) return LIB_E_TEST_STOP_;
    return lib_swap_install(c->base, LIB_TEST_STEP(c->cfg), step);
}

lib_err_t lib_build(const lib_build_cfg_t *cfg, void *scratch, size_t scratch_n, lib_build_stats_t *stats) {
    if (scratch == NULL || scratch_n < LIB_SCRATCH_MIN || ((uintptr_t)scratch & 7u) != 0) return LIB_E_SCRATCH;
    lib_arena_t ar = {.base = scratch, .cap = scratch_n};
    bctx_t *c = lib_arena_alloc(&ar, sizeof *c);
    if (c == NULL) return LIB_E_SCRATCH;
    memset(c, 0, sizeof *c);
    c->cfg = cfg;
    c->ar = &ar;
    const char *base = cfg->base != NULL ? cfg->base : LIB_BASE_DEFAULT, *root = cfg->root != NULL ? cfg->root : LIB_ROOT_DEFAULT;
    if (strlen(base) > LIB_BASE_MAX || strlen(root) >= sizeof c->root) return LIB_E_RANGE; // (nothing has been touched)
    if (meta_tags_ext() != NULL) return LIB_E_BUSY;                                       // another scan holds the parser hooks
    snprintf(c->base, sizeof c->base, "%s", base);
    snprintf(c->root, sizeof c->root, "%s", root);
    if (!lib_path(c->tmp, sizeof c->tmp, c->base, "lib.tmp") || !lib_path(c->work, sizeof c->work, c->base, "work")) return LIB_E_RANGE;

    bool was = false;
    lib_err_t e = lib_write_begin(c->base, &was); // nfs write scope + hardware lock, only for the duration
    if (e != LIB_OK) return e;
    int step = 0;
    e = build_inner(c, &step);
    // Failure stance: nothing but lib.tmp / work has been touched before the swap, so a failed or cancelled build only needs those
    // removed; a simulated power cut (tests) leaves everything as it was, for lib_recover().
    if (e != LIB_OK && !LIB_IS_STOP(e) && step < 3) {
        lib_rmtree(c->tmp);
        lib_rmtree(c->work);
    }
    lib_write_end(was);
    if (stats != NULL) {
        stats->tracks = c->m.tracks;
        stats->playable = c->m.playable;
        stats->artists = c->m.artists;
        stats->albums = c->m.albums;
        stats->genres = c->m.genres;
        stats->dirs = c->m.dirs;
        stats->generation = c->m.generation;
        stats->scratch_peak = (uint32_t)ar.peak;
        stats->sort_runs = c->sst.runs;
        stats->sort_merges = c->sst.merges;
    }
    return e;
}

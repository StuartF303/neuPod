// SPDX-License-Identifier: MPL-2.0
// Shared by test_library.c and test_library_big.c: a FatFs volume on a host disk image, and tiny synthetic audio files (FLAC, MP3,
// WAV, MP4) whose headers parse and whose tags are the ones asked for. Header-only, static functions.
#ifndef NEUPOD_TESTS_LIBRARY_FIXTURE_H
#define NEUPOD_TESTS_LIBRARY_FIXTURE_H

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ff.h"
#include "hal_host.h"
#include "hal_storage.h"

/* ---- volume ------------------------------------------------------------------------------------------------------ */

static char g_image[] = "neupod_library_test_XXXXXX";

/** A fresh exFAT volume of @p bytes (sparse file), mounted, write lock on. @p au: cluster size in bytes (0 = FatFs default). */
static inline bool fx_volume(uint64_t bytes, uint32_t au) {
    static FATFS fs;
    static BYTE work[FF_MAX_SS * 8];
    hal_host_storage_set_mounted(false);
    f_mount(NULL, "", 0);
    hal_storage_deinit();
    static bool named;
    if (!named) { /* first use: create the file name */
        int fd = mkstemp(g_image);
        if (fd < 0) return false;
        close(fd);
        named = true;
    }
    if (truncate(g_image, 0) != 0 || truncate(g_image, (off_t)bytes) != 0) return false;
    if (setenv("NEUPOD_DISK_IMAGE", g_image, 1) != 0) return false;
    hal_storage_set_write_enabled(true);
    if (hal_storage_init() != HAL_STORAGE_OK) return false;
    MKFS_PARM opt = {FM_EXFAT, 1, 0, 0, au};
    if (f_mkfs("", &opt, work, sizeof work) != FR_OK) return false;
    if (f_mount(&fs, "", 1) != FR_OK) return false;
    hal_host_storage_set_mounted(true); /* nfs_fatfs.c (linked into the test) checks it */
    return true;
}

static inline void fx_cleanup(void) {
    hal_host_storage_set_mounted(false);
    f_mount(NULL, "", 0);
    hal_storage_deinit();
    unlink(g_image);
}

static inline bool fx_mkdirs(const char *path) { /* every missing folder of "/a/b/c" (the last component is a folder too) */
    char p[256];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s != '\0'; s++) {
        if (*s != '/') continue;
        *s = '\0';
        FRESULT fr = f_mkdir(p);
        if (fr != FR_OK && fr != FR_EXIST) return false;
        *s = '/';
    }
    FRESULT fr = f_mkdir(p);
    return fr == FR_OK || fr == FR_EXIST;
}

static inline bool fx_put(const char *path, const void *data, size_t n) {
    char dir[256];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash != NULL && slash != dir) {
        *slash = '\0';
        if (!fx_mkdirs(dir)) return false;
    }
    FIL f;
    UINT w = 0;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    FRESULT fr = f_write(&f, data, (UINT)n, &w);
    return f_close(&f) == FR_OK && fr == FR_OK && w == n;
}

/* ---- tags -------------------------------------------------------------------------------------------------------- */

typedef struct {
    const char *title, *artist, *album, *aartist, *genre; /* NULL / "" = absent */
    const char *s_title, *s_artist, *s_album, *s_aartist;
    int track, disc, year;
    bool comp;
} fx_tags_t;

static inline size_t fx_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    return 4;
}
static inline size_t fx_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return 4;
}

static inline size_t fx_vc(uint8_t *p, const char *key, const char *val) {
    if (val == NULL || val[0] == '\0') return 0;
    size_t kl = strlen(key), vl = strlen(val);
    fx_le32(p, (uint32_t)(kl + 1 + vl));
    memcpy(p + 4, key, kl);
    p[4 + kl] = '=';
    memcpy(p + 5 + kl, val, vl);
    return 4 + kl + 1 + vl;
}

static inline size_t fx_vc_num(uint8_t *p, const char *key, int v) {
    char t[16];
    if (v <= 0) return 0;
    snprintf(t, sizeof t, "%d", v);
    return fx_vc(p, key, t);
}

/** A FLAC file: STREAMINFO (44.1 kHz, 16 bit, stereo, @p seconds) and one Vorbis comment block, no audio frames. */
static inline size_t fx_flac(uint8_t *b, const fx_tags_t *t, unsigned seconds) {
    size_t n = 0;
    memcpy(b, "fLaC", 4);
    n = 4;
    b[n++] = 0;
    b[n++] = 0;
    b[n++] = 0;
    b[n++] = 34;
    uint8_t si[34] = {0x10, 0x00, 0x10, 0x00};
    si[10] = 0x0A;
    si[11] = 0xC4;
    si[12] = 0x42;
    uint64_t total = 44100ull * seconds;
    si[13] = (uint8_t)(0xF0u | ((total >> 32) & 0xFu));
    fx_be32(si + 14, (uint32_t)total);
    memcpy(b + n, si, 34);
    n += 34;
    uint8_t vc[1024];
    size_t v = 0;
    fx_le32(vc, 0); /* empty vendor */
    v = 4;
    size_t count_at = v;
    v += 4;
    uint32_t count = 0;
    struct {
        const char *k;
        const char *val;
    } texts[] = {{"TITLE", t->title},       {"ARTIST", t->artist},        {"ALBUM", t->album},          {"ALBUMARTIST", t->aartist},
                 {"GENRE", t->genre},       {"TITLESORT", t->s_title},    {"ARTISTSORT", t->s_artist},  {"ALBUMSORT", t->s_album},
                 {"ALBUMARTISTSORT", t->s_aartist}};
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        size_t k = fx_vc(vc + v, texts[i].k, texts[i].val);
        v += k;
        count += k != 0;
    }
    size_t k;
    k = fx_vc_num(vc + v, "TRACKNUMBER", t->track), v += k, count += k != 0;
    k = fx_vc_num(vc + v, "DISCNUMBER", t->disc), v += k, count += k != 0;
    k = fx_vc_num(vc + v, "DATE", t->year), v += k, count += k != 0;
    k = t->comp ? fx_vc(vc + v, "COMPILATION", "1") : 0, v += k, count += k != 0;
    fx_le32(vc + count_at, count);
    b[n++] = 0x80 | 4; /* last block, VORBIS_COMMENT */
    b[n++] = (uint8_t)(v >> 16);
    b[n++] = (uint8_t)(v >> 8);
    b[n++] = (uint8_t)v;
    memcpy(b + n, vc, v);
    n += v;
    return n;
}

static inline size_t fx_id3_frame(uint8_t *p, const char *id, const char *text) {
    if (text == NULL || text[0] == '\0') return 0;
    size_t tl = strlen(text);
    memcpy(p, id, 4);
    fx_be32(p + 4, (uint32_t)(tl + 1));
    p[8] = p[9] = 0;
    p[10] = 0; /* ISO-8859-1 / ASCII (the tests that need UTF-8 give UTF-8 bytes: the parser copies encoding 0 as Latin-1, so keep ASCII) */
    memcpy(p + 11, text, tl);
    return 11 + tl;
}

/** An MP3: ID3v2.3 (or 2.4 with TDRC instead of TYER) tag and two valid MPEG-1 Layer III frames (128 kbit/s, 44.1 kHz). */
static inline size_t fx_mp3(uint8_t *b, const fx_tags_t *t, bool v24) {
    uint8_t fr[1024];
    size_t n = 0;
    char num[16];
    n += fx_id3_frame(fr + n, "TIT2", t->title);
    n += fx_id3_frame(fr + n, "TPE1", t->artist);
    n += fx_id3_frame(fr + n, "TALB", t->album);
    n += fx_id3_frame(fr + n, "TPE2", t->aartist);
    n += fx_id3_frame(fr + n, "TCON", t->genre);
    n += fx_id3_frame(fr + n, "TSOT", t->s_title);
    n += fx_id3_frame(fr + n, "TSOP", t->s_artist);
    n += fx_id3_frame(fr + n, "TSOA", t->s_album);
    n += fx_id3_frame(fr + n, "TSO2", t->s_aartist);
    if (t->track > 0) snprintf(num, sizeof num, "%d/12", t->track), n += fx_id3_frame(fr + n, "TRCK", num);
    if (t->disc > 0) snprintf(num, sizeof num, "%d/2", t->disc), n += fx_id3_frame(fr + n, "TPOS", num);
    if (t->year > 0) snprintf(num, sizeof num, "%d", t->year), n += fx_id3_frame(fr + n, v24 ? "TDRC" : "TYER", num);
    if (t->comp) n += fx_id3_frame(fr + n, "TCMP", "1");
    size_t at = 0;
    memcpy(b, "ID3", 3);
    b[3] = v24 ? 4 : 3;
    b[4] = 0;
    b[5] = 0;
    b[6] = (uint8_t)((n >> 21) & 0x7F);
    b[7] = (uint8_t)((n >> 14) & 0x7F);
    b[8] = (uint8_t)((n >> 7) & 0x7F);
    b[9] = (uint8_t)(n & 0x7F);
    at = 10;
    if (v24) { /* v2.4 frame sizes are syncsafe */
        for (size_t p = 0; p < n;) {
            uint32_t sz = (uint32_t)fr[p + 4] << 24 | (uint32_t)fr[p + 5] << 16 | (uint32_t)fr[p + 6] << 8 | fr[p + 7];
            fr[p + 4] = (uint8_t)((sz >> 21) & 0x7F);
            fr[p + 5] = (uint8_t)((sz >> 14) & 0x7F);
            fr[p + 6] = (uint8_t)((sz >> 7) & 0x7F);
            fr[p + 7] = (uint8_t)(sz & 0x7F);
            p += 10 + sz;
        }
    }
    memcpy(b + at, fr, n);
    at += n;
    for (int f = 0; f < 2; f++) {
        memset(b + at, 0, 417);
        b[at] = 0xFF;
        b[at + 1] = 0xFB;
        b[at + 2] = 0x90;
        b[at + 3] = 0x00;
        at += 417;
    }
    return at;
}

/** A WAV: 16-bit stereo 44.1 kHz, @p frames frames of silence. No tags. */
static inline size_t fx_wav(uint8_t *b, unsigned frames) {
    size_t n = 0;
    uint32_t data = frames * 4u;
    memcpy(b, "RIFF", 4);
    fx_le32(b + 4, 36 + data);
    memcpy(b + 8, "WAVEfmt ", 8);
    fx_le32(b + 16, 16);
    b[20] = 1, b[21] = 0, b[22] = 2, b[23] = 0;
    fx_le32(b + 24, 44100);
    fx_le32(b + 28, 44100 * 4);
    b[32] = 4, b[33] = 0, b[34] = 16, b[35] = 0;
    memcpy(b + 36, "data", 4);
    fx_le32(b + 40, data);
    memset(b + 44, 0, data);
    n = 44 + data;
    return n;
}

static inline size_t fx_box(uint8_t *p, const char *type, const uint8_t *body, size_t n) {
    fx_be32(p, (uint32_t)(8 + n));
    memcpy(p + 4, type, 4);
    if (n > 0) memcpy(p + 8, body, n);
    return 8 + n;
}

static inline size_t fx_ilst_text(uint8_t *p, const char type[4], const char *text) {
    if (text == NULL || text[0] == '\0') return 0;
    uint8_t d[200], item[220];
    size_t tl = strlen(text);
    fx_be32(d, 1); /* UTF-8 text */
    fx_be32(d + 4, 0);
    memcpy(d + 8, text, tl);
    size_t k = fx_box(item, "data", d, 8 + tl);
    return fx_box(p, type, item, k);
}

static inline size_t fx_ilst_data(uint8_t *p, const char type[4], const uint8_t *val, size_t n) {
    uint8_t d[64], item[80];
    fx_be32(d, 0);
    fx_be32(d + 4, 0);
    memcpy(d + 8, val, n);
    size_t k = fx_box(item, "data", d, 8 + n);
    return fx_box(p, type, item, k);
}

/** An MP4 whose only audio is "mp4a" (AAC, which the player cannot decode) and whose ilst carries the tags. Tests D6 (listed, greyed out). */
static inline size_t fx_m4a_aac(uint8_t *b, const fx_tags_t *t) {
    static const char A9[] = {(char)0xA9, 0};
    uint8_t ilst[1024], tmp[1024], trak[512];
    size_t n = 0;
    char key[5] = {(char)0xA9, 'n', 'a', 'm', 0};
    n += fx_ilst_text(ilst + n, key, t->title);
    key[1] = 'A', key[2] = 'R', key[3] = 'T';
    n += fx_ilst_text(ilst + n, key, t->artist);
    key[1] = 'a', key[2] = 'l', key[3] = 'b';
    n += fx_ilst_text(ilst + n, key, t->album);
    n += fx_ilst_text(ilst + n, "aART", t->aartist);
    key[1] = 'g', key[2] = 'e', key[3] = 'n';
    n += fx_ilst_text(ilst + n, key, t->genre);
    (void)A9;
    if (t->year > 0) {
        char y[32];
        snprintf(y, sizeof y, "%d-05-04T08:00:00Z", t->year);
        key[1] = 'd', key[2] = 'a', key[3] = 'y';
        n += fx_ilst_text(ilst + n, key, y);
    }
    if (t->track > 0) {
        uint8_t v[8] = {0, 0, (uint8_t)(t->track >> 8), (uint8_t)t->track, 0, 12, 0, 0};
        n += fx_ilst_data(ilst + n, "trkn", v, 8);
    }
    if (t->disc > 0) {
        uint8_t v[6] = {0, 0, (uint8_t)(t->disc >> 8), (uint8_t)t->disc, 0, 2};
        n += fx_ilst_data(ilst + n, "disk", v, 6);
    }
    if (t->comp) {
        uint8_t v[1] = {1};
        n += fx_ilst_data(ilst + n, "cpil", v, 1);
    }
    n += fx_ilst_text(ilst + n, "soar", t->s_artist);
    n += fx_ilst_text(ilst + n, "soaa", t->s_aartist);
    n += fx_ilst_text(ilst + n, "soal", t->s_album);
    n += fx_ilst_text(ilst + n, "sonm", t->s_title);
    uint8_t ib[1100];
    size_t k = fx_box(ib, "ilst", ilst, n);
    uint8_t meta[1200];
    memset(meta, 0, 4); /* meta is a full box */
    memcpy(meta + 4, ib, k);
    size_t mk = fx_box(tmp, "meta", meta, 4 + k);
    uint8_t udta[1200];
    size_t uk = fx_box(udta, "udta", tmp, mk);
    /* trak > mdia > { hdlr(soun), minf > stbl > stsd(mp4a) } */
    uint8_t hdlr[24] = {0}, stsd[44] = {0}, stbl[80], minf[100], mdia[200];
    memcpy(hdlr + 8, "soun", 4);
    uint8_t hb[40];
    size_t hk = fx_box(hb, "hdlr", hdlr, 24);
    fx_be32(stsd + 4, 1);
    fx_be32(stsd + 8, 36);
    memcpy(stsd + 12, "mp4a", 4);
    uint8_t sb[60];
    size_t sk = fx_box(sb, "stsd", stsd, 44);
    size_t bk = fx_box(stbl, "stbl", sb, sk);
    size_t mk2 = fx_box(minf, "minf", stbl, bk);
    memcpy(mdia, hb, hk);
    memcpy(mdia + hk, minf, mk2);
    uint8_t mdb[300];
    size_t mdk = fx_box(mdb, "mdia", mdia, hk + mk2);
    size_t tk = fx_box(trak, "trak", mdb, mdk);
    uint8_t moov[1800];
    memcpy(moov, trak, tk);
    memcpy(moov + tk, udta, uk);
    size_t at = 0;
    uint8_t ftyp[8] = {'M', '4', 'A', ' ', 0, 0, 0, 0};
    at += fx_box(b, "ftyp", ftyp, 8);
    at += fx_box(b + at, "moov", moov, tk + uk);
    return at;
}

#endif

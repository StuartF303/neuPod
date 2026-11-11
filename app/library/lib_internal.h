// SPDX-License-Identifier: MPL-2.0
/**
 * @file lib_internal.h
 * @brief Shared by the library's .c files only: the on-card layout (docs/adr/0017), the scratch arena, the external sort.
 */
#ifndef NEUPOD_LIB_INTERNAL_H
#define NEUPOD_LIB_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ff.h"
#include "library.h"

/* ---- on-card layout ----------------------------------------------------------------------------------------------- */

#define LIB_VERSION 1u
#define LIB_HDR 16u /**< magic, version, count, crc32 (of the payload) */

/** Files of the set. Order = manifest order; LF_MANIFEST is the 12th file and is not in the manifest's own table. */
enum { LF_DIRS, LF_NAMES, LF_TRACKS, LF_STRINGS, LF_ARTISTS, LF_ALBUMS, LF_GENRES, LF_BY_ALBUM, LF_BY_TITLE, LF_BY_ALBUM_NAME,
       LF_GENRE_ARTISTS, LF_ENAMES, LF_MANIFEST };

typedef struct {
    const char *name;
    uint8_t rec;      /**< record size in bytes (1 for the string pools: their count is bytes) */
    uint8_t sentinel; /**< 1: one extra record after `count` whose first-child field is the total (the table's end) */
} lib_file_info_t;

extern const lib_file_info_t lib_files[LIB_FILES + 1];

#define LIB_REC_DIR 8u     /**< parent u32, name_off u32 (names.str) */
#define LIB_REC_TRACK 48u
#define LIB_REC_ARTIST 16u /**< name_off u32, key[8], first album u32 */
#define LIB_REC_ALBUM 20u  /**< name_off u32, key[8], first position in by_album u32, artist id u32 */
#define LIB_REC_GENRE 16u  /**< name_off u32, key[8], first position in genre_artists u32 */

/* track record offsets */
#define TR_NAME 0u     /* u32 file name offset in names.str */
#define TR_TITLE 4u    /* u32 title offset in strings.str */
#define TR_ARTIST 8u   /* u32 track artist (display) offset in strings.str */
#define TR_DIR 12u     /* u32 */
#define TR_AARTIST 16u /* u32 album artist id, LIB_NONE for compilations */
#define TR_ALBUM 20u   /* u32 */
#define TR_SIZE 24u    /* u32, saturated at 4 GiB - 1 */
#define TR_MTIME 28u   /* u32 seconds since 1970 */
#define TR_DUR 32u     /* u32 seconds */
#define TR_GENRE 36u   /* u16, LIB_GENRE_NONE = none */
#define TR_YEAR 38u    /* u16 */
#define TR_TRACK 40u   /* u16 */
#define TR_DISC 42u    /* u8 (saturated) */
#define TR_FLAGS 43u   /* u8: bit0 compilation */
#define TR_CODEC 44u   /* u8: LC_* */
#define TR_BITS 45u    /* u8 */
#define TR_RATE 46u    /* u8: index into lib_rates[] (0 = unknown) */
#define TR_PLAY 47u    /* u8: lib_play_t */

extern const uint32_t lib_rates[12]; /**< sample rates a track record can name; 0 = unknown */

/** Entities and tracks use NONE for "no such". */
#define LIB_GENRE_NONE 0xFFFFu

/* little-endian access, no alignment assumptions */
static inline uint32_t ld32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t ld16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline void st32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static inline void st16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

uint32_t lib_crc32(uint32_t crc, const void *data, size_t n); /**< standard CRC-32 (IEEE), crc = 0 to start */
uint32_t lib_magic(unsigned file);                            /**< file magic: "NPL" + 'A' + index */

/* ---- scratch arena ------------------------------------------------------------------------------------------------ */

typedef struct {
    uint8_t *base;
    size_t cap, used, peak;
} lib_arena_t;

void *lib_arena_alloc(lib_arena_t *a, size_t n); /**< 8-byte aligned, NULL if it does not fit */
static inline size_t lib_arena_free(const lib_arena_t *a) { return a->cap - a->used; }

/* ---- paths and FatFs helpers -------------------------------------------------------------------------------------- */

lib_err_t lib_map_fr(FRESULT fr);
/** Delete every file in @p dir and the directory itself; fine if it does not exist. */
lib_err_t lib_rmtree(const char *dir);
bool lib_exists(const char *path);
/** Absolute offset of a file's payload byte 0 is LIB_HDR. Read exactly n bytes at file offset off. */
lib_err_t lib_read_at(FIL *f, uint32_t off, void *dst, uint32_t n);

/** "<base>/lib" ... into out (64 bytes). */
void lib_path(char *out, size_t cap, const char *base, const char *leaf);

/* ---- external sort ------------------------------------------------------------------------------------------------ */

typedef int (*lib_cmp_fn)(const void *a, const void *b, void *ctx);

typedef struct {
    uint32_t runs, merges; /**< runs written / merge steps made (each merges up to the fan-in), totals over all sorts */
} lib_sort_stats_t;

/**
 * Sort the fixed-size records (no header) of file @p src into @p dst (deleted first if it exists; @p src is left alone). Runs are
 * written to @p work_dir. Everything it needs is carved from @p ar and handed back on return. @p cmp must be a total order (break ties
 * on a sequence number). Empty input gives an empty @p dst.
 */
lib_err_t lib_esort(lib_arena_t *ar, const char *work_dir, const char *src, const char *dst, uint32_t recsz, lib_cmp_fn cmp, void *ctx,
                    lib_sort_stats_t *st);

#endif

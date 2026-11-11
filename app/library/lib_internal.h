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
/* Test hooks (NEUPOD_LIB_TEST_HOOKS, defined only for the test library): a simulated power cut after a swap step. Production builds have none. */
#ifdef NEUPOD_LIB_TEST_HOOKS
#define LIB_TEST_STEP(cfg) ((cfg)->test_stop_step)
#define LIB_STOP_AT(stop, n) ((stop) == (n))
#define LIB_IS_STOP(e) ((e) == LIB_E_TEST_STOP)
#define LIB_E_TEST_STOP_ LIB_E_TEST_STOP
#else
#define LIB_TEST_STEP(cfg) 0
#define LIB_STOP_AT(stop, n) ((void)(stop), 0)
#define LIB_IS_STOP(e) ((void)(e), 0)
#define LIB_E_TEST_STOP_ LIB_E_IO
#endif

/**
 * f_open with FatFs' "denied" (full volume or directory) reported as LIB_E_FULL. Anything that can write (FA_WRITE) is first checked
 * against the nfs write scope (lib_write_allowed): the FIL objects are ours (they live in the scratch, there are far more of them than
 * nfs.h's four handles), but the permission to write is nfs's.
 */
lib_err_t lib_fopen(FIL *f, const char *path, BYTE mode);
/* NFS_MAX_OPEN_FILES (4) is deliberately bypassed for the content FILs: the scanner holds up to ~40 open at once, carved from its 32 KB
 * scratch, where nfs's handles would cost ~1.6 KB of static RAM each. Directory-tree changes do go through nfs (lib_mkdir etc.). */
/** True only while the library's nfs write scope is open and @p path lies inside it. */
bool lib_write_allowed(const char *path);
/** Path operations that change the card go through nfs.h (same gate as the settings store). EXISTS counts as success for mkdir. */
lib_err_t lib_mkdir(const char *path);
lib_err_t lib_unlink(const char *path);
lib_err_t lib_rename_path(const char *from, const char *to);
/** Open the nfs write scope on LIB_BASE_DEFAULT and the hardware write lock; lib_write_end() closes both (lock first, then scope). */
lib_err_t lib_write_begin(const char *base, bool *lock_was);
void lib_write_end(bool lock_was);
/** Delete every file in @p dir and the directory itself; fine if it does not exist. */
lib_err_t lib_rmtree(const char *dir);
bool lib_exists(const char *path);

/** "<base>/lib" ... into out (64 bytes). */
bool lib_path(char *out, size_t cap, const char *base, const char *leaf); /**< false if it did not fit (out is then unusable) */

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

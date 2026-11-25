// SPDX-License-Identifier: MPL-2.0
// Manifest encoding and the swap, shared by lib_build.c and lib_store.c.
#ifndef NEUPOD_LIB_STORE_H
#define NEUPOD_LIB_STORE_H

#include "lib_internal.h"

// payload: generation, decoder, fingerprint(2), tracks, playable, artists, albums, genres, dirs, comp_first, links, genre_tracks, [crc,size]*13, jumps
#define MF_WORDS (2 + 2 + 9 + 2 * LIB_FILES + LIB_LISTS * LIB_BUCKETS)
#define MF_BYTES (MF_WORDS * 4u)

/** The working memory of lib_manifest_load (~2.2 KB: a FIL, a FILINFO, the file image, a decoded manifest). The build lends one from its arena. */
typedef struct {
    char path[96];
    union {
        uint8_t buf[LIB_HDR + MF_BYTES + 1]; /* manifest.bin plus one byte, to see a file that is too long */
        uint8_t blk[512];                    /* CRC pass over a table (full check) */
    };
    FIL f;
    FILINFO fi;
    lib_manifest_t m;
} lib_mfscratch_t;

/** lib_manifest_load() with caller-supplied working memory (see lib_mfscratch_t). */
lib_err_t lib_manifest_load_with(lib_mfscratch_t *s, const char *dir, bool full, lib_manifest_t *out);

/** Size of manifest.bin (header + payload). */
size_t lib_manifest_bytes(void);
/** Serialise @p m, header included, into @p out (lib_manifest_bytes() bytes). */
void lib_manifest_encode(const lib_manifest_t *m, uint8_t *out);
/**
 * The swap: delete a leftover lib.old, lib -> lib.old (if lib exists), lib.tmp -> lib, delete lib.old. @p stop_step (tests): return
 * LIB_E_TEST_STOP after that step (3, 4 or 5; test builds only). *step_done tells how far it got.
 */
lib_err_t lib_swap_install(const char *base, int stop_step, int *step_done);

/** lib_recover() for a caller that has the write scope open already (lib_build). */
lib_err_t lib_recover_locked(const char *base, lib_mfscratch_t *s);

#endif

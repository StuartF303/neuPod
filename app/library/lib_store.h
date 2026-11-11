// SPDX-License-Identifier: MPL-2.0
// Manifest encoding and the swap, shared by lib_build.c and lib_store.c.
#ifndef NEUPOD_LIB_STORE_H
#define NEUPOD_LIB_STORE_H

#include "lib_internal.h"

/** Size of manifest.bin (header + payload). */
size_t lib_manifest_bytes(void);
/** Serialise @p m, header included, into @p out (lib_manifest_bytes() bytes). */
void lib_manifest_encode(const lib_manifest_t *m, uint8_t *out);
/**
 * The swap: delete a leftover lib.old, lib -> lib.old (if lib exists), lib.tmp -> lib, delete lib.old. @p stop_step (tests): return
 * LIB_E_TEST_STOP after that step (3, 4 or 5; test builds only). *step_done tells how far it got.
 */
lib_err_t lib_swap_install(const char *base, int stop_step, int *step_done);

#endif

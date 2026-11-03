// SPDX-License-Identifier: MPL-2.0
/* Replaces the FatFs ffsystem.c (see app/include/ff_alloc.h for why). Only FF_FS_REENTRANT == 0 is supported:
 * the stock file's mutex functions are not needed, and the storage task is the only FatFs user. */
#include <stdlib.h>

#include "ff.h"
#include "ff_alloc.h"

#if FF_FS_REENTRANT
#error "ff_alloc.c does not provide ff_mutex_*; use the library's ffsystem.c when enabling FF_FS_REENTRANT"
#endif

#ifdef PICO_ON_DEVICE
/* The SDK links malloc through --wrap, and the wrapper panics on exhaustion; the real allocator returns NULL. */
extern void *__real_malloc(size_t n);
#define RAW_MALLOC __real_malloc
#else
#define RAW_MALLOC malloc
#endif

#if FF_USE_LFN == 3
void *ff_memalloc(UINT msize) {
    if (msize == 0u || msize > NEUPOD_FF_ALLOC_MAX_BYTES) {
        return NULL;
    }
    return RAW_MALLOC((size_t)msize);
}

void ff_memfree(void *mblock) { free(mblock); }
#endif

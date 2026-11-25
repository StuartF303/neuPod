// SPDX-License-Identifier: MPL-2.0
/* Replaces the FatFs ffsystem.c (see ff_alloc.h for why). Scratch comes from os_alloc() (the kernel heap, NULL on failure).
 * Only FF_FS_REENTRANT == 0 is supported:
 * the stock file's mutex functions are not needed, and the storage task is the only FatFs user. */
#include "ff.h"
#include "ff_alloc.h"
#include "os.h"

#if FF_FS_REENTRANT
#error "ff_alloc.c does not provide ff_mutex_*; use the library's ffsystem.c when enabling FF_FS_REENTRANT"
#endif

#if FF_USE_LFN == 3
void *ff_memalloc(UINT msize) {
    if (msize == 0u || msize > NEUPOD_FF_ALLOC_MAX_BYTES) {
        return NULL;
    }
    return os_alloc((size_t)msize);
}

void ff_memfree(void *mblock) { os_free(mblock); }
#endif

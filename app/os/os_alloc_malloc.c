// SPDX-License-Identifier: MPL-2.0
/* os_alloc()/os_free() for binaries without the RTOS: the host unit tests and the RP2350 storage_test bench tool.
 * The real ones (FreeRTOS heap) are in os_freertos.c; a binary links one or the other, never both. malloc() is fine
 * here: hosted tests have plenty of memory, and the bench tool has no budget to protect. Adds fault injection. */
#include "os_alloc_malloc.h"

#include <stdlib.h>

#include "os.h"

#ifdef NEUPOD_OS_ALLOC_REAL_MALLOC
/* Device build without the kernel (storage_test): the SDK links malloc through --wrap and the wrapper panics on exhaustion;
 * the real allocator returns NULL, which is what FatFs relies on. Set by platforms/rp2350/CMakeLists.txt. */
extern void *__real_malloc(size_t n);
#define RAW_MALLOC __real_malloc
#else
#define RAW_MALLOC malloc
#endif

static int s_fail_from = -1;
static int s_calls;
static int s_live;

void os_alloc_fail_from(int n) {
    s_fail_from = n < 0 ? -1 : s_calls + n;
}
int os_alloc_calls(void) { return s_calls; }
int os_alloc_live(void) { return s_live; }

void *os_alloc(size_t size) {
    int idx = s_calls++;
    if (size == 0u || (s_fail_from >= 0 && idx >= s_fail_from)) {
        return NULL;
    }
    void *p = RAW_MALLOC(size);
    if (p != NULL) {
        s_live++;
    }
    return p;
}

void os_free(void *p) {
    if (p != NULL) {
        s_live--;
        free(p);
    }
}

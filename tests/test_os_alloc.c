// SPDX-License-Identifier: MPL-2.0
// The real os_alloc()/os_free() (app/os/os_freertos.c) on the FreeRTOS POSIX port with heap_4: exhaustion returns NULL without
// reaching the panic path, the NEUPOD_OS_ALLOC_RESERVE is honoured, and a plain pvPortMalloc failure still reaches the hook.
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "memory_budget.h"
#include "os.h"
#include "panic.h"
#include "unit.h"

static int s_panics;
static jmp_buf s_jb;
static int s_jb_armed;

// Stand-in for the real panic: counts, and (when a test armed it) jumps out instead of never returning.
void neupod_panic(neupod_panic_code_t code, const char *detail) {
    (void)code;
    (void)detail;
    s_panics++;
    if (s_jb_armed) {
        s_jb_armed = 0;
        longjmp(s_jb, 1);
    }
    printf("unexpected panic\n");
    _Exit(3);
}
void neupod_panic_assert(const char *file, int line) {
    (void)file;
    (void)line;
    neupod_panic(NEUPOD_PANIC_MALLOC_FAILED, "assert");
}

#define MAX_BLOCKS 512
static void *s_blocks[MAX_BLOCKS];

static void free_all(int n) {
    for (int i = 0; i < n; i++) {
        os_free(s_blocks[i]);
        s_blocks[i] = NULL;
    }
}

TEST(test_exhaustion_returns_null_without_panic) {
    int n = 0;
    void *p;
    while (n < MAX_BLOCKS && (p = os_alloc(16u * 1024u)) != NULL) {
        s_blocks[n++] = p;
    }
    ASSERT_TRUE(n > 0 && n < MAX_BLOCKS); // the heap really ran out
    ASSERT_TRUE(os_alloc(16u * 1024u) == NULL);
    ASSERT_TRUE(os_alloc(0u) == NULL);
    ASSERT_TRUE(os_alloc((size_t)-1) == NULL); // absurd size: NULL, not an overflow
    ASSERT_EQ(0, s_panics);
    ASSERT_TRUE(xPortGetFreeHeapSize() >= NEUPOD_OS_ALLOC_RESERVE); // the reserve was left alone
    free_all(n);
    void *q = os_alloc(1024u); // and the heap is usable again
    ASSERT_TRUE(q != NULL);
    os_free(q);
    os_free(NULL);
}

TEST(test_reserve_is_honoured) {
    size_t free_now = xPortGetFreeHeapSize();
    ASSERT_TRUE(free_now > NEUPOD_OS_ALLOC_RESERVE + 4096u);
    // Heap_4 adds a block header and alignment, so keep a margin of 64 B around the boundary.
    size_t room = free_now - NEUPOD_OS_ALLOC_RESERVE;
    void *over = os_alloc(room + 64u); // would leave less than the reserve
    ASSERT_TRUE(over == NULL);
    void *ok = os_alloc(room - 64u - 64u); // leaves a little more than the reserve
    ASSERT_TRUE(ok != NULL);
    if (ok != NULL) {
        ASSERT_TRUE(xPortGetFreeHeapSize() >= NEUPOD_OS_ALLOC_RESERVE);
        ASSERT_TRUE(os_alloc(256u) == NULL); // now inside the reserve: refused
        os_free(ok);
    }
    ASSERT_TRUE(xPortGetFreeHeapSize() == free_now);
    ASSERT_EQ(0, s_panics);
}

TEST(test_plain_malloc_failure_still_panics) {
    s_panics = 0;
    s_jb_armed = 1;
    if (setjmp(s_jb) == 0) {
        void *p = pvPortMalloc((size_t)configTOTAL_HEAP_SIZE); // cannot fit: heap_4 calls vApplicationMallocFailedHook
        (void)p;
        s_jb_armed = 0;
    }
    ASSERT_EQ(1, s_panics); // the hook was not left standing down after os_alloc
}

int main(void) {
    // heap_4 sets itself up on the first allocation (until then it reports 0 bytes free); the firmware creates tasks first.
    vPortFree(pvPortMalloc(16u));
    RUN(test_exhaustion_returns_null_without_panic);
    RUN(test_reserve_is_honoured);
    RUN(test_plain_malloc_failure_still_panics);
    return RESULT();
}

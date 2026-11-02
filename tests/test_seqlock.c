// SPDX-License-Identifier: MPL-2.0
// Seqlock: a writer publishes {n,n,n,n} while a reader copies; every successful read must be consistent.
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "seqlock.h"
#include "unit.h"

typedef struct {
    volatile uint32_t a, b, c, d;
} payload_t;

static seqlock_t g_lock;
static payload_t g_data;
static volatile int g_done;

static void *writer(void *arg) {
    (void)arg;
    for (uint32_t n = 1; n <= 3000000u; n++) {
        seqlock_write_begin(&g_lock);
        g_data.a = g_data.b = g_data.c = g_data.d = n;
        seqlock_write_end(&g_lock);
    }
    g_done = 1;
    return NULL;
}

static uint32_t g_reads, g_torn;

static void *reader(void *arg) {
    (void)arg;
    while (!g_done) {
        payload_t copy;
        uint32_t q;
        do {
            q = seqlock_read_begin(&g_lock);
            copy.a = g_data.a;
            copy.b = g_data.b;
            copy.c = g_data.c;
            copy.d = g_data.d;
        } while (seqlock_read_retry(&g_lock, q));
        g_reads++;
        if (copy.a != copy.b || copy.b != copy.c || copy.c != copy.d) {
            g_torn++;
        }
    }
    return NULL;
}

TEST(never_torn) {
    pthread_t w, r;
    pthread_create(&r, NULL, reader, NULL);
    pthread_create(&w, NULL, writer, NULL);
    pthread_join(w, NULL);
    pthread_join(r, NULL);
    ASSERT_TRUE(g_reads > 0);
    ASSERT_EQ(0, g_torn);
}

TEST(try_begin_refuses_during_write) {
    seqlock_t l = {0};
    uint32_t q;
    ASSERT_TRUE(seqlock_read_try_begin(&l, &q));
    seqlock_write_begin(&l);
    ASSERT_FALSE(seqlock_read_try_begin(&l, &q)); // odd: write in progress
    seqlock_write_end(&l);
    ASSERT_TRUE(seqlock_read_try_begin(&l, &q));
}

int main(void) {
    RUN(never_torn);
    RUN(try_begin_refuses_during_write);
    return RESULT();
}

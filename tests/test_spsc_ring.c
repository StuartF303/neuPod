// SPDX-License-Identifier: MPL-2.0
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "spsc_ring.h"
#include "unit.h"

TEST(init_rejects_bad_args) {
    spsc_ring_t r;
    uint8_t buf[64];
    ASSERT_FALSE(spsc_ring_init(&r, buf, 4, 6)); // not a power of two
    ASSERT_FALSE(spsc_ring_init(&r, buf, 0, 8));
    ASSERT_FALSE(spsc_ring_init(&r, NULL, 4, 8));
    ASSERT_FALSE(spsc_ring_init(&r, buf, 4, 1));
    ASSERT_TRUE(spsc_ring_init(&r, buf, 4, 8));
}

TEST(empty_and_full) {
    spsc_ring_t r;
    uint32_t buf[8], in[16], out[16];
    ASSERT_TRUE(spsc_ring_init(&r, buf, 4, 8));
    ASSERT_EQ(0, spsc_ring_available(&r));
    ASSERT_EQ(8, spsc_ring_space(&r));
    ASSERT_EQ(0, spsc_ring_read(&r, out, 4));
    for (uint32_t i = 0; i < 16; i++) {
        in[i] = i + 1;
    }
    ASSERT_EQ(8, spsc_ring_write(&r, in, 16)); // only 8 fit
    ASSERT_EQ(0, spsc_ring_space(&r));
    ASSERT_EQ(0, spsc_ring_write(&r, in, 1));
    ASSERT_EQ(8, spsc_ring_read(&r, out, 16));
    for (uint32_t i = 0; i < 8; i++) {
        ASSERT_EQ(i + 1, out[i]);
    }
    ASSERT_EQ(0, spsc_ring_available(&r));
}

TEST(wraparound_preserves_order) {
    spsc_ring_t r;
    uint32_t buf[8], in[5], out[5];
    ASSERT_TRUE(spsc_ring_init(&r, buf, 4, 8));
    uint32_t next = 0, expect = 0;
    for (int round = 0; round < 50; round++) { // 5-frame chunks across an 8-frame ring
        for (int i = 0; i < 5; i++) {
            in[i] = next++;
        }
        ASSERT_EQ(5, spsc_ring_write(&r, in, 5));
        ASSERT_EQ(5, spsc_ring_read(&r, out, 5));
        for (int i = 0; i < 5; i++) {
            ASSERT_EQ(expect++, out[i]);
        }
    }
}

TEST(null_source_writes_silence) {
    spsc_ring_t r;
    uint32_t buf[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}, out[4];
    ASSERT_TRUE(spsc_ring_init(&r, buf, 4, 4));
    ASSERT_EQ(3, spsc_ring_write(&r, NULL, 3));
    ASSERT_EQ(3, spsc_ring_read(&r, out, 3));
    ASSERT_EQ(0, out[0] | out[1] | out[2]);
}

#define TOTAL 2000000u
static spsc_ring_t g_ring;
static uint32_t g_store[256];
static uint32_t g_bad;

static void *producer(void *arg) {
    (void)arg;
    uint32_t next = 0, chunk[37];
    while (next < TOTAL) {
        uint32_t n = TOTAL - next < 37 ? TOTAL - next : 37;
        if (spsc_ring_space(&g_ring) < n) {
            continue; // full: wait for the consumer
        }
        for (uint32_t i = 0; i < n; i++) {
            chunk[i] = next + i;
        }
        next += spsc_ring_write(&g_ring, chunk, n);
    }
    return NULL;
}

static void *consumer(void *arg) {
    (void)arg;
    uint32_t expect = 0, chunk[53];
    while (expect < TOTAL) {
        uint32_t n = spsc_ring_read(&g_ring, chunk, 53);
        for (uint32_t i = 0; i < n; i++) {
            if (chunk[i] != expect++) {
                g_bad++;
            }
        }
    }
    return NULL;
}

TEST(concurrent_producer_consumer) {
    ASSERT_TRUE(spsc_ring_init(&g_ring, g_store, 4, 256));
    pthread_t p, c;
    pthread_create(&p, NULL, producer, NULL);
    pthread_create(&c, NULL, consumer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    ASSERT_EQ(0, g_bad); // every frame arrived once, in order
    ASSERT_EQ(0, spsc_ring_available(&g_ring));
}

int main(void) {
    RUN(init_rejects_bad_args);
    RUN(empty_and_full);
    RUN(wraparound_preserves_order);
    RUN(null_source_writes_silence);
    RUN(concurrent_producer_consumer);
    return RESULT();
}

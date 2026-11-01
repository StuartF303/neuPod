// SPDX-License-Identifier: MPL-2.0
#include "clickwheel_frame.h"
#include "unit.h"

#define GOOD 0x8000001Au

TEST(test_init_packet_constant) { ASSERT_EQ(0x8000023Au, CLICKWHEEL_INIT_PACKET); }

TEST(test_valid_magic_accepted) {
    hal_wheel_state st;
    ASSERT_TRUE(clickwheel_decode(GOOD, &st));
    ASSERT_TRUE(st.valid);
    ASSERT_EQ(0, st.buttons);
    ASSERT_FALSE(st.touched);
}

TEST(test_bad_low_byte_rejected) {
    for (unsigned bit = 0; bit < 8; bit++) {
        hal_wheel_state st;
        ASSERT_FALSE(clickwheel_decode(GOOD ^ (1u << bit), &st));
        ASSERT_FALSE(st.valid);
    }
}

TEST(test_bit31_rejected) {
    hal_wheel_state st;
    ASSERT_FALSE(clickwheel_decode(GOOD & ~0x80000000u, &st));
    ASSERT_FALSE(st.valid);
    ASSERT_FALSE(clickwheel_decode(0, NULL));
}

TEST(test_buttons) {
    static const uint8_t k_map[5] = {HAL_BTN_SELECT, HAL_BTN_RIGHT, HAL_BTN_LEFT, HAL_BTN_PLAY,
                                     HAL_BTN_MENU};
    for (unsigned i = 0; i < 5; i++) {
        hal_wheel_state st;
        ASSERT_TRUE(clickwheel_decode(GOOD | (1u << (8 + i)), &st));
        ASSERT_EQ(k_map[i], st.buttons);
    }
    hal_wheel_state st;
    ASSERT_TRUE(clickwheel_decode(GOOD | (0x1Fu << 8), &st));
    ASSERT_EQ(0x1F, st.buttons);
    // Bits 13..15 are not buttons.
    ASSERT_TRUE(clickwheel_decode(GOOD | (7u << 13), &st));
    ASSERT_EQ(0, st.buttons);
}

TEST(test_position_and_touch) {
    hal_wheel_state st;
    ASSERT_TRUE(clickwheel_decode(GOOD | CLICKWHEEL_TOUCH_BIT, &st));
    ASSERT_TRUE(st.touched);
    ASSERT_EQ(0, st.position);

    ASSERT_TRUE(clickwheel_decode(GOOD | CLICKWHEEL_TOUCH_BIT | (95u << 16), &st));
    ASSERT_EQ(95, st.position);

    // Bits outside 16..22 (15, 23..29, 31 already set) must not leak into the position.
    uint32_t noise = (1u << 15) | (0x7Fu << 23);
    ASSERT_TRUE(clickwheel_decode(GOOD | CLICKWHEEL_TOUCH_BIT | (42u << 16) | noise, &st));
    ASSERT_EQ(42, st.position);

    // Untouched: position field is not meaningful, reported as 0.
    ASSERT_TRUE(clickwheel_decode(GOOD | (42u << 16), &st));
    ASSERT_FALSE(st.touched);
    ASSERT_EQ(0, st.position);
}

TEST(test_position_out_of_range_rejected) {
    hal_wheel_state st;
    ASSERT_FALSE(clickwheel_decode(GOOD | CLICKWHEEL_TOUCH_BIT | (96u << 16), &st));
    ASSERT_FALSE(st.valid);
    ASSERT_FALSE(clickwheel_decode(GOOD | CLICKWHEEL_TOUCH_BIT | (127u << 16), &st));
    // Same field value without touch is not an error.
    ASSERT_TRUE(clickwheel_decode(GOOD | (96u << 16), &st));
}

TEST(test_encode_roundtrip) {
    for (unsigned pos = 0; pos < HAL_WHEEL_POSITIONS; pos++) {
        hal_wheel_state in = {.buttons = (uint8_t)(pos & 0x1F), .position = (uint8_t)pos,
                              .touched = true, .valid = true};
        hal_wheel_state out;
        ASSERT_TRUE(clickwheel_decode(clickwheel_encode(&in), &out));
        ASSERT_EQ(in.buttons, out.buttons);
        ASSERT_EQ(in.position, out.position);
        ASSERT_TRUE(out.touched);
    }
}

// Append a 32-bit frame (LSB first) to a bit array.
static size_t put_frame(uint8_t *bits, size_t n, uint32_t f) {
    for (unsigned i = 0; i < 32; i++) {
        bits[n++] = (f >> i) & 1u;
    }
    return n;
}

TEST(test_resync_shifted_stream) {
    for (unsigned shift = 0; shift < 32; shift++) {
        uint8_t bits[32 * 12 + 32];
        size_t n = 0;
        for (unsigned i = 0; i < shift; i++) {
            bits[n++] = 1; // arbitrary leading junk: stream joined mid-frame
        }
        const unsigned frames = 8;
        for (unsigned f = 0; f < frames; f++) {
            hal_wheel_state s = {.buttons = (uint8_t)(f & 0x1F), .position = (uint8_t)(f * 11),
                                 .touched = true};
            n = put_frame(bits, n, clickwheel_encode(&s));
        }

        clickwheel_sync sync;
        clickwheel_sync_init(&sync);
        unsigned got = 0;
        hal_wheel_state last = {0};
        for (size_t i = 0; i < n; i++) {
            hal_wheel_state st;
            if (clickwheel_sync_push_bit(&sync, bits[i], &st)) {
                // The first frame only confirms alignment; reporting starts at frame 1.
                ASSERT_EQ((got + 1) & 0x1F, st.buttons); // in order, none skipped
                ASSERT_EQ((got + 1) * 11, st.position);
                got++;
                last = st;
            }
        }
        ASSERT_EQ(frames - 1, got);
        ASSERT_EQ(CLICKWHEEL_SYNC_LOCKED, sync.state);
        ASSERT_EQ(7, last.buttons);
    }
}

TEST(test_resync_words) {
    // Same stream, delivered as PIO-style 32-bit words that are not frame aligned.
    const unsigned shift = 13, frames = 6;
    uint8_t bits[32 * 8];
    size_t n = 0;
    for (unsigned i = 0; i < shift; i++) {
        bits[n++] = 0;
    }
    for (unsigned f = 0; f < frames; f++) {
        hal_wheel_state s = {.buttons = HAL_BTN_PLAY, .position = (uint8_t)(f + 1), .touched = true};
        n = put_frame(bits, n, clickwheel_encode(&s));
    }
    while (n % 32) {
        bits[n++] = 0;
    }
    clickwheel_sync sync;
    clickwheel_sync_init(&sync);
    unsigned total = 0;
    hal_wheel_state st = {0};
    for (size_t w = 0; w < n / 32; w++) {
        uint32_t word = 0;
        for (unsigned i = 0; i < 32; i++) {
            word |= (uint32_t)bits[w * 32 + i] << i;
        }
        total += clickwheel_sync_push_word(&sync, word, &st);
    }
    ASSERT_EQ(frames - 1, total);
    ASSERT_EQ(frames, st.position);
    ASSERT_EQ(HAL_BTN_PLAY, st.buttons);
}

TEST(test_resync_loses_and_recovers) {
    clickwheel_sync sync;
    clickwheel_sync_init(&sync);
    hal_wheel_state s = {.position = 5, .touched = true}, st;
    unsigned got = 0;
    for (unsigned f = 0; f < 3; f++) {
        got += clickwheel_sync_push_word(&sync, clickwheel_encode(&s), &st);
    }
    ASSERT_EQ(2, got);
    // Garbage drops lock...
    for (unsigned f = 0; f < 2; f++) {
        ASSERT_EQ(0, clickwheel_sync_push_word(&sync, 0, &st));
    }
    ASSERT_EQ(CLICKWHEEL_SYNC_SEARCH, sync.state);
    // ...and a clean stream is found again.
    got = 0;
    for (unsigned f = 0; f < 3; f++) {
        got += clickwheel_sync_push_word(&sync, clickwheel_encode(&s), &st);
    }
    ASSERT_EQ(2, got);
}

// Deterministic junk that contains no valid frame at any alignment (checked by the test).
static uint32_t lcg(uint32_t *x) {
    *x = *x * 1664525u + 1013904223u;
    return *x >> 8;
}

TEST(test_resync_junk_only_yields_nothing) {
    clickwheel_sync sync;
    clickwheel_sync_init(&sync);
    uint32_t seed = 1;
    unsigned got = 0;
    // Every bit, so chance single matches occur (~1/512) but must never be reported.
    for (unsigned i = 0; i < 200000; i++) {
        got += clickwheel_sync_push_bit(&sync, lcg(&seed) & 1u, NULL);
    }
    ASSERT_EQ(0, got);
}

TEST(test_resync_junk_then_stream) {
    clickwheel_sync sync;
    clickwheel_sync_init(&sync);
    uint32_t seed = 7;
    unsigned got = 0;
    for (unsigned i = 0; i < 5000; i++) {
        got += clickwheel_sync_push_bit(&sync, lcg(&seed) & 1u, NULL);
    }
    ASSERT_EQ(0, got);
    hal_wheel_state s = {.buttons = HAL_BTN_MENU, .position = 33, .touched = true}, st = {0};
    for (unsigned f = 0; f < 6; f++) {
        got += clickwheel_sync_push_word(&sync, clickwheel_encode(&s), &st);
    }
    // Junk leaves the bit phase arbitrary, so a few frames may be spent locking on.
    ASSERT_TRUE(got >= 4);
    ASSERT_TRUE(got <= 5);
    ASSERT_EQ(33, st.position);
    ASSERT_EQ(HAL_BTN_MENU, st.buttons);
}

TEST(test_resync_corrupt_frame_relocks) {
    clickwheel_sync sync;
    clickwheel_sync_init(&sync);
    hal_wheel_state s = {.position = 9, .touched = true}, st;
    unsigned got = 0;
    for (unsigned f = 0; f < 4; f++) {
        got += clickwheel_sync_push_word(&sync, clickwheel_encode(&s), &st);
    }
    ASSERT_EQ(3, got);
    ASSERT_EQ(0, clickwheel_sync_push_word(&sync, clickwheel_encode(&s) ^ 1u, &st)); // bad magic
    got = 0;
    for (unsigned f = 0; f < 4; f++) {
        got += clickwheel_sync_push_word(&sync, clickwheel_encode(&s), &st);
    }
    ASSERT_EQ(3, got);
    ASSERT_EQ(9, st.position);
}

TEST(test_push_word_null_out) {
    clickwheel_sync sync;
    clickwheel_sync_init(&sync);
    hal_wheel_state s = {.position = 1, .touched = true};
    unsigned got = 0;
    for (unsigned f = 0; f < 3; f++) {
        got += clickwheel_sync_push_word(&sync, clickwheel_encode(&s), NULL);
    }
    ASSERT_EQ(2, got);
}

int main(void) {
    RUN(test_init_packet_constant);
    RUN(test_valid_magic_accepted);
    RUN(test_bad_low_byte_rejected);
    RUN(test_bit31_rejected);
    RUN(test_buttons);
    RUN(test_position_and_touch);
    RUN(test_position_out_of_range_rejected);
    RUN(test_encode_roundtrip);
    RUN(test_resync_shifted_stream);
    RUN(test_resync_words);
    RUN(test_resync_loses_and_recovers);
    RUN(test_resync_junk_only_yields_nothing);
    RUN(test_resync_junk_then_stream);
    RUN(test_resync_corrupt_frame_relocks);
    RUN(test_push_word_null_out);
    return RESULT();
}

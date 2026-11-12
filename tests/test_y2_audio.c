// SPDX-License-Identifier: MPL-2.0
// Y2 wired output (platforms/y2/hal/y2_audio.c) against a fake device layer: the safety allow-lists, the exact bring-up and
// teardown sequences, the S16 conversion with dither, the ring, and the state machine (start on the first audible block, silence
// then standby, idle then DAC off, unplug, faults, shutdown, the Bluetooth virtual clock).
#include <stdlib.h>
#include <string.h>

#include "unit.h"
#include "y2_audio.h"

// ---- the fake device ----

#define LOG_MAX 512
typedef struct {
    char kind;           // 'D' dac ioctl, 'E' eac ioctl, 'R' register write, 'W' write
    unsigned long cmd;   // ioctl number, or the register offset for 'R'
    unsigned long arg;   // value (register: value), or bytes for 'W'
    unsigned long mask;  // register mask
} entry_t;

static entry_t g_log[LOG_MAX];
static int g_n;
static uint64_t g_now_us = 1000000;
static int g_h2w = 2;
static unsigned long g_fail_cmd; // an ioctl number that fails (0 = none)
static int g_fail_write;
static uint8_t g_written[1 << 20];
static size_t g_written_bytes;
static void (*g_on_sleep)(void); // runs inside sleep_ms: lets a test interleave the producer
static y2_audio_t *g_a;

static void note(char k, unsigned long cmd, unsigned long arg, unsigned long mask) {
    if (g_n < LOG_MAX) {
        g_log[g_n++] = (entry_t){k, cmd, arg, mask};
    }
}

static int f_dac(void *c, unsigned long cmd, unsigned long arg) {
    (void)c;
    note('D', cmd, arg, 0);
    return cmd == g_fail_cmd ? -1 : 0;
}

static int f_eac(void *c, unsigned long cmd, unsigned long arg) {
    (void)c;
    if (cmd == 0xc0044300ul) {
        const y2_reg_ctl_t *r = (const y2_reg_ctl_t *)(uintptr_t)arg;
        note('R', r->offset, r->value, r->mask);
        return 0;
    }
    note('E', cmd, arg, 0);
    return cmd == g_fail_cmd ? -1 : 0;
}

static long f_write(void *c, const void *buf, size_t bytes) {
    (void)c;
    if (g_fail_write) {
        return -1;
    }
    note('W', 0, bytes, 0);
    if (g_written_bytes + bytes <= sizeof g_written) {
        memcpy(g_written + g_written_bytes, buf, bytes);
        g_written_bytes += bytes;
    }
    g_now_us += (uint64_t)bytes * 1000000u / (44100u * 4u); // the DMA paces the write
    return (long)bytes;
}

static int f_hp(void *c) {
    (void)c;
    return g_h2w;
}

static uint64_t f_now(void *c) {
    (void)c;
    return g_now_us;
}

static void f_sleep(void *c, uint32_t ms) {
    (void)c;
    g_now_us += (uint64_t)ms * 1000u;
    if (g_on_sleep != NULL) {
        void (*f)(void) = g_on_sleep;
        g_on_sleep = NULL;
        f();
    }
}

static y2_audio_t g_dev; // static: it is big

static void setup(int h2w, const char *env) {
    g_n = 0;
    g_now_us = 1000000;
    g_h2w = h2w;
    g_fail_cmd = 0;
    g_fail_write = 0;
    g_written_bytes = 0;
    g_on_sleep = NULL;
    y2_audio_ops_t ops = {.dac_ioctl = f_dac, .eac_ioctl = f_eac, .eac_write = f_write, .headphones = f_hp, .now_us = f_now, .sleep_ms = f_sleep, .log = NULL, .ctx = NULL};
    y2_audio_init(&g_dev, &ops, env);
    g_a = &g_dev;
    y2_audio_set_open(&g_dev, true);
}

static int count(char kind, unsigned long cmd) {
    int n = 0;
    for (int i = 0; i < g_n; i++) {
        if (g_log[i].kind == kind && g_log[i].cmd == cmd) {
            n++;
        }
    }
    return n;
}

static int find(char kind, unsigned long cmd, unsigned long arg, int from) {
    for (int i = from; i < g_n; i++) {
        if (g_log[i].kind == kind && g_log[i].cmd == cmd && g_log[i].arg == arg) {
            return i;
        }
    }
    return -1;
}

static int32_t g_block[Y2_AUDIO_CHUNK_FRAMES * 2];

static void fill_block(int32_t left, int32_t right) {
    for (uint32_t i = 0; i < Y2_AUDIO_CHUNK_FRAMES; i++) {
        g_block[2u * i] = left;
        g_block[2u * i + 1u] = right;
    }
}

static uint32_t produce(int32_t l, int32_t r) {
    fill_block(l, r);
    return y2_audio_produce(&g_dev, g_block, Y2_AUDIO_CHUNK_FRAMES);
}

static void advance_ms(uint32_t ms) { g_now_us += (uint64_t)ms * 1000u; }

// Run the worker until it has nothing to do, or has just brought the stream up (a streaming worker always has a chunk to write).
static void settle(void) {
    for (int i = 0; i < 64 && y2_audio_work(&g_dev) && y2_audio_state(&g_dev) != Y2_DAC_STREAMING; i++) {
    }
}

// n passes of the worker (each writes one chunk while streaming).
static void pump(int n) {
    for (int i = 0; i < n; i++) {
        (void)y2_audio_work(&g_dev);
    }
}

static void deliver_audible_block(void) { (void)produce((int32_t)0x50000000, (int32_t)0x50000000); }

static int first_register_write(unsigned long offset) {
    for (int i = 0; i < g_n; i++) {
        if (g_log[i].kind == 'R' && g_log[i].cmd == offset) {
            return i;
        }
    }
    return -1;
}

// ---- allow-lists ----

TEST(only_the_whitelisted_afe_offsets_are_allowed) {
    const uint32_t ok[] = {0x14, 0x20, 0x4c, 0x3a0, 0x3ac, 0x4c4, 0x10};
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        ASSERT_TRUE(y2_audio_afe_offset_ok(ok[i]));
    }
    // neighbours and the AFE registers the other paths use (the internal DAC, the line-in, the 2nd I2S input) are refused
    const uint32_t bad[] = {0x0, 0x4, 0x8, 0xc, 0x18, 0x1c, 0x24, 0x28, 0x48, 0x50, 0x3a4, 0x3a8, 0x3b0, 0x4c0, 0x4c8, 0x573, 0xFFFFFFFF};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ASSERT_FALSE(y2_audio_afe_offset_ok(bad[i]));
    }
}

TEST(forbidden_device_commands_are_refused) {
    // PMIC (SET_ANAAFE_REG / GET), the speaker amplifier ioctls, the CS43131 mono path, hardware init and the frequency command
    ASSERT_FALSE(y2_audio_eac_cmd_ok(0xc0044302ul));
    ASSERT_FALSE(y2_audio_eac_cmd_ok(0xc0044303ul));
    ASSERT_FALSE(y2_audio_eac_cmd_ok(0xc0044301ul)); // read: not needed
    for (unsigned long c = 0x400443a0ul; c <= 0x400443a7ul; c++) {
        ASSERT_FALSE(y2_audio_eac_cmd_ok(c));
        ASSERT_FALSE(y2_audio_dac_cmd_ok(c));
    }
    ASSERT_FALSE(y2_audio_dac_cmd_ok(0x4306ul));
    ASSERT_FALSE(y2_audio_dac_cmd_ok(0x4301ul));
    ASSERT_FALSE(y2_audio_dac_cmd_ok(0x40044305ul));
    ASSERT_FALSE(y2_audio_dac_cmd_ok(0xc0044302ul));
    // and what is allowed
    ASSERT_TRUE(y2_audio_dac_cmd_ok(0x4302ul));
    ASSERT_TRUE(y2_audio_dac_cmd_ok(0x4303ul));
    ASSERT_TRUE(y2_audio_dac_cmd_ok(0x4307ul));
    ASSERT_TRUE(y2_audio_dac_cmd_ok(0x40044304ul));
    ASSERT_TRUE(y2_audio_eac_cmd_ok(0xc0044300ul));
    ASSERT_TRUE(y2_audio_eac_cmd_ok(0xc0044310ul));
    ASSERT_TRUE(y2_audio_eac_cmd_ok(0xc0044320ul));
    ASSERT_TRUE(y2_audio_eac_cmd_ok(0xc0044355ul));
}

TEST(a_bad_command_never_reaches_the_device) {
    setup(2, NULL);
    // drive the guarded paths with a device that only counts: a full run must never be refused, and nothing outside the lists shows up
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(0, g_dev.stats.refused);
    for (int i = 0; i < g_n; i++) {
        if (g_log[i].kind == 'D') {
            ASSERT_TRUE(y2_audio_dac_cmd_ok(g_log[i].cmd));
        } else if (g_log[i].kind == 'E') {
            ASSERT_TRUE(y2_audio_eac_cmd_ok(g_log[i].cmd));
        } else if (g_log[i].kind == 'R') {
            ASSERT_TRUE(y2_audio_afe_offset_ok((uint32_t)g_log[i].cmd));
        }
    }
}

// ---- small pure pieces ----

TEST(dac_index_default_env_and_clamp) {
    ASSERT_EQ(11, y2_audio_dac_index_from_env(NULL));
    ASSERT_EQ(11, y2_audio_dac_index_from_env(""));
    ASSERT_EQ(11, y2_audio_dac_index_from_env("loud"));
    ASSERT_EQ(11, y2_audio_dac_index_from_env("5x"));
    ASSERT_EQ(5, y2_audio_dac_index_from_env("5"));
    ASSERT_EQ(0, y2_audio_dac_index_from_env("0"));
    ASSERT_EQ(0, y2_audio_dac_index_from_env("-3"));
    ASSERT_EQ(11, y2_audio_dac_index_from_env("12"));  // clamped: never louder than index 11
    ASSERT_EQ(11, y2_audio_dac_index_from_env("15"));
    ASSERT_EQ(11, y2_audio_dac_index_from_env("99999999999"));
    ASSERT_EQ(0x29, y2_audio_dac_volume_reg(11));
    ASSERT_EQ(0x00, y2_audio_dac_volume_reg(0));
    ASSERT_EQ(0x06, y2_audio_dac_volume_reg(15));
}

TEST(h2w_parsing) {
    ASSERT_EQ(2, y2_audio_h2w_parse("2\n"));
    ASSERT_EQ(0, y2_audio_h2w_parse("0"));
    ASSERT_EQ(-1, y2_audio_h2w_parse(""));
    ASSERT_EQ(-1, y2_audio_h2w_parse("x"));
    ASSERT_EQ(-1, y2_audio_h2w_parse(NULL));
    ASSERT_TRUE(y2_audio_h2w_headphones(2));
    ASSERT_TRUE(y2_audio_h2w_headphones(1));
    ASSERT_FALSE(y2_audio_h2w_headphones(0));
    ASSERT_FALSE(y2_audio_h2w_headphones(-1));
    ASSERT_FALSE(y2_audio_h2w_headphones(3));
}

TEST(silence_and_dither_detection) {
    fill_block(0, 0);
    ASSERT_TRUE(y2_audio_is_silent(g_block, Y2_AUDIO_CHUNK_FRAMES));
    ASSERT_FALSE(y2_audio_needs_dither(g_block, Y2_AUDIO_CHUNK_FRAMES));
    fill_block(0x12340000, -0x56780000); // 16-bit material at unity: nothing below the 16th bit
    ASSERT_FALSE(y2_audio_is_silent(g_block, Y2_AUDIO_CHUNK_FRAMES));
    ASSERT_FALSE(y2_audio_needs_dither(g_block, Y2_AUDIO_CHUNK_FRAMES));
    g_block[2 * 100 + 1] |= 1; // one sample with a low bit
    ASSERT_TRUE(y2_audio_needs_dither(g_block, Y2_AUDIO_CHUNK_FRAMES));
}

// ---- the ring and the conversion ----

TEST(unity_16_bit_material_reaches_the_ring_bit_exact_and_attenuated_material_is_dithered) {
    setup(2, NULL);
    settle(); // reads the jack
    ASSERT_TRUE(y2_audio_headphones(&g_dev));
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce((int32_t)0x12340000, (int32_t)0xA9880000));
    int16_t out[Y2_AUDIO_CHUNK_FRAMES * 2];
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, spsc_ring_read(&g_dev.ring, out, Y2_AUDIO_CHUNK_FRAMES));
    ASSERT_EQ(0x1234, out[0]);
    ASSERT_EQ((int16_t)0xA988, out[1]);
    ASSERT_EQ(0x1234, out[2 * 255]);

    // a level with bits below the 16th: rounded with a triangular dither, so within +-1 LSB of the exact value and not constant
    fill_block(0x12348000 + 0x1000, 0);
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, y2_audio_produce(&g_dev, g_block, Y2_AUDIO_CHUNK_FRAMES));
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, spsc_ring_read(&g_dev.ring, out, Y2_AUDIO_CHUNK_FRAMES));
    bool varied = false;
    for (uint32_t i = 0; i < Y2_AUDIO_CHUNK_FRAMES; i++) {
        int d = out[2 * i] - 0x1235; // 0x12349000 / 65536 = 0x1234.56 -> 0x1235 when rounded
        ASSERT_TRUE(d >= -1 && d <= 1);
        varied = varied || out[2 * i] != out[0];
    }
    ASSERT_TRUE(varied);
}

TEST(the_ring_fills_up_then_refuses_blocks) {
    setup(2, NULL);
    settle();
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce(1 << 20, 1 << 20)); // starts the real output (want_dev)
    ASSERT_TRUE(y2_audio_clock_follows_device(&g_dev));
    uint32_t blocks = 1;
    while (produce(1 << 20, 1 << 20) == Y2_AUDIO_CHUNK_FRAMES) {
        blocks++;
    }
    ASSERT_EQ(Y2_AUDIO_RING_FRAMES / Y2_AUDIO_CHUNK_FRAMES, blocks);
    ASSERT_EQ(0, y2_audio_space(&g_dev));
}

// ---- the state machine ----

TEST(no_headphones_means_the_null_sink_and_the_device_is_never_touched) {
    setup(0, NULL);
    settle();
    ASSERT_FALSE(y2_audio_headphones(&g_dev));
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce(1 << 20, 1 << 20));
    ASSERT_FALSE(y2_audio_clock_follows_device(&g_dev)); // the timer paces
    settle();
    ASSERT_EQ(Y2_DAC_OFF, y2_audio_state(&g_dev));
    ASSERT_EQ(0, g_n);
    setup(-1, NULL); // an unreadable jack counts as nothing plugged in
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(0, g_n);
}

TEST(the_first_audible_block_powers_the_dac_and_starts_the_stream_in_the_verified_order) {
    setup(2, NULL);
    settle();
    ASSERT_EQ(0, g_n); // idle until something is audible: the DAC is not powered at boot
    // silence does not start anything
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce(0, 0));
    settle();
    ASSERT_EQ(0, g_n);
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce(1 << 20, 1 << 20));
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    ASSERT_EQ(1u, g_dev.stats.starts);

    const int on = find('D', 0x4302, 0, 0);
    const int stereo = find('D', 0x4307, 0, 0);
    const int mute = find('D', 0x40044304ul, 0, 0);
    const int ana = find('E', 0xc0044355ul, 1, 0);
    const int afe = find('E', 0xc0044351ul, 1, 0);
    const int i2s = find('E', 0xc0044354ul, 1, 0);
    const int alloc = find('E', 0xc0044310ul, Y2_AUDIO_BUF_BYTES, 0);
    const int start = find('E', 0xc0044320ul, 0, 0);
    const int vol = find('D', 0x40044304ul, 11, 0);
    ASSERT_TRUE(on >= 0 && on < stereo && stereo < mute && mute < ana && ana < afe && afe < i2s && i2s < alloc && alloc < start && start < vol);

    // the registers, as in the tone that was heard
    int r = alloc;
    const struct {
        unsigned long off, val, mask;
    } regs[] = {{0x20, (1u << 5) | (1u << 22), (1u << 5) | (1u << 22)},
                {0x14, 9, 0xF | (1u << 21)},
                {0x4c, 0x90A, 0xFFFFFFFEu},
                {0x4c, 1, 1},
                {0x4c4, 0x10, 0x10},
                {0x3ac, 1024, 0xFFFFFFFFu}, // period = buffer / 4 = 1024 frames
                {0x3a0, 9u << 4, 0xF0}};
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++) {
        while (r < g_n && g_log[r].kind != 'R') {
            r++;
        }
        ASSERT_TRUE(r < g_n);
        ASSERT_EQ(regs[i].off, g_log[r].cmd);
        ASSERT_EQ(regs[i].val, g_log[r].arg);
        ASSERT_EQ(regs[i].mask, g_log[r].mask);
        r++;
    }
    // START_MEMIF, then the silence prefill (half the buffer), then AFE_DAC_CON0 = 3, then the IRQ enable, then the volume
    const int con0 = first_register_write(0x10);
    ASSERT_TRUE(con0 > start);
    ASSERT_EQ(3, g_log[con0].arg);
    unsigned long prefill = 0;
    for (int i = start + 1; i < con0; i++) {
        ASSERT_EQ('W', g_log[i].kind);
        prefill += g_log[i].arg;
    }
    ASSERT_EQ(Y2_AUDIO_BUF_BYTES / 2, prefill);
    ASSERT_TRUE(con0 + 1 < g_n && g_log[con0 + 1].kind == 'R' && g_log[con0 + 1].cmd == 0x3a0 && g_log[con0 + 1].arg == 1);
    ASSERT_TRUE(vol > con0);
    ASSERT_EQ(0, g_dev.stats.refused);
    // the volume is restored only after the stream runs, and the user's audio is held back by a warm-up of silence
    ASSERT_EQ(1, count('D', 0x4302));
    ASSERT_EQ(1, count('D', 0x4307));
}

TEST(audio_in_the_ring_is_written_after_the_warm_up_in_order_and_nothing_is_lost) {
    setup(2, NULL);
    settle();
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce((int32_t)0x10000000, (int32_t)0x20000000)); // arrives while the DAC is still off
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce((int32_t)0x30000000, (int32_t)0x40000000));
    settle();
    pump(4);
    // find the first non-zero sample written: it is the first block
    size_t i = 0;
    while (i + 3 < g_written_bytes && g_written[i] == 0 && g_written[i + 1] == 0 && g_written[i + 2] == 0 && g_written[i + 3] == 0) {
        i += 4;
    }
    ASSERT_TRUE(i + 4u * Y2_AUDIO_CHUNK_FRAMES * 2u <= g_written_bytes);
    const int16_t *p = (const int16_t *)(g_written + i);
    ASSERT_EQ(0x1000, p[0]);
    ASSERT_EQ(0x2000, p[1]);
    ASSERT_EQ(0x3000, p[2 * Y2_AUDIO_CHUNK_FRAMES]);
    ASSERT_EQ(0x4000, p[2 * Y2_AUDIO_CHUNK_FRAMES + 1]);
    // the warm-up of silence before it is at least 100 ms
    ASSERT_TRUE(i >= (size_t)(44100u / 10u) * 4u);
}

TEST(a_starved_stream_counts_underruns_only_while_open_and_plays_silence) {
    setup(2, NULL);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    pump(1); // the block is written: the ring is now empty
    const uint32_t u0 = atomic_load(&g_dev.underruns);
    ASSERT_TRUE(y2_audio_work(&g_dev)); // the ring is empty
    ASSERT_EQ(u0 + 1u, atomic_load(&g_dev.underruns));
    y2_audio_set_open(&g_dev, false); // between streams (a track change): silence, not an underrun
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(u0 + 1u, atomic_load(&g_dev.underruns));
    y2_audio_stats_t st;
    y2_audio_stats(&g_dev, &st, true);
    ASSERT_EQ(u0 + 1u, st.underruns);
    ASSERT_TRUE(st.writes > 0);
}

TEST(silence_stops_the_stream_muted_first_and_keeps_the_dac_on_then_audio_restarts_without_a_power_cycle) {
    setup(2, NULL);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    // the player pauses: zeros go through the clock for a while; nothing happens before the hold time
    advance_ms(Y2_AUDIO_SILENCE_HOLD_MS - 500);
    (void)produce(0, 0);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    advance_ms(600);
    const int before = g_n;
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_ON, y2_audio_state(&g_dev));
    ASSERT_FALSE(y2_audio_clock_follows_device(&g_dev)); // back on the timer
    const int mute = find('D', 0x40044304ul, 0, before);
    const int standby = find('E', 0xc0044321ul, 0, before);
    const int free_ = find('E', 0xc0044311ul, 0, before);
    ASSERT_TRUE(mute >= before && mute < standby && standby < free_);
    ASSERT_EQ(0, count('D', 0x4303)); // the DAC stays powered
    // the last thing the teardown does is the clocks off: ANA, AFE and I2S
    ASSERT_EQ(2, count('E', 0xc0044354ul)); // on once, off once
    ASSERT_TRUE(find('E', 0xc0044355ul, 0, before) > free_);
    // audio comes back: same DAC power state, a new stream, nothing lost
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce(1 << 20, 1 << 20));
    ASSERT_TRUE(y2_audio_clock_follows_device(&g_dev));
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    ASSERT_EQ(1, count('D', 0x4302));
    ASSERT_EQ(2u, g_dev.stats.starts);
}

TEST(audio_arriving_while_the_stream_is_stopping_is_kept) {
    setup(2, NULL);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    advance_ms(Y2_AUDIO_SILENCE_HOLD_MS + 10);
    g_on_sleep = deliver_audible_block; // the clock delivers a block during the mute wait
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_TRUE(atomic_load(&g_dev.want_dev) != 0u);       // not handed back to the timer
    ASSERT_TRUE(spsc_ring_available(&g_dev.ring) >= Y2_AUDIO_CHUNK_FRAMES); // and still queued
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    pump(4);
    bool found = false;
    for (size_t i = 0; i + 1 < g_written_bytes; i += 2) {
        if (*(const int16_t *)(g_written + i) == 0x5000) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
}

TEST(idle_for_thirty_seconds_powers_the_dac_down_muted_first) {
    setup(2, NULL);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    advance_ms(Y2_AUDIO_SILENCE_HOLD_MS + 10);
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_ON, y2_audio_state(&g_dev));
    advance_ms(Y2_AUDIO_DAC_IDLE_OFF_MS - 1000);
    ASSERT_FALSE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_ON, y2_audio_state(&g_dev));
    const int before = g_n;
    advance_ms(1100);
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_OFF, y2_audio_state(&g_dev));
    const int off = find('D', 0x4303, 0, before);
    ASSERT_TRUE(off >= before);
    ASSERT_EQ(1, count('D', 0x4303));
    // the volume is at mute at that point (the stream stop muted it and nothing raised it since)
    int last_vol = -1;
    for (int i = 0; i < off; i++) {
        if (g_log[i].kind == 'D' && g_log[i].cmd == 0x40044304ul) {
            last_vol = (int)g_log[i].arg;
        }
    }
    ASSERT_EQ(0, last_vol);
    // and the next audible block powers it up again
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce(1 << 20, 1 << 20));
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    ASSERT_EQ(2, count('D', 0x4302));
}

TEST(unplugging_while_streaming_stops_everything_and_discards_the_audio) {
    setup(2, NULL);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    g_h2w = 0;
    advance_ms(Y2_AUDIO_HP_POLL_MS + 1);
    const int before = g_n;
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_OFF, y2_audio_state(&g_dev));
    ASSERT_FALSE(y2_audio_headphones(&g_dev));
    const int mute = find('D', 0x40044304ul, 0, before);
    const int standby = find('E', 0xc0044321ul, 0, before);
    const int off = find('D', 0x4303, 0, before);
    ASSERT_TRUE(mute >= before && mute < standby && standby < off);
    ASSERT_FALSE(y2_audio_clock_follows_device(&g_dev));
    // the player is paused by the route; audio that still comes is discarded, nothing restarts the device
    const int n = g_n;
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce(1 << 20, 1 << 20));
    settle();
    ASSERT_EQ(n, g_n);
    // plugging back in: the next audible block starts it again
    g_h2w = 2;
    advance_ms(Y2_AUDIO_HP_POLL_MS + 1);
    settle();
    ASSERT_TRUE(y2_audio_headphones(&g_dev));
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
}

TEST(the_jack_is_polled_not_read_on_every_pass) {
    setup(2, NULL);
    settle();
    g_h2w = 0;
    ASSERT_FALSE(y2_audio_work(&g_dev));
    ASSERT_TRUE(y2_audio_headphones(&g_dev)); // not read again yet
    advance_ms(Y2_AUDIO_HP_POLL_MS);
    (void)y2_audio_work(&g_dev);
    ASSERT_FALSE(y2_audio_headphones(&g_dev));
}

TEST(a_failing_device_falls_back_to_the_null_sink_powered_down_and_retries_later) {
    setup(2, NULL);
    settle();
    g_fail_cmd = 0xc0044320ul; // START_MEMIF
    (void)produce(1 << 20, 1 << 20);
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_OFF, y2_audio_state(&g_dev)); // powered down again
    ASSERT_EQ(1, count('D', 0x4303));
    ASSERT_TRUE(find('E', 0xc0044311ul, 0, 0) >= 0); // buffer freed
    ASSERT_FALSE(y2_audio_clock_follows_device(&g_dev));
    ASSERT_TRUE(g_dev.stats.errors > 0);
    // within the back-off the audio is discarded and the device is left alone
    g_fail_cmd = 0;
    const int n = g_n;
    advance_ms(1000);
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(n, g_n);
    // after it, the real output is tried again
    advance_ms(Y2_AUDIO_RETRY_MS);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
}

TEST(a_failed_write_is_a_fault_too) {
    setup(2, NULL);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    g_fail_write = 1;
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_OFF, y2_audio_state(&g_dev));
    ASSERT_EQ(1, count('D', 0x4303));
}

TEST(shutdown_and_force_off_power_everything_down) {
    setup(2, NULL);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    y2_audio_request_off(&g_dev); // hal_audio_force_mute
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_OFF, y2_audio_state(&g_dev));
    ASSERT_EQ(1, count('D', 0x4303));
    // it may start again at the next audible block ...
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
    // ... but not after a shutdown request
    y2_audio_request_shutdown(&g_dev);
    ASSERT_TRUE(y2_audio_work(&g_dev));
    ASSERT_EQ(Y2_DAC_OFF, y2_audio_state(&g_dev));
    ASSERT_EQ(2, count('D', 0x4303));
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_OFF, y2_audio_state(&g_dev));
    ASSERT_EQ(0, g_dev.stats.refused);
}

TEST(a_shutdown_with_the_dac_never_started_touches_nothing) {
    setup(2, NULL);
    settle();
    y2_audio_request_shutdown(&g_dev);
    ASSERT_FALSE(y2_audio_work(&g_dev));
    ASSERT_EQ(0, g_n);
}

TEST(the_bluetooth_virtual_clock_never_starts_the_dac) {
    setup(2, NULL);
    settle();
    y2_audio_set_virtual(&g_dev, true);
    settle();
    ASSERT_EQ(Y2_AUDIO_CHUNK_FRAMES, produce(1 << 20, 1 << 20));
    ASSERT_FALSE(y2_audio_clock_follows_device(&g_dev));
    settle();
    ASSERT_EQ(0, g_n);
    y2_audio_set_virtual(&g_dev, false);
    advance_ms(Y2_AUDIO_HP_POLL_MS);
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_EQ(Y2_DAC_STREAMING, y2_audio_state(&g_dev));
}

TEST(the_volume_index_from_the_environment_is_what_the_stream_starts_with) {
    setup(2, "7");
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_TRUE(find('D', 0x40044304ul, 7, 0) >= 0);
    ASSERT_EQ(-1, find('D', 0x40044304ul, 11, 0));
    setup(2, "15"); // clamped
    settle();
    (void)produce(1 << 20, 1 << 20);
    settle();
    ASSERT_TRUE(find('D', 0x40044304ul, 11, 0) >= 0);
    ASSERT_EQ(-1, find('D', 0x40044304ul, 15, 0));
}

int main(void) {
    RUN(only_the_whitelisted_afe_offsets_are_allowed);
    RUN(forbidden_device_commands_are_refused);
    RUN(a_bad_command_never_reaches_the_device);
    RUN(dac_index_default_env_and_clamp);
    RUN(h2w_parsing);
    RUN(silence_and_dither_detection);
    RUN(unity_16_bit_material_reaches_the_ring_bit_exact_and_attenuated_material_is_dithered);
    RUN(the_ring_fills_up_then_refuses_blocks);
    RUN(no_headphones_means_the_null_sink_and_the_device_is_never_touched);
    RUN(the_first_audible_block_powers_the_dac_and_starts_the_stream_in_the_verified_order);
    RUN(audio_in_the_ring_is_written_after_the_warm_up_in_order_and_nothing_is_lost);
    RUN(a_starved_stream_counts_underruns_only_while_open_and_plays_silence);
    RUN(silence_stops_the_stream_muted_first_and_keeps_the_dac_on_then_audio_restarts_without_a_power_cycle);
    RUN(audio_arriving_while_the_stream_is_stopping_is_kept);
    RUN(idle_for_thirty_seconds_powers_the_dac_down_muted_first);
    RUN(unplugging_while_streaming_stops_everything_and_discards_the_audio);
    RUN(the_jack_is_polled_not_read_on_every_pass);
    RUN(a_failing_device_falls_back_to_the_null_sink_powered_down_and_retries_later);
    RUN(a_failed_write_is_a_fault_too);
    RUN(shutdown_and_force_off_power_everything_down);
    RUN(a_shutdown_with_the_dac_never_started_touches_nothing);
    RUN(the_bluetooth_virtual_clock_never_starts_the_dac);
    RUN(the_volume_index_from_the_environment_is_what_the_stream_starts_with);
    return RESULT();
}

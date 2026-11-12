// SPDX-License-Identifier: MPL-2.0
// Y2 wired headphone output: allow-lists, sample conversion and the DAC / AFE state machine (see y2_audio.h). No device access
// of its own: everything goes through y2_audio_ops_t, so the host tests drive it with a fake.
#include "y2_audio.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_dsp.h"

// /dev/eac commands (MTK AudDrv, magic 'C'; recon section 2)
#define EAC_SET_AUDSYS_REG 0xc0044300ul
#define EAC_ALLOC_DL1 0xc0044310ul
#define EAC_FREE_DL1 0xc0044311ul
#define EAC_START_MEMIF 0xc0044320ul
#define EAC_STANDBY_MEMIF 0xc0044321ul
#define EAC_SET_AFE_CLK 0xc0044351ul
#define EAC_SET_I2S_CLK 0xc0044354ul
#define EAC_SET_ANA_CLK 0xc0044355ul
// /dev/cs43131_dac commands (recon section 4)
#define DAC_ON 0x4302ul
#define DAC_OFF 0x4303ul
#define DAC_STEREO 0x4307ul
#define DAC_VOLUME 0x40044304ul // arg is the index by value
// AFE register offsets
#define AFE_DAC_CON0 0x10u // bit0 AFE on, bit1 DL1 on
#define AFE_DAC_CON1 0x14u // bits[3:0] DL1 rate, bit21 mono
#define AFE_CONN0 0x20u    // bit5 I05->O00, bit22 I06->O01: DL1 to the external I2S
#define AFE_I2S_CON3 0x4cu // 2nd I2S out: rate<<8, format<<3, word length<<1, bit0 enable
#define AFE_IRQ_MCU_CON 0x3a0u
#define AFE_IRQ_CNT1 0x3acu
#define AFE_2ND_I2S_AUX 0x4c4u // bit4: set by the stock HAL for the 2nd I2S output

bool y2_audio_afe_offset_ok(uint32_t o) {
    return o == AFE_DAC_CON0 || o == AFE_DAC_CON1 || o == AFE_CONN0 || o == AFE_I2S_CON3 || o == AFE_IRQ_MCU_CON || o == AFE_IRQ_CNT1 ||
           o == AFE_2ND_I2S_AUX;
}

bool y2_audio_eac_cmd_ok(unsigned long c) {
    return c == EAC_SET_AUDSYS_REG || c == EAC_ALLOC_DL1 || c == EAC_FREE_DL1 || c == EAC_START_MEMIF || c == EAC_STANDBY_MEMIF ||
           c == EAC_SET_AFE_CLK || c == EAC_SET_I2S_CLK || c == EAC_SET_ANA_CLK;
}

bool y2_audio_dac_cmd_ok(unsigned long c) { return c == DAC_ON || c == DAC_OFF || c == DAC_STEREO || c == DAC_VOLUME; }

static const uint8_t k_vol_reg[16] = {0x00, 0x6e, 0x68, 0x62, 0x5c, 0x56, 0x50, 0x4a, 0x44, 0x3b, 0x32, 0x29, 0x20, 0x17, 0x0e, 0x06};

uint8_t y2_audio_dac_volume_reg(uint32_t index) { return k_vol_reg[index > 15u ? 15u : index]; }

uint32_t y2_audio_dac_index_from_env(const char *env) {
    if (env == NULL || env[0] == '\0') {
        return Y2_AUDIO_DAC_INDEX;
    }
    char *end = NULL;
    long v = strtol(env, &end, 10);
    if (end == env || *end != '\0') {
        return Y2_AUDIO_DAC_INDEX;
    }
    return v < 0 ? 0u : v > (long)Y2_AUDIO_DAC_INDEX_MAX ? Y2_AUDIO_DAC_INDEX_MAX : (uint32_t)v;
}

bool y2_audio_h2w_headphones(int state) { return state == 1 || state == 2; }

int y2_audio_h2w_parse(const char *text) {
    if (text == NULL) {
        return -1;
    }
    char *end = NULL;
    long v = strtol(text, &end, 10);
    return end == text || v < 0 || v > 1000 ? -1 : (int)v;
}

bool y2_audio_needs_dither(const int32_t *in, uint32_t frames) {
    for (uint32_t i = 0; i < frames * 2u; i++) {
        if ((in[i] & 0xFFFF) != 0) {
            return true;
        }
    }
    return false;
}

bool y2_audio_is_silent(const int32_t *in, uint32_t frames) {
    for (uint32_t i = 0; i < frames * 2u; i++) {
        if (in[i] != 0) {
            return false;
        }
    }
    return true;
}

// ---- Device access through the allow-lists ----

static uint64_t now_us(const y2_audio_t *a) { return a->ops.now_us(a->ops.ctx); }

static void logf_(const y2_audio_t *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void logf_(const y2_audio_t *a, const char *fmt, ...) {
    if (a->ops.log == NULL) {
        return;
    }
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    a->ops.log(a->ops.ctx, line);
}

static int dac_cmd(y2_audio_t *a, unsigned long cmd, unsigned long arg) {
    if (!y2_audio_dac_cmd_ok(cmd)) {
        a->stats.refused++;
        return -1;
    }
    int r = a->ops.dac_ioctl(a->ops.ctx, cmd, arg);
    if (r < 0) {
        a->stats.errors++;
    } else if (cmd == DAC_ON) {
        a->dac_muted = false; // powering up restores the last volume
    } else if (cmd == DAC_VOLUME) {
        a->dac_muted = arg == 0;
    }
    return r;
}

static int eac_cmd(y2_audio_t *a, unsigned long cmd, unsigned long arg) {
    if (!y2_audio_eac_cmd_ok(cmd)) {
        a->stats.refused++;
        return -1;
    }
    int r = a->ops.eac_ioctl(a->ops.ctx, cmd, arg);
    if (r < 0) {
        a->stats.errors++;
    }
    return r;
}

static int afe_set(y2_audio_t *a, uint32_t offset, uint32_t value, uint32_t mask) {
    if (!y2_audio_afe_offset_ok(offset)) {
        a->stats.refused++;
        return -1;
    }
    y2_reg_ctl_t rc = {.offset = offset, .value = value, .mask = mask};
    return eac_cmd(a, EAC_SET_AUDSYS_REG, (unsigned long)(uintptr_t)&rc);
}

static void sleep_ms(const y2_audio_t *a, uint32_t ms) { a->ops.sleep_ms(a->ops.ctx, ms); }

static bool write_all(y2_audio_t *a, const void *buf, size_t bytes) {
    const uint8_t *p = buf;
    size_t off = 0;
    while (off < bytes) {
        const uint64_t t0 = now_us(a);
        const long w = a->ops.eac_write(a->ops.ctx, p + off, bytes - off);
        const uint64_t dt = now_us(a) - t0;
        if (w <= 0) {
            a->stats.errors++;
            return false;
        }
        a->stats.writes++;
        a->stats.window_writes++;
        a->stats.write_us_sum += dt;
        if (dt > a->stats.write_us_max) {
            a->stats.write_us_max = (uint32_t)dt;
        }
        off += (size_t)w;
    }
    return true;
}

static bool write_silence(y2_audio_t *a, uint32_t frames) {
    static const int16_t zeros[Y2_AUDIO_CHUNK_FRAMES * 2u];
    while (frames > 0) {
        const uint32_t n = frames < Y2_AUDIO_CHUNK_FRAMES ? frames : Y2_AUDIO_CHUNK_FRAMES;
        if (!write_all(a, zeros, (size_t)n * Y2_AUDIO_BYTES_PER_FRAME)) {
            return false;
        }
        frames -= n;
    }
    return true;
}

// ---- Producer ----

void y2_audio_init(y2_audio_t *a, const y2_audio_ops_t *ops, const char *env_index) {
    memset(a, 0, sizeof *a);
    a->ops = *ops;
    a->dac_index = y2_audio_dac_index_from_env(env_index);
    (void)spsc_ring_init(&a->ring, a->store, 2u * sizeof(int16_t), Y2_AUDIO_RING_FRAMES);
    a->st = Y2_DAC_OFF;
}

uint32_t y2_audio_produce(y2_audio_t *a, const int32_t *in, uint32_t frames) {
    if (frames > Y2_AUDIO_CHUNK_FRAMES) {
        frames = Y2_AUDIO_CHUNK_FRAMES;
    }
    if (!y2_audio_is_silent(in, frames)) {
        // seq first, then the ring: the worker's standby check relies on this order (see y2_audio_work)
        atomic_fetch_add(&a->audible_seq, 1u);
        atomic_store(&a->last_audible_us, now_us(a));
        if (atomic_load(&a->dev_ok) != 0u) {
            atomic_store(&a->want_dev, 1u);
        }
    }
    if (atomic_load(&a->want_dev) == 0u) {
        return frames; // the null sink: discarded
    }
    if (spsc_ring_space(&a->ring) < frames) {
        return 0;
    }
    audio_s32_to_s16(in, a->chunk16, frames, y2_audio_needs_dither(in, frames), &a->dither_rng);
    return spsc_ring_write(&a->ring, a->chunk16, frames);
}

uint32_t y2_audio_space(const y2_audio_t *a) { return spsc_ring_space(&a->ring); }

bool y2_audio_clock_follows_device(const y2_audio_t *a) { return atomic_load(&a->want_dev) != 0u && atomic_load(&a->virt) == 0u; }

void y2_audio_set_open(y2_audio_t *a, bool open) { atomic_store(&a->stream_open, open ? 1u : 0u); }

void y2_audio_set_virtual(y2_audio_t *a, bool virt) { atomic_store(&a->virt, virt ? 1u : 0u); }

bool y2_audio_headphones(const y2_audio_t *a) { return atomic_load(&a->headphones) != 0u; }

// ---- Worker ----

static void drop_ring(y2_audio_t *a) {
    atomic_store(&a->want_dev, 0u);
    (void)spsc_ring_discard(&a->ring, Y2_AUDIO_RING_FRAMES);
}

// Everything the AFE side set up, in reverse. Best effort: each step is independent and a failure only counts.
static void afe_teardown(y2_audio_t *a) {
    (void)eac_cmd(a, EAC_STANDBY_MEMIF, 0);
    (void)afe_set(a, AFE_IRQ_MCU_CON, 0u, 0x1u);
    (void)afe_set(a, AFE_DAC_CON0, 0u, 0x3u);
    (void)afe_set(a, AFE_IRQ_MCU_CON, 0u, 0xF0u);
    (void)afe_set(a, AFE_IRQ_CNT1, 0u, 0xFFFFFFFFu);
    (void)afe_set(a, AFE_2ND_I2S_AUX, 0u, 0x10u);
    (void)afe_set(a, AFE_I2S_CON3, 0u, 0x1u);
    (void)afe_set(a, AFE_I2S_CON3, 0u, 0xFFFFFFFEu);
    (void)afe_set(a, AFE_DAC_CON1, 0u, 0xFu | (1u << 21));
    (void)afe_set(a, AFE_CONN0, 0u, (1u << 5) | (1u << 22));
    (void)eac_cmd(a, EAC_FREE_DL1, 0);
    (void)eac_cmd(a, EAC_SET_I2S_CLK, 0);
    (void)eac_cmd(a, EAC_SET_AFE_CLK, 0);
    (void)eac_cmd(a, EAC_SET_ANA_CLK, 0);
}

// Stop the AFE stream with the DAC muted first (a stream torn down under an audible DAC clicks). Leaves the DAC powered and muted.
static void stop_afe(y2_audio_t *a) {
    (void)dac_cmd(a, DAC_VOLUME, 0);
    sleep_ms(a, Y2_AUDIO_VOL_SETTLE_MS);
    afe_teardown(a);
    a->st = Y2_DAC_ON;
    a->on_since_us = now_us(a);
}

// Everything off: AFE if running, then the DAC (muted before it is powered down), and anything queued is dropped.
static void power_down(y2_audio_t *a) {
    if (a->st == Y2_DAC_STREAMING) {
        stop_afe(a);
    }
    if (a->st != Y2_DAC_OFF) {
        if (!a->dac_muted) {
            (void)dac_cmd(a, DAC_VOLUME, 0);
            sleep_ms(a, Y2_AUDIO_VOL_SETTLE_MS);
        }
        (void)dac_cmd(a, DAC_OFF, 0);
        sleep_ms(a, 20);
        a->st = Y2_DAC_OFF;
        logf_(a, "audio: DAC powered down");
    }
    drop_ring(a);
}

static void fault(y2_audio_t *a, const char *what) {
    logf_(a, "audio: %s failed, back to the null sink for %u s", what, (unsigned)(Y2_AUDIO_RETRY_MS / 1000u));
    a->retry_at_us = now_us(a) + (uint64_t)Y2_AUDIO_RETRY_MS * 1000u;
    atomic_store(&a->dev_ok, 0u);
    power_down(a);
}

static bool start_dac(y2_audio_t *a) {
    if (dac_cmd(a, DAC_ON, 0) < 0) {
        return false;
    }
    a->st = Y2_DAC_ON; // from here a failure powers it down again (fault)
    a->on_since_us = now_us(a);
    if (dac_cmd(a, DAC_STEREO, 0) < 0) {
        return false;
    }
    // 0x4302 restores the last volume: mute at once, nothing is streaming yet
    if (dac_cmd(a, DAC_VOLUME, 0) < 0) {
        return false;
    }
    sleep_ms(a, Y2_AUDIO_VOL_SETTLE_MS);
    logf_(a, "audio: DAC on (volume index %u, reg 0x%02x when audible)", (unsigned)a->dac_index, (unsigned)y2_audio_dac_volume_reg(a->dac_index));
    return true;
}

// The verified bring-up order (12 Nov 2026), then the volume is restored and silence runs while the kernel applies it.
static bool start_afe(y2_audio_t *a) {
    const uint32_t period_frames = Y2_AUDIO_BUF_BYTES / 4u / Y2_AUDIO_BYTES_PER_FRAME;
    const uint32_t con3 = (9u << 8) | (1u << 3) | (1u << 1); // 44.1 kHz, I2S format, 32-bit slot, master
    bool ok = eac_cmd(a, EAC_SET_ANA_CLK, 1) >= 0 && eac_cmd(a, EAC_SET_AFE_CLK, 1) >= 0 && eac_cmd(a, EAC_SET_I2S_CLK, 1) >= 0 &&
              eac_cmd(a, EAC_ALLOC_DL1, Y2_AUDIO_BUF_BYTES) >= 0 && afe_set(a, AFE_CONN0, (1u << 5) | (1u << 22), (1u << 5) | (1u << 22)) >= 0 &&
              afe_set(a, AFE_DAC_CON1, 9u, 0xFu | (1u << 21)) >= 0 && afe_set(a, AFE_I2S_CON3, con3, 0xFFFFFFFEu) >= 0 &&
              afe_set(a, AFE_I2S_CON3, 1u, 1u) >= 0 && afe_set(a, AFE_2ND_I2S_AUX, 0x10u, 0x10u) >= 0 &&
              afe_set(a, AFE_IRQ_CNT1, period_frames, 0xFFFFFFFFu) >= 0 && afe_set(a, AFE_IRQ_MCU_CON, 9u << 4, 0xF0u) >= 0 &&
              eac_cmd(a, EAC_START_MEMIF, 0) >= 0 &&
              write_silence(a, Y2_AUDIO_BUF_BYTES / 2u / Y2_AUDIO_BYTES_PER_FRAME) && // prefill before the AFE and its interrupt start
              afe_set(a, AFE_DAC_CON0, 3u, 3u) >= 0 && afe_set(a, AFE_IRQ_MCU_CON, 1u, 1u) >= 0;
    if (ok) {
        ok = dac_cmd(a, DAC_VOLUME, a->dac_index) >= 0 &&
             write_silence(a, (Y2_AUDIO_RATE / 1000u) * Y2_AUDIO_WARMUP_MS); // audio waits in the ring until the volume has taken
    }
    if (!ok) {
        afe_teardown(a);
        return false;
    }
    a->st = Y2_DAC_STREAMING;
    a->stats.starts++;
    logf_(a, "audio: stream started (44100 Hz S16 stereo, %u-byte DMA buffer)", (unsigned)Y2_AUDIO_BUF_BYTES);
    return true;
}

bool y2_audio_work(y2_audio_t *a) {
    const uint64_t now = now_us(a);
    if (!a->hp_read || now >= a->next_hp_poll_us) {
        const int s = a->ops.headphones(a->ops.ctx);
        atomic_store(&a->headphones, s >= 0 && y2_audio_h2w_headphones(s) ? 1u : 0u);
        const bool had = a->hp_read;
        a->hp_read = true;
        a->next_hp_poll_us = now + (uint64_t)Y2_AUDIO_HP_POLL_MS * 1000u;
        if (!had) {
            logf_(a, "audio: jack state %d (%s)", s, y2_audio_h2w_headphones(s) ? "headphones" : "nothing plugged in");
        }
    }
    const bool hp = atomic_load(&a->headphones) != 0u;
    const bool sd = atomic_load(&a->shutdown) != 0u;
    const bool fo = atomic_exchange(&a->force_off, 0u) != 0u;
    atomic_store(&a->dev_ok, hp && !sd && atomic_load(&a->virt) == 0u && now >= a->retry_at_us ? 1u : 0u);
    const bool ok = atomic_load(&a->dev_ok) != 0u;

    if (a->st != Y2_DAC_OFF && (!hp || sd || fo)) {
        if (!hp) {
            logf_(a, "audio: headphones unplugged");
        }
        power_down(a);
        return true;
    }

    switch (a->st) {
    case Y2_DAC_OFF:
        if (atomic_load(&a->want_dev) == 0u) {
            return false;
        }
        if (!ok) {
            drop_ring(a);
            return false;
        }
        if (!start_dac(a) || !start_afe(a)) {
            fault(a, "starting the DAC");
            return true;
        }
        return true;

    case Y2_DAC_ON:
        if (atomic_load(&a->want_dev) != 0u) {
            if (!ok) {
                drop_ring(a);
                return false;
            }
            if (!start_afe(a)) {
                fault(a, "starting the stream");
            }
            return true;
        }
        if (now - a->on_since_us >= (uint64_t)Y2_AUDIO_DAC_IDLE_OFF_MS * 1000u) {
            power_down(a);
            return true;
        }
        return false;

    case Y2_DAC_STREAMING:
        break;
    }

    // Streaming. Digital silence for long enough: stop the stream (the DAC stays on). Samples queued by the clock before this point are
    // silent too, unless a non-silent block arrives while the stream is stopping (audible_seq moved): then they are kept and the next
    // pass restarts the stream.
    if (now - atomic_load(&a->last_audible_us) >= (uint64_t)Y2_AUDIO_SILENCE_HOLD_MS * 1000u) {
        const uint32_t seq0 = atomic_load(&a->audible_seq);
        stop_afe(a);
        atomic_store(&a->want_dev, 0u);
        const uint32_t queued = spsc_ring_available(&a->ring);
        if (atomic_load(&a->audible_seq) != seq0) {
            atomic_store(&a->want_dev, 1u);
        } else {
            (void)spsc_ring_discard(&a->ring, queued);
        }
        logf_(a, "audio: stream stopped after %u s of silence (DAC stays on)", (unsigned)(Y2_AUDIO_SILENCE_HOLD_MS / 1000u));
        return true;
    }

    uint32_t got = spsc_ring_read(&a->ring, a->wchunk, Y2_AUDIO_CHUNK_FRAMES);
    if (got < Y2_AUDIO_CHUNK_FRAMES) {
        memset(a->wchunk + got * 2u, 0, (Y2_AUDIO_CHUNK_FRAMES - got) * Y2_AUDIO_BYTES_PER_FRAME);
        if (atomic_load(&a->stream_open) != 0u) {
            atomic_fetch_add(&a->underruns, 1u);
        }
    }
    if (!write_all(a, a->wchunk, (size_t)Y2_AUDIO_CHUNK_FRAMES * Y2_AUDIO_BYTES_PER_FRAME)) {
        fault(a, "writing audio");
        return true;
    }
    if (now - a->last_log_us >= 10u * 1000000u) {
        a->last_log_us = now;
        const uint32_t w = a->stats.window_writes != 0u ? a->stats.window_writes : 1u;
        logf_(a, "audio: y2 dac streaming: underruns=%u writes=%u write_us avg/max=%u/%u", (unsigned)atomic_load(&a->underruns),
              (unsigned)a->stats.writes, (unsigned)(a->stats.write_us_sum / w), (unsigned)a->stats.write_us_max);
        a->stats.window_writes = 0;
        a->stats.write_us_sum = 0;
        a->stats.write_us_max = 0;
    }
    return true;
}

void y2_audio_request_shutdown(y2_audio_t *a) { atomic_store(&a->shutdown, 1u); }

void y2_audio_request_off(y2_audio_t *a) { atomic_store(&a->force_off, 1u); }

y2_dac_state_t y2_audio_state(const y2_audio_t *a) { return a->st; }

void y2_audio_stats(y2_audio_t *a, y2_audio_stats_t *out, bool reset_window) {
    *out = a->stats;
    out->underruns = atomic_load(&a->underruns);
    if (reset_window) {
        a->stats.window_writes = 0;
        a->stats.write_us_sum = 0;
        a->stats.write_us_max = 0;
    }
}

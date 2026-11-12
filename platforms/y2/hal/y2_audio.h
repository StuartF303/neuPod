// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_audio.h
 * @brief Y2 wired headphone output: the CS43131 DAC behind the MT6582 AFE, driven through /dev/eac and /dev/cs43131_dac.
 *        Portable logic (the device is reached only through y2_audio_ops_t), host-tested; hal_audio.c wires it to the device.
 *
 * What it does: a lock-free ring takes the audio core's 32-bit blocks (converted to S16, TPDF dither when bits below the 16th
 * are present) from the block clock and a worker thread streams them with blocking write() to /dev/eac, which paces
 * everything on the DAC's own clock. The DAC stays powered across tracks and pauses; the AFE stream stops after a few seconds
 * of digital silence and the DAC powers down after a longer idle, always with the DAC volume at mute first (clicks).
 * Output happens only while /sys/class/switch/h2w reports headphones; otherwise the stream is discarded (the null sink).
 *
 * SAFETY (owner rules, enforced here and tested): only the AFE offsets and device commands listed below ever reach the
 * kernel. Never the PMIC analog registers (SET_ANAAFE_REG 0xc0044302), never the speaker amplifier ioctls (0x400443a0..a7),
 * never the CS43131 mono path (0x4306), hardware init (0x4301) or its debug register file.
 */
#ifndef NEUPOD_Y2_AUDIO_H
#define NEUPOD_Y2_AUDIO_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "spsc_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

#define Y2_EAC_PATH "/dev/eac"
#define Y2_DAC_PATH "/dev/cs43131_dac"
#define Y2_H2W_PATH "/sys/class/switch/h2w/state"
#define Y2_AUDIO_DAC_INDEX_ENV "NEUPOD_Y2_DAC_INDEX"

/** CS43131 hardware volume index (kernel table, 0 = mute .. 15, 0.5 dB per step). 11 = 0x29 = -20.5 dB, about 0.2 Vrms at full scale. The user's
 * volume is the software volume in the audio core. An override from the environment is clamped to Y2_AUDIO_DAC_INDEX_MAX. */
#define Y2_AUDIO_DAC_INDEX 11u
#define Y2_AUDIO_DAC_INDEX_MAX 11u

/** Kernel DL1 buffer (bytes) and what the AFE interrupts on: a quarter of it. */
#define Y2_AUDIO_BUF_BYTES 16384u
#define Y2_AUDIO_BYTES_PER_FRAME 4u
#define Y2_AUDIO_RING_FRAMES 4096u  /**< ring between the block clock and the writer (power of two): 93 ms at 44.1 kHz */
#define Y2_AUDIO_CHUNK_FRAMES 256u  /**< one write(): a block */
#define Y2_AUDIO_RATE 44100u

#define Y2_AUDIO_SILENCE_HOLD_MS 3000u   /**< digital silence for this long stops the AFE stream (DAC stays powered) */
#define Y2_AUDIO_DAC_IDLE_OFF_MS 30000u  /**< ... and this long after that the DAC is powered down (0x4303) */
#define Y2_AUDIO_VOL_SETTLE_MS 100u      /**< the kernel applies a volume change by delayed work 80 ms later */
#define Y2_AUDIO_WARMUP_MS 100u          /**< silence streamed after the volume is restored, before audio is let through */
#define Y2_AUDIO_HP_POLL_MS 200u
#define Y2_AUDIO_RETRY_MS 5000u          /**< after a device error the real output is not tried again for this long */

/* ---- The allow-lists ---- */

/** AFE register offsets we may touch. 0x10 is AFE_DAC_CON0 (bit 0 AFE on, bit 1 DL1 on): without it the DL1 memory interface
 * never starts, and the verified tone path (12 Nov 2026) sets it. */
bool y2_audio_afe_offset_ok(uint32_t offset);
/** /dev/eac commands: SET_AUDSYS_REG, ALLOCATE/FREE_MEMIF_DL1, START/STANDBY_MEMIF, AUD_SET_ANA/AFE/I2S_CLOCK. */
bool y2_audio_eac_cmd_ok(unsigned long cmd);
/** /dev/cs43131_dac commands: on (0x4302), off (0x4303), set volume (0x40044304), stereo (0x4307). */
bool y2_audio_dac_cmd_ok(unsigned long cmd);

/** The kernel's volume-register value for a volume index (0 = mute .. 15), for the log. */
uint8_t y2_audio_dac_volume_reg(uint32_t index);
/** The DAC index to use: @p env (decimal, e.g. getenv()) or the default; clamped to 0..Y2_AUDIO_DAC_INDEX_MAX. Unparseable = default. */
uint32_t y2_audio_dac_index_from_env(const char *env);
/** h2w switch state: 1 (headset) and 2 (headphones) have something on the jack. */
bool y2_audio_h2w_headphones(int state);
/** Parse the text of the h2w state file; -1 if it is not a number. */
int y2_audio_h2w_parse(const char *text);

/* ---- Sample conversion ---- */

/** True if any sample has bits below the 16th set (volume attenuation or a deeper source): those need dither when cut to 16 bit. */
bool y2_audio_needs_dither(const int32_t *in, uint32_t frames);
/** True if every sample is zero. */
bool y2_audio_is_silent(const int32_t *in, uint32_t frames);

/* ---- The device, as the state machine sees it ---- */

typedef struct {
    uint32_t offset, value, mask;
} y2_reg_ctl_t; /**< SET_AUDSYS_REG argument: the kernel reads 12 bytes */

typedef struct {
    int (*dac_ioctl)(void *ctx, unsigned long cmd, unsigned long arg); /**< /dev/cs43131_dac; arg by value */
    int (*eac_ioctl)(void *ctx, unsigned long cmd, unsigned long arg); /**< /dev/eac; arg by value or a pointer to y2_reg_ctl_t */
    long (*eac_write)(void *ctx, const void *buf, size_t bytes);       /**< blocks until the DMA ring has room; bytes written or < 0 */
    int (*headphones)(void *ctx);                                      /**< the h2w state, < 0 if unreadable */
    uint64_t (*now_us)(void *ctx);
    void (*sleep_ms)(void *ctx, uint32_t ms);
    void (*log)(void *ctx, const char *line); /**< optional: state changes and a stats line every 10 s while streaming */
    void *ctx;
} y2_audio_ops_t;

typedef enum {
    Y2_DAC_OFF = 0,   /**< powered down (or never started by us) */
    Y2_DAC_ON,        /**< powered, volume muted, no AFE stream */
    Y2_DAC_STREAMING, /**< AFE running, writer feeding the DMA ring */
} y2_dac_state_t;

typedef struct {
    uint32_t underruns;     /**< chunks the ring could not supply while the stream was open (silence went out instead); filled by y2_audio_stats() */
    uint32_t writes;        /**< write() calls */
    uint32_t write_us_max;  /**< longest write() since the last stats window */
    uint64_t write_us_sum;  /**< total time in write() since the last stats window (average = sum / window_writes) */
    uint32_t window_writes;
    uint32_t refused;       /**< commands the allow-lists stopped (must stay 0) */
    uint32_t errors;        /**< device calls that failed */
    uint32_t starts;        /**< AFE stream starts */
} y2_audio_stats_t;

typedef struct {
    y2_audio_ops_t ops;
    uint32_t dac_index;
    y2_audio_stats_t stats;

    /* shared by the block clock (producer) and the worker */
    spsc_ring_t ring;
    int16_t store[Y2_AUDIO_RING_FRAMES * 2u];
    _Atomic uint32_t want_dev;      /**< the clock paces on the ring (the device is, or is being, started) */
    _Atomic uint32_t dev_ok;        /**< the real output may be used now (headphones in, no recent fault, not virtual) */
    _Atomic uint32_t audible_seq;   /**< counts non-silent blocks */
    _Atomic uint64_t last_audible_us;
    _Atomic uint32_t stream_open;
    _Atomic uint32_t virt;          /**< Bluetooth: the sink is only a clock */
    _Atomic uint32_t headphones;    /**< last h2w reading */
    _Atomic uint32_t shutdown;
    _Atomic uint32_t force_off;
    _Atomic uint32_t underruns;
    uint32_t dither_rng;            /**< producer's */
    int16_t chunk16[Y2_AUDIO_CHUNK_FRAMES * 2u]; /**< producer's conversion scratch */

    /* worker only */
    y2_dac_state_t st;
    uint64_t on_since_us;           /**< when the DAC entered Y2_DAC_ON */
    uint64_t next_hp_poll_us;
    uint64_t retry_at_us;
    uint64_t last_log_us;
    bool dac_muted;                 /**< the last volume command was mute (index 0) */
    bool hp_read;                   /**< the first reading has been taken */
    int16_t wchunk[Y2_AUDIO_CHUNK_FRAMES * 2u];
} y2_audio_t;

/** @p env_index: the NEUPOD_Y2_DAC_INDEX text or NULL. Nothing touches the device. */
void y2_audio_init(y2_audio_t *a, const y2_audio_ops_t *ops, const char *env_index);

/* Producer side (the block clock; never blocks). */

/** Take one block (up to Y2_AUDIO_CHUNK_FRAMES frames, interleaved int32 left-justified). Returns frames accepted: all of them while the
 * output is the null sink or the ring has room, fewer when the ring is full. Starts the real output on the first non-silent block. */
uint32_t y2_audio_produce(y2_audio_t *a, const int32_t *in, uint32_t frames);
/** Ring room in frames while the real output paces the clock. */
uint32_t y2_audio_space(const y2_audio_t *a);
/** The clock should wait for ring room (true) rather than run on a timer (the null sink). */
bool y2_audio_clock_follows_device(const y2_audio_t *a);
void y2_audio_set_open(y2_audio_t *a, bool open);
void y2_audio_set_virtual(y2_audio_t *a, bool virt);
/** Headphones in (as of the last reading). */
bool y2_audio_headphones(const y2_audio_t *a);

/* Worker side. */

/** One pass of the worker: poll the jack, run the state machine, write at most one chunk (blocking while the DMA ring is full).
 * Returns true if it did device work (call again at once), false if idle (sleep a little). */
bool y2_audio_work(y2_audio_t *a);
/** Ask the worker to power everything down and stay down: use before exit. y2_audio_work() then reaches Y2_DAC_OFF. */
void y2_audio_request_shutdown(y2_audio_t *a);
/** Ask for a power-down now, the real output may start again at the next audible block (hal_audio_force_mute). */
void y2_audio_request_off(y2_audio_t *a);
y2_dac_state_t y2_audio_state(const y2_audio_t *a);
/** A copy of the statistics; @p reset_window restarts the write-latency window. */
void y2_audio_stats(y2_audio_t *a, y2_audio_stats_t *out, bool reset_window);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_AUDIO_H

// SPDX-License-Identifier: MPL-2.0
/*
 * I2S audio sink for the Pimoroni Pico Audio Pack: PIO I2S transmitter fed by two ping-pong DMA channels.
 *
 *   PIO0 state machine  -- neupod_i2s.pio: 32-bit slots, stereo, we are the master (BCK GP10, LRCK GP11, DATA GP9)
 *   DMA ch A <-> ch B   -- each moves one 256-frame block (512 words) into the PIO TX FIFO and chains to the other
 *   DMA_IRQ_1 on core 1 -- fires when a block has been sent: refills that buffer through the block callback
 *
 * The interrupt is enabled from the core that calls hal_audio_open(), the bare-metal audio core (core 1);
 * DMA_IRQ_0 stays with the SD library on core 0. docs/audio.md has the allocation, clocking and mute story.
 *
 * CORE-1 RULES apply to this whole file (docs/adr/0011-bare-metal-audio-core.md): hardware registers and
 * spin-lock-only SDK calls only; nothing that reaches the RTOS. Enforced by cmake/check_core1_rules.cmake.
 * The one exception by design is hal_audio_force_mute(), which core 0's fatal-error path also calls; it only
 * writes registers.
 */
#include "hal_audio.h"

#include "hal_system.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "neupod_i2s.pio.h"

#define PIN_DATA 9u
#define PIN_BCK 10u // BCK and LRCK must be consecutive: side-set base = BCK, base + 1 = LRCK
#define PIN_LRCK 11u

#ifndef NEUPOD_AUDIO_MUTE_PIN
#define NEUPOD_AUDIO_MUTE_PIN 22u
#endif
// TODO(hw): UNCONFIRMED polarity of the Audio Pack's mute / amp-enable line on GP22. Neither pico-extras nor
// pico-playground drives it (no board file for the Pack); docs/design.md assumes "high = amp enabled" pending
// Pimoroni's pinout (docs/reference.md checklist). The SAFE state is the opposite level, which is what the pin
// is driven to at boot, on close and in the fatal-error path. If the polarity is wrong the board stays silent
// (nothing is ever unmuted by accident); build with -DNEUPOD_AUDIO_UNMUTE_LEVEL=0 to flip it.
#ifndef NEUPOD_AUDIO_UNMUTE_LEVEL
#define NEUPOD_AUDIO_UNMUTE_LEVEL 1u
#endif

#define BLOCK_WORDS (HAL_AUDIO_BLOCK_FRAMES * 2u) // stereo, one 32-bit word per slot
#define BLOCK_BYTES (BLOCK_WORDS * 4u)
#define RING_BITS 11u // log2(BLOCK_BYTES): the DMA read pointer wraps inside its own buffer, so a late
                      // restart replays the stale block instead of reading whatever follows it
_Static_assert(BLOCK_BYTES == (1u << RING_BITS), "DMA read ring must match the block size");

static PIO const g_pio = pio0;
static uint32_t g_dma_buf[2][BLOCK_WORDS] __attribute__((aligned(BLOCK_BYTES)));

static hal_audio_block_cb g_cb;
static volatile bool g_open;
static volatile bool g_muted = true;
static volatile int g_sm = -1;
static volatile int g_ch[2] = {-1, -1};
static uint g_offset;
static uint32_t g_next;     // index of the buffer whose completion we expect next
static uint32_t g_rate, g_clkdiv_x256;
static uint32_t g_rate_actual_mhz;
static volatile uint32_t g_late; // written only by the ISR
static int32_t *g_wbuf;     // buffer being filled by the callback
static uint32_t g_wpos;

static inline void mute_pin_set(bool mute) {
    bool high = mute ? (NEUPOD_AUDIO_UNMUTE_LEVEL == 0u) : (NEUPOD_AUDIO_UNMUTE_LEVEL != 0u);
    if (high) {
        sio_hw->gpio_set = 1u << NEUPOD_AUDIO_MUTE_PIN;
    } else {
        sio_hw->gpio_clr = 1u << NEUPOD_AUDIO_MUTE_PIN;
    }
}

hal_status_t hal_audio_init(void) {
    hal_audio_force_mute();
    return HAL_OK;
}

void hal_audio_force_mute(void) {
    // Pin first: the output stage must be muted before anything else is touched.
    gpio_init(NEUPOD_AUDIO_MUTE_PIN);
    mute_pin_set(true);
    gpio_set_dir(NEUPOD_AUDIO_MUTE_PIN, GPIO_OUT);
    g_muted = true;
    // If a stream was running (and core 1 has been reset under it) stop the hardware that feeds the DAC.
    int c0 = g_ch[0], c1 = g_ch[1], sm = g_sm;
    if (c0 >= 0 && c1 >= 0) {
        dma_hw->abort = (1u << c0) | (1u << c1);
    }
    if (sm >= 0) {
        pio_sm_set_enabled(g_pio, (uint)sm, false);
    }
}

bool hal_audio_rate_supported(uint32_t sample_rate) {
    return sample_rate == 44100u || sample_rate == 48000u || sample_rate == 88200u || sample_rate == 96000u;
}

void hal_audio_set_block_callback(hal_audio_block_cb cb) { g_cb = cb; }

// ---- DMA interrupt: a block has left the buffer, refill it ------------------------------------------------

static void __not_in_flash_func(refill)(uint idx, uint32_t missed) {
    g_wbuf = (int32_t *)g_dma_buf[idx];
    g_wpos = 0;
    if (g_cb != NULL) {
        g_cb(missed);
    }
    for (uint32_t i = g_wpos * 2u; i < BLOCK_WORDS; i++) {
        g_dma_buf[idx][i] = 0; // the callback wrote less than a block: the rest is silence
    }
    uint ch = (uint)g_ch[idx];
    dma_channel_set_read_addr(ch, g_dma_buf[idx], false);
    dma_channel_set_trans_count(ch, BLOCK_WORDS, false);
}

static void __isr __not_in_flash_func(dma_isr)(void) {
    const uint32_t m0 = 1u << (uint)g_ch[0], m1 = 1u << (uint)g_ch[1];
    uint32_t ints = dma_hw->ints1 & (m0 | m1);
    if (ints == 0) {
        return;
    }
    dma_hw->ints1 = ints; // acknowledge
    uint32_t missed = 0;
    if (ints == (m0 | m1)) {
        missed = 1; // both blocks finished before we ran: the first one was replayed
        g_late++;
    }
    if (!(ints & (1u << (uint)g_ch[g_next]))) {
        g_next ^= 1u; // resynchronise on whichever buffer actually completed
    }
    for (int n = 0; n < 2; n++) {
        uint idx = g_next;
        if (!(ints & (1u << (uint)g_ch[idx]))) {
            break;
        }
        if (dma_channel_is_busy((uint)g_ch[idx])) {
            g_late++; // already restarted with the stale block; leave it, it will interrupt again
            break;
        }
        refill(idx, missed);
        missed = 0;
        g_next ^= 1u;
    }
}

// ---- Open / close -----------------------------------------------------------------------------------------

hal_status_t hal_audio_open(const hal_audio_format *fmt) {
    if (fmt == NULL) {
        return HAL_EINVAL;
    }
    if (g_open) {
        return HAL_EBUSY;
    }
    if (!hal_audio_rate_supported(fmt->sample_rate) || fmt->channels < 1 || fmt->channels > 2) {
        return HAL_ENOTSUP;
    }
    hal_audio_force_mute();

    // Clock: PIO clock = 128 x Fs (two cycles per BCK, 64 BCK per frame); divider in 1/256 steps.
    const uint32_t sys = clock_get_hz(clk_sys);
    uint32_t div256 = (uint32_t)((((uint64_t)sys * 2u) + fmt->sample_rate / 2u) / fmt->sample_rate);
    if (div256 < 256u * 2u || div256 > 0xFFFFFFu) {
        return HAL_ENOTSUP;
    }

    int sm = pio_claim_unused_sm(g_pio, false);
    if (sm < 0) {
        return HAL_EBUSY;
    }
    if (!pio_can_add_program(g_pio, &neupod_i2s_program)) {
        pio_sm_unclaim(g_pio, (uint)sm);
        return HAL_EBUSY;
    }
    int c0 = dma_claim_unused_channel(false);
    int c1 = c0 >= 0 ? dma_claim_unused_channel(false) : -1;
    if (c0 < 0 || c1 < 0) {
        if (c0 >= 0) {
            dma_channel_unclaim((uint)c0);
        }
        pio_sm_unclaim(g_pio, (uint)sm);
        return HAL_EBUSY;
    }
    g_offset = pio_add_program(g_pio, &neupod_i2s_program);
    g_sm = sm;
    g_ch[0] = c0;
    g_ch[1] = c1;
    g_rate = fmt->sample_rate;
    g_clkdiv_x256 = div256;
    g_rate_actual_mhz = (uint32_t)(((uint64_t)sys * 2000u) / div256);
    g_late = 0;
    g_next = 0;
    for (uint b = 0; b < 2; b++) {
        for (uint32_t i = 0; i < BLOCK_WORDS; i++) {
            g_dma_buf[b][i] = 0; // start with silence in both buffers
        }
    }

    pio_sm_config pc = neupod_i2s_program_get_default_config(g_offset);
    sm_config_set_out_pins(&pc, PIN_DATA, 1);
    sm_config_set_sideset_pins(&pc, PIN_BCK);
    sm_config_set_out_shift(&pc, false, true, 32); // MSB first, autopull every 32 bits
    sm_config_set_fifo_join(&pc, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&pc, div256 >> 8, div256 & 0xFFu);
    pio_gpio_init(g_pio, PIN_DATA);
    pio_gpio_init(g_pio, PIN_BCK);
    pio_gpio_init(g_pio, PIN_LRCK);
    pio_sm_set_consecutive_pindirs(g_pio, (uint)sm, PIN_DATA, 1, true);
    pio_sm_set_consecutive_pindirs(g_pio, (uint)sm, PIN_BCK, 2, true);
    pio_sm_init(g_pio, (uint)sm, g_offset, &pc); // leaves the state machine disabled, pc at entry_point

    for (uint b = 0; b < 2; b++) {
        dma_channel_config dc = dma_channel_get_default_config((uint)g_ch[b]);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, true);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, pio_get_dreq(g_pio, (uint)sm, true));
        channel_config_set_ring(&dc, false, RING_BITS);
        channel_config_set_chain_to(&dc, (uint)g_ch[b ^ 1u]);
        channel_config_set_high_priority(&dc, true); // win bus arbitration against the display DMA
        dma_channel_configure((uint)g_ch[b], &dc, &g_pio->txf[sm], g_dma_buf[b], BLOCK_WORDS, false);
        dma_channel_set_irq1_enabled((uint)g_ch[b], true);
    }
    dma_hw->ints1 = (1u << (uint)c0) | (1u << (uint)c1);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_isr); // enabled on THIS core (the audio core)
    irq_set_enabled(DMA_IRQ_1, true);

    g_open = true;
    dma_channel_start((uint)c0);               // fills the FIFO with silence through the DREQ
    pio_sm_set_enabled(g_pio, (uint)sm, true); // the bit clocks start now; the pin stays muted
    return HAL_OK;
}

void hal_audio_close(void) {
    if (!g_open) {
        return;
    }
    hal_audio_force_mute();
    irq_set_enabled(DMA_IRQ_1, false);
    g_open = false;
    int c0 = g_ch[0], c1 = g_ch[1], sm = g_sm;
    dma_channel_set_irq1_enabled((uint)c0, false);
    dma_channel_set_irq1_enabled((uint)c1, false);
    irq_remove_handler(DMA_IRQ_1, dma_isr);
    dma_hw->ints1 = (1u << (uint)c0) | (1u << (uint)c1);
    g_ch[0] = g_ch[1] = -1;
    g_sm = -1;
    dma_channel_unclaim((uint)c0);
    dma_channel_unclaim((uint)c1);
    pio_remove_program(g_pio, &neupod_i2s_program, g_offset);
    pio_sm_unclaim(g_pio, (uint)sm);
    gpio_init(PIN_DATA); // pins back to idle inputs
    gpio_init(PIN_BCK);
    gpio_init(PIN_LRCK);
}

// ---- Block interface for the audio core --------------------------------------------------------------------

int32_t __not_in_flash_func(hal_audio_write)(const int32_t *samples, uint32_t frames) {
    uint32_t room = HAL_AUDIO_BLOCK_FRAMES - g_wpos;
    if (frames > room) {
        frames = room;
    }
    int32_t *d = g_wbuf + g_wpos * 2u;
    for (uint32_t i = 0; i < frames * 2u; i++) {
        d[i] = samples[i];
    }
    g_wpos += frames;
    return (int32_t)frames;
}

uint32_t __not_in_flash_func(hal_audio_free_frames)(void) { return HAL_AUDIO_BLOCK_FRAMES - g_wpos; }

uint32_t __not_in_flash_func(hal_audio_underruns)(void) { return g_late; }

void hal_audio_idle(void) { __wfi(); }

const char *hal_audio_backend_name(void) { return "i2s"; }

void __not_in_flash_func(hal_audio_mute)(bool mute) {
    if (!mute && !g_open) {
        return; // nothing is playing: stay muted
    }
    mute_pin_set(mute);
    g_muted = mute;
}

void hal_audio_get_info(hal_audio_info_t *out) {
    out->rate_hz = g_open ? g_rate : 0;
    out->rate_actual_mhz = g_open ? g_rate_actual_mhz : 0;
    out->clkdiv_x256 = g_open ? g_clkdiv_x256 : 0;
    out->muted = g_muted;
}

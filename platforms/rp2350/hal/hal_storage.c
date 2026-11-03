// SPDX-License-Identifier: MPL-2.0
/*
 * microSD over SDIO (PIO) via carlk3's no-OS-FatFS-SD-SDIO-SPI-RPi-Pico library. This file is
 * the only place that touches the library; everything above sees hal_storage.h.
 *
 * Write safety: the library's write_blocks hook is replaced by a guard that fails unless
 * hal_storage_set_write_enabled(true) was called, so neither raw writes nor FatFs (f_write,
 * f_mkfs, ...) can reach the card by accident. Identification and every read are read-only.
 */
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "hal_storage.h"
#include "hal_storage_decode.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hw_config.h"
#include "pico/stdlib.h"
#include "sd_card.h"
#include "SDIO/SdioCard.h"
#include "SDIO/rp2040_sdio.h"
#include "sdio_config.h"

// The library's PIO program takes 4 PIO cycles per SD clock (CLKDIV in rp2040_sdio.pio;
// platforms/rp2350/CMakeLists.txt verifies the value at configure time).
#define SDIO_PIO_CYCLES_PER_CLK 4u
#define SDIO_MIN_HZ 100000u // the identification clock is 400 kHz; below 100 kHz is pointless
#define SDIO_READ_CHUNK 128u // sectors per library call (its limit is 256)

static sd_card_t *g_sd;
static bool g_write_enabled;
static bool g_mounted;
static bool g_write_fault; // latched by a failed/suspicious write; cleared only by a successful init
static hal_card_info_t g_info;
static block_dev_err_t (*g_lib_write)(sd_card_t *, const uint8_t *, uint32_t, uint32_t);
static uint32_t g_bounce[HAL_STORAGE_SECTOR_SIZE / 4]; // aligned bounce buffer for odd callers

static bool initialised(void) { return g_sd != NULL && !(g_sd->state.m_Status & STA_NOINIT); }

// R1 card-status check (CMD13). Error bits: 31..19 (out of range ... ERROR), 16 (CSD overwrite),
// 3 (AKE sequence).
//
// Expected CURRENT_STATE after a write, from the library source (sd_card_sdio.c): a single-block
// write (CMD24) ends in "tran" (4) or briefly "prg" (7). A multi-block write (CMD25) is left OPEN
// on purpose: the library sets state.ongoing_wr_mlt_blk and keeps the card in "rcv" (6) so that a
// following write to the next sector continues without a new command. It sends CMD12 itself
// (sd_sdio_stopTransmission, which clears the flag) before any read, any single-block write, any
// non-sequential write and sync. So "rcv" is legitimate ONLY while ongoing_wr_mlt_blk is true, and
// then the card must also report READY_FOR_DATA (bit 8). "rcv" without the flag, or "data" (5),
// means the library left the card mid-command.
#define R1_ERROR_MASK 0xFFF90008u
#define R1_READY_FOR_DATA (1u << 8)
static bool r1_is_clean(uint32_t r1) {
    uint32_t state = (r1 >> 9) & 0xFu;
    if ((r1 & R1_ERROR_MASK) != 0) {
        return false;
    }
    if (state == 4u || state == 7u) {
        return true;
    }
    return state == 6u && g_sd != NULL && g_sd->sdio_if_p->state.ongoing_wr_mlt_blk &&
           (r1 & R1_READY_FOR_DATA) != 0;
}

static bool query_status(uint32_t *r1) {
    uint32_t reply = 0;
    sd_lock(g_sd);
    sdio_status_t s = rp2040_sdio_command_R1(g_sd, 13, g_sd->sdio_if_p->state.rca, &reply);
    sd_unlock(g_sd);
    if (r1 != NULL) {
        *r1 = reply;
    }
    return s == SDIO_OK;
}

static void sdio_reset_bus(void);
static float clk_div_for(uint32_t hz);

// Bring the card to rest: close an open multi-block write (CMD12 + wait for not-busy, done by the
// library's sync), then poll CMD13 until the card is in the transfer state with READY_FOR_DATA
// (up to 2 s; programming after a big write can take a while). Done at the CURRENT clock, before
// anything changes the clock or resets the card, so a half-finished write is never cut off
// mid-programming and the next command never meets a card that is still busy.
static bool quiesce_card(void) {
    if (g_sd == NULL || (g_sd->state.m_Status & STA_NOINIT)) {
        return false;
    }
    g_sd->sync(g_sd);
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    do {
        uint32_t r1 = 0;
        if (query_status(&r1) && (r1 & R1_ERROR_MASK) == 0 && ((r1 >> 9) & 0xFu) == 4u &&
            (r1 & R1_READY_FOR_DATA)) {
            return true;
        }
        sleep_ms(5);
    } while (to_ms_since_boot(get_absolute_time()) - t0 < 2000u);
    return false;
}

// Library write hook. The library ignores the card's R1 status on CMD24/25 and sends no CMD12
// after a failed CMD24, so a failed write can leave the card waiting for data and a later write
// (e.g. FatFs metadata) could land at the wrong address. Therefore: refuse unless writes are
// enabled; after ANY failure (or a bad status check after success) latch g_write_fault and refuse
// every further write until hal_storage_init() succeeds (its CMD0 resets the card).
static block_dev_err_t guarded_write(sd_card_t *sd, const uint8_t *buf, uint32_t sector,
                                     uint32_t count) {
    if (g_write_fault) {
        return SD_BLOCK_DEVICE_ERROR_WRITE;
    }
    if (!g_write_enabled) {
        return SD_BLOCK_DEVICE_ERROR_WRITE_PROTECTED;
    }
    // Second line of defence against the guard calling itself (see install_write_guard()).
    if (g_lib_write == NULL || g_lib_write == guarded_write) {
        return SD_BLOCK_DEVICE_ERROR_WRITE;
    }
    block_dev_err_t r = g_lib_write(sd, buf, sector, count);
    uint32_t r1 = 0;
    if (r != SD_BLOCK_DEVICE_ERROR_NONE) {
        g_write_fault = true;
        return r;
    }
    if (!query_status(&r1) || !r1_is_clean(r1)) {
        g_write_fault = true;
        printf("storage: card status after write is bad (R1 0x%08X): writes locked until re-init\n",
               (unsigned)r1);
        return SD_BLOCK_DEVICE_ERROR_WRITE;
    }
    return r;
}

// Library read hook wrapper. carlk3 v3.6.2 bug: rp2040_sdio.h declares
// dma_blocks[SDIO_MAX_BLOCKS * 2] (512) but rp2040_sdio_rx_start() writes its list terminator at
// dma_blocks[num_blocks * 2], i.e. one entry past the end for a 256-block read. That entry is
// received_checksums[0], which the DMA then overwrites, so the descriptor chain runs on into
// garbage and the driver wedges (bench 2026-11-02: first seen on exFAT with 128 KB clusters, where
// FatFs issues 256-sector reads; FAT32 with 2 KB clusters never asked for more than 4).
// Split every read into chunks well below the limit. Any read failure also latches
// g_write_fault: the driver may be left mid-transfer, so no write may follow until a re-init.
#define NEUPOD_SDIO_MAX_READ_BLOCKS 128u
static block_dev_err_t (*g_lib_read)(sd_card_t *, uint8_t *, uint32_t, uint32_t);
static block_dev_err_t guarded_read(sd_card_t *sd, uint8_t *buf, uint32_t sector, uint32_t count) {
    if (g_lib_read == NULL) {
        return SD_BLOCK_DEVICE_ERROR_NO_DEVICE;
    }
    while (count > 0) {
        uint32_t n = count > NEUPOD_SDIO_MAX_READ_BLOCKS ? NEUPOD_SDIO_MAX_READ_BLOCKS : count;
        block_dev_err_t r = g_lib_read(sd, buf, sector, n);
        if (r != SD_BLOCK_DEVICE_ERROR_NONE) {
            g_write_fault = true;
            return r;
        }
        buf += (size_t)n * HAL_STORAGE_SECTOR_SIZE;
        sector += n;
        count -= n;
    }
    return SD_BLOCK_DEVICE_ERROR_NONE;
}

// Idempotent: the library constructor (which sets write_blocks / read_blocks) runs once per boot,
// but this can be called any number of times. Saving a hook again after it was replaced would
// make the wrapper call itself forever.
static void install_write_guard(sd_card_t *sd) {
    if (sd->write_blocks != guarded_write) {
        g_lib_write = sd->write_blocks;
        sd->write_blocks = guarded_write;
    }
    if (sd->read_blocks != guarded_read) {
        g_lib_read = sd->read_blocks;
        sd->read_blocks = guarded_read;
    }
}

// Library state -> HAL status after a failed command sequence.
static hal_storage_status_t map_sdio_error(sdio_status_t e) {
    switch (e) {
    case SDIO_OK: return HAL_STORAGE_TIMEOUT; // sequence gave up without a bus error code
    case SDIO_ERR_RESPONSE_TIMEOUT:
    case SDIO_ERR_DATA_TIMEOUT: return HAL_STORAGE_NO_CARD;
    case SDIO_ERR_RESPONSE_CRC:
    case SDIO_ERR_DATA_CRC:
    case SDIO_ERR_WRITE_CRC: return HAL_STORAGE_CRC;
    default: return HAL_STORAGE_IO;
    }
}

static hal_storage_status_t map_block_err(block_dev_err_t e) {
    if (e == SD_BLOCK_DEVICE_ERROR_NONE) {
        return HAL_STORAGE_OK;
    }
    if (e == SD_BLOCK_DEVICE_ERROR_WRITE_PROTECTED) {
        return HAL_STORAGE_WRITE_PROTECT;
    }
    if (e == SD_BLOCK_DEVICE_ERROR_NO_DEVICE || e == SD_BLOCK_DEVICE_ERROR_NO_INIT) {
        return HAL_STORAGE_NO_CARD;
    }
    if (e == SD_BLOCK_DEVICE_ERROR_NO_RESPONSE) {
        return HAL_STORAGE_TIMEOUT;
    }
    if (e == SD_BLOCK_DEVICE_ERROR_CRC) {
        return HAL_STORAGE_CRC;
    }
    return HAL_STORAGE_IO;
}

// One-time driver bring-up. Does not talk to the card.
static hal_storage_status_t ensure_driver(void) {
    if (g_sd != NULL) {
        return HAL_STORAGE_OK;
    }
    sd_card_t *sd = sd_get_by_num(0);
    // The library wipes pio1's instruction memory and expects to own it.
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; sm++) {
        if (pio_sm_is_claimed(pio1, sm)) {
            return HAL_STORAGE_BUSY;
        }
    }
    bool ok = sd != NULL && sd_init_driver();
    if (sd != NULL) {
        install_write_guard(sd); // the library constructor may have run even if init failed
    }
    if (!ok) {
        return HAL_STORAGE_IO;
    }
    g_sd = sd;
    return HAL_STORAGE_OK;
}

// Clock divider exactly as the library computes it, quantised to the PIO's 16.8 fixed point.
static float clk_div_for(uint32_t hz) {
    float div = (float)clock_get_hz(clk_sys) / (float)(SDIO_PIO_CYCLES_PER_CLK * hz);
    uint32_t q = (uint32_t)(div * 256.0f + 0.5f);
    return (float)q / 256.0f;
}

static uint32_t actual_clock_hz(uint32_t requested_hz) {
    return (uint32_t)((float)clock_get_hz(clk_sys) /
                      ((float)SDIO_PIO_CYCLES_PER_CLK * clk_div_for(requested_hz)));
}

// A card that was in the middle of a transfer when the core was reset (a debugger reset after flashing) can sit in a data
// state and ignore the identification commands until it is power cycled. Without control of the card's power the best that
// can be done is what the specification allows in any state: a few STOP_TRANSMISSION (CMD12) commands, which end an open
// read or write-multiple and give the card well over 74 clocks with CMD high, then a pause so it can finish programming.
// CMD12 carries no data and is harmless to a card that is idle (it answers "illegal command"); nothing is written.
// Needs the bus set up by a first failed identification (the pins and the PIO are then already configured).
static void card_unwedge(void) {
    for (int i = 0; i < 4; i++) {
        sd_lock(g_sd);
        if (i == 0) {
            (void)rp2040_sdio_init(g_sd, clk_div_for(400u * 1000u)); // the identification clock
        }
        (void)rp2040_sdio_command_R1(g_sd, 12, 0, NULL);
        sd_unlock(g_sd);
        sleep_ms(5);
    }
    sleep_ms(50);
}

hal_storage_status_t hal_storage_init(void) {
    hal_storage_status_t st = ensure_driver();
    if (st != HAL_STORAGE_OK) {
        return st;
    }
    hal_storage_unmount();
    install_write_guard(g_sd); // cheap and idempotent; never lose the guard across re-inits
    if (initialised()) {
        // A card that was in use: let it finish whatever it was doing, and drop back to the
        // default clock while it is still in its current mode, BEFORE resetting it. (A card left in
        // High Speed mode, or still programming, answers the identification commands badly.)
        quiesce_card();
        g_sd->sdio_if_p->baud_rate = NEUPOD_SDIO_BAUD_HZ;
        sdio_reset_bus();
    }
    // Always back to the default data clock: a fresh card is in default speed, and recovery or
    // cleanup after a clock sweep must never run at a sweep clock.
    g_sd->sdio_if_p->baud_rate = NEUPOD_SDIO_BAUD_HZ;
    // Three attempts (a card that worked a moment ago, or one left mid-transfer by a reset); a missing card still fails
    // within about a second (each command times out after 10 ms).
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            printf("storage: card did not identify, wake-up attempt %d\n", attempt);
            card_unwedge();
            sleep_ms(50);
        }
        if (initialised()) {
            g_sd->deinit(g_sd); // forces a fresh identification below
        }
        // Forget any open multi-block write: CMD0 below resets the card, so continuing it would
        // send CMD12 to a fresh card.
        sd_sdio_if_state_t *ss = &g_sd->sdio_if_p->state;
        ss->ongoing_wr_mlt_blk = false;
        ss->transfer_state = SDIO_IDLE;
        ss->error = SDIO_OK;
        g_sd->init(g_sd);
        uint32_t r1 = 0;
        if (initialised() && query_status(&r1) && (r1 & R1_ERROR_MASK) == 0 &&
            ((r1 >> 9) & 0xFu) == 4u) {
            break; // identified and sitting in the transfer state
        }
        if (initialised()) {
            g_sd->state.m_Status |= STA_NOINIT; // not trustworthy: identify again
        }
    }
    if (!initialised()) {
        return map_sdio_error(g_sd->sdio_if_p->state.error);
    }

    memset(&g_info, 0, sizeof g_info);
    hal_storage_decode_cid(g_sd->state.CID, &g_info);
    if (!hal_storage_decode_csd(g_sd->state.CSD, &g_info)) {
        return HAL_STORAGE_UNSUPPORTED;
    }
    if (g_sd->state.sectors != 0) {
        g_info.sectors = g_sd->state.sectors; // the library's own count (agrees with CSD)
        g_info.capacity_bytes = (uint64_t)g_info.sectors * HAL_STORAGE_SECTOR_SIZE;
    }
    uint8_t ssr[64] __attribute__((aligned(4))); // DMA target
    sd_lock(g_sd);
    bool have_ssr = rp2040_sdio_get_sd_status(g_sd, ssr); // ACMD13: read-only
    sd_unlock(g_sd);
    if (have_ssr) {
        hal_storage_decode_ssr(ssr, &g_info);
    }
    g_info.bus_width = 4;
    g_info.bus_clock_hz = actual_clock_hz(g_sd->sdio_if_p->baud_rate);
    g_info.high_speed = false;
    g_write_fault = false; // identification (CMD0 ...) has reset the card
    return HAL_STORAGE_OK;
}

void hal_storage_deinit(void) {
    if (g_sd == NULL) {
        return;
    }
    hal_storage_unmount();
    if (initialised()) {
        g_sd->deinit(g_sd);
    }
}

bool hal_storage_ready(void) { return initialised(); }

bool hal_storage_card_present(void) {
    if (ensure_driver() != HAL_STORAGE_OK) {
        return false;
    }
    return g_sd->sd_test_com(g_sd);
}

hal_storage_status_t hal_storage_get_info(hal_card_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    *out = g_info;
    return HAL_STORAGE_OK;
}

uint32_t hal_storage_sector_count(void) { return initialised() ? g_info.sectors : 0; }

hal_storage_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count) {
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if ((uint64_t)lba + count > g_info.sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    uint8_t *dst = buf;
    bool aligned = ((uintptr_t)dst & 3u) == 0;
    while (count > 0) {
        uint32_t n = aligned ? (count < SDIO_READ_CHUNK ? count : SDIO_READ_CHUNK) : 1u;
        uint8_t *target = aligned ? dst : (uint8_t *)g_bounce;
        hal_storage_status_t st = map_block_err(g_sd->read_blocks(g_sd, target, lba, n));
        if (st != HAL_STORAGE_OK) {
            return st;
        }
        if (!aligned) {
            memcpy(dst, g_bounce, HAL_STORAGE_SECTOR_SIZE);
        }
        dst += (size_t)n * HAL_STORAGE_SECTOR_SIZE;
        lba += n;
        count -= n;
    }
    return HAL_STORAGE_OK;
}

void hal_storage_set_write_enabled(bool enabled) { g_write_enabled = enabled; }
bool hal_storage_get_write_enabled(void) { return g_write_enabled; }

hal_storage_status_t hal_storage_write(uint32_t lba, const void *buf, uint32_t count) {
    if (!g_write_enabled) {
        return HAL_STORAGE_WRITE_PROTECT; // checked first: nothing below may run while locked
    }
    if (g_write_fault) {
        return HAL_STORAGE_IO; // see guarded_write(): needs hal_storage_init()
    }
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if (g_info.write_protected) {
        return HAL_STORAGE_WRITE_PROTECT;
    }
    if ((uint64_t)lba + count > g_info.sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    const uint8_t *src = buf;
    bool aligned = ((uintptr_t)src & 3u) == 0;
    while (count > 0) {
        uint32_t n = aligned ? (count < SDIO_READ_CHUNK ? count : SDIO_READ_CHUNK) : 1u;
        const uint8_t *from = src;
        if (!aligned) {
            memcpy(g_bounce, src, HAL_STORAGE_SECTOR_SIZE);
            from = (const uint8_t *)g_bounce;
        }
        hal_storage_status_t st = map_block_err(g_sd->write_blocks(g_sd, from, lba, n));
        if (st != HAL_STORAGE_OK) {
            return st;
        }
        src += (size_t)n * HAL_STORAGE_SECTOR_SIZE;
        lba += n;
        count -= n;
    }
    return HAL_STORAGE_OK;
}

hal_storage_status_t hal_storage_sync(void) {
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    return map_block_err(g_sd->sync(g_sd));
}

hal_storage_status_t hal_storage_set_bus_clock(uint32_t hz) {
    uint32_t max_hz = clock_get_hz(clk_sys) / SDIO_PIO_CYCLES_PER_CLK;
    if (hz < SDIO_MIN_HZ || hz > max_hz) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (hz > NEUPOD_SDIO_DEFAULT_SPEED_MAX_HZ && !(initialised() && g_info.high_speed)) {
        return HAL_STORAGE_UNSUPPORTED; // out of spec unless the card is in High Speed mode
    }
    hal_storage_status_t st = ensure_driver();
    if (st != HAL_STORAGE_OK) {
        return st;
    }
    install_write_guard(g_sd);
    g_sd->sdio_if_p->baud_rate = hz;
    if (initialised()) {
        // The library's own "increase to the high clock" step after identification: reload the
        // PIO programs with the new divider. The card stays in transfer state.
        quiesce_card(); // at the OLD clock: close any open write, wait until not busy
        sd_lock(g_sd);
        bool ok = rp2040_sdio_init(g_sd, clk_div_for(hz));
        sd_unlock(g_sd);
        uint32_t r1 = 0;
        if (!ok || !query_status(&r1) || (r1 & R1_ERROR_MASK) != 0 || ((r1 >> 9) & 0xFu) != 4u) {
            g_sd->state.m_Status |= STA_NOINIT; // bus state unknown: require re-init
            return HAL_STORAGE_IO;
        }
        g_info.bus_clock_hz = actual_clock_hz(hz);
    }
    return HAL_STORAGE_OK;
}

// Re-run the library's SDIO setup at the current divider. This reloads the PIO programs and
// resets the state machines, which also aborts any half-finished data transfer.
static void sdio_reset_bus(void) {
    sd_lock(g_sd);
    rp2040_sdio_init(g_sd, clk_div_for(g_sd->sdio_if_p->baud_rate));
    sd_unlock(g_sd);
}

// CMD6 (SWITCH_FUNC) on the public low-level SDIO calls; the library itself never sends it.
// Same sequence as the library's SD Status read: arm the data receiver, send the command, poll.
// *r1 gets the command's R1 response.
static bool sdio_cmd6(uint32_t arg, uint32_t resp_words[16], uint32_t *r1) {
    if (rp2040_sdio_rx_start(g_sd, (uint8_t *)resp_words, 1, 64) != SDIO_OK ||
        rp2040_sdio_command_R1(g_sd, 6, arg, r1) != SDIO_OK) {
        sdio_reset_bus(); // receiver armed but no data coming: abort the DMA/PIO
        return false;
    }
    sdio_status_t s;
    do {
        s = rp2040_sdio_rx_poll(g_sd, 64 / 4);
    } while (s == SDIO_BUSY);
    if (s != SDIO_OK) {
        sdio_reset_bus();
        return false;
    }
    return true;
}

#define R1_SWITCH_ERROR (1u << 7)
#define R1_ILLEGAL_COMMAND (1u << 22)

hal_storage_status_t hal_storage_enable_high_speed(void) {
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if (g_info.high_speed) {
        return HAL_STORAGE_OK;
    }
    uint32_t words[16];
    uint8_t *st = (uint8_t *)words;
    uint32_t r1 = 0;
    hal_storage_status_t res = HAL_STORAGE_OK;
    bool bad_bus = false;
    sd_lock(g_sd);
    // Status block is 512 bits, byte 0 = bits 511:504. Group 1 support = bits 415:400 (bit 1 =
    // High Speed, in byte 13); selected function in group 1 = bits 379:376 (low nibble of byte 16).
    if (!sdio_cmd6(0x00FFFFF1u, words, &r1)) { // mode 0: check only
        res = HAL_STORAGE_IO;
    } else if (r1 & (R1_ILLEGAL_COMMAND | R1_SWITCH_ERROR)) {
        res = HAL_STORAGE_UNSUPPORTED;
    } else if (!(st[13] & 0x02u)) {
        res = HAL_STORAGE_UNSUPPORTED;
    } else if (!sdio_cmd6(0x80FFFFF1u, words, &r1)) { // mode 1: switch group 1 to function 1
        res = HAL_STORAGE_IO;
        bad_bus = true;
    } else if (r1 & (R1_ILLEGAL_COMMAND | R1_SWITCH_ERROR) || (st[16] & 0x0Fu) != 1u) {
        res = HAL_STORAGE_UNSUPPORTED; // card refused the switch
    }
    sd_unlock(g_sd);
    if (res == HAL_STORAGE_OK) {
        // Confirm the card is still healthy at the OLD clock before anything raises it.
        uint32_t st13 = 0;
        if (!query_status(&st13) || !r1_is_clean(st13)) {
            res = HAL_STORAGE_IO;
            bad_bus = true;
        }
    }
    if (bad_bus) {
        g_sd->state.m_Status |= STA_NOINIT; // card state unknown: require hal_storage_init()
        return res;
    }
    if (res == HAL_STORAGE_OK) {
        g_info.high_speed = true;
    }
    return res;
}

hal_storage_status_t hal_storage_card_status(uint32_t *r1_out) {
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    uint32_t r1 = 0;
    if (!query_status(&r1)) {
        return HAL_STORAGE_TIMEOUT;
    }
    if (r1_out != NULL) {
        *r1_out = r1;
    }
    return r1_is_clean(r1) ? HAL_STORAGE_OK : HAL_STORAGE_IO;
}

bool hal_storage_write_fault(void) { return g_write_fault; }

uint32_t hal_storage_get_bus_clock(void) {
    sd_card_t *sd = g_sd != NULL ? g_sd : sd_get_by_num(0);
    return sd != NULL ? actual_clock_hz(sd->sdio_if_p->baud_rate) : 0;
}

hal_storage_status_t hal_storage_mount(void) {
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    FRESULT fr = f_mount(&g_sd->state.fatfs, sd_get_drive_prefix(g_sd), 1); // reads only
    switch (fr) {
    case FR_OK:
        g_mounted = true;
        return HAL_STORAGE_OK;
    case FR_NO_FILESYSTEM: return HAL_STORAGE_NO_FILESYSTEM;
    case FR_NOT_READY: return HAL_STORAGE_NO_CARD;
    case FR_DISK_ERR: return HAL_STORAGE_IO;
    default: return HAL_STORAGE_IO;
    }
}

void hal_storage_unmount(void) {
    if (g_sd != NULL && g_mounted) {
        f_unmount(sd_get_drive_prefix(g_sd));
        g_mounted = false;
    }
}

bool hal_storage_is_mounted(void) { return g_mounted && initialised(); }

hal_fs_type_t hal_storage_fs_type(void) {
    if (!hal_storage_is_mounted()) {
        return HAL_FS_NONE;
    }
    switch (g_sd->state.fatfs.fs_type) {
    case FS_FAT12: return HAL_FS_FAT12;
    case FS_FAT16: return HAL_FS_FAT16;
    case FS_FAT32: return HAL_FS_FAT32;
    case FS_EXFAT: return HAL_FS_EXFAT;
    default: return HAL_FS_NONE;
    }
}

hal_storage_status_t hal_storage_volume_info(hal_volume_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!hal_storage_is_mounted()) {
        return HAL_STORAGE_NOT_MOUNTED;
    }
    memset(out, 0, sizeof *out);
    out->fs = hal_storage_fs_type();
    out->sectors_per_cluster = g_sd->state.fatfs.csize;
    out->cluster_bytes = out->sectors_per_cluster * HAL_STORAGE_SECTOR_SIZE;
    TCHAR label[36] = {0}; // FatFs needs at least 34 bytes
    DWORD vsn = 0;
    if (f_getlabel(sd_get_drive_prefix(g_sd), label, &vsn) != FR_OK) {
        return HAL_STORAGE_IO;
    }
    snprintf(out->label, sizeof out->label, "%s", label);
    out->serial = vsn;
    return HAL_STORAGE_OK;
}

// Work buffer for f_mkfs (it is also handed to the disk layer, so it must be word aligned).
static uint8_t g_mkfs_work[32 * 1024] __attribute__((aligned(4)));

// Logical drive 0 -> physical drive 0, partition 0 = auto-detect (first FAT/exFAT partition, or a
// card without partition table). hal_storage_format() sets 1 to make f_mkfs use partition 1 of
// the MBR it wrote. FatFs requires this table when FF_MULTI_PARTITION is on.
PARTITION VolToPart[FF_VOLUMES] = {{0, 0}};

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// MBR with one exFAT/NTFS-type (0x07) partition at start_lba. FatFs f_fdisk cannot place the
// partition (it always starts at sector 63), so the table is written by hand.
static void build_mbr(uint8_t *s, uint32_t start_lba, uint32_t sectors, uint32_t disk_id) {
    memset(s, 0, HAL_STORAGE_SECTOR_SIZE);
    put_le32(&s[440], disk_id);
    uint8_t *e = &s[446];
    e[0] = 0x00;                 // not bootable
    e[1] = 0xFE, e[2] = 0xFF, e[3] = 0xFF; // CHS start: "use LBA" marker
    e[4] = 0x07;                 // exFAT / NTFS / HPFS
    e[5] = 0xFE, e[6] = 0xFF, e[7] = 0xFF; // CHS end
    put_le32(&e[8], start_lba);
    put_le32(&e[12], sectors);
    s[510] = 0x55;
    s[511] = 0xAA;
}

hal_storage_status_t hal_storage_format(const char *label, const hal_format_opts_t *opts) {
    if (opts == NULL || !opts->erase_all_confirmed) {
        return HAL_STORAGE_BAD_PARAM; // the caller must state that it has the user's confirmation
    }
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if (g_info.write_protected) {
        return HAL_STORAGE_WRITE_PROTECT;
    }
    uint32_t cluster = opts->cluster_bytes ? opts->cluster_bytes : 131072u;
    uint32_t align = opts->align_sectors ? opts->align_sectors
                                         : (g_info.au_size_bytes ? g_info.au_size_bytes / HAL_STORAGE_SECTOR_SIZE
                                                                 : 8192u); // 4 MiB fallback
    if (cluster < HAL_STORAGE_SECTOR_SIZE || (cluster & (cluster - 1u)) != 0 ||
        (align & (align - 1u)) != 0 || g_info.sectors <= 2u * align) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (label == NULL || label[0] == '\0') {
        label = "NEUPOD";
    }
    hal_storage_unmount();
    bool prev_enabled = g_write_enabled;
    g_write_enabled = true; // unlocked only for the duration of this function
    hal_storage_status_t st = HAL_STORAGE_OK;

    // 1. MBR: one partition from sector `align`, length a whole number of allocation units.
    uint32_t part_sectors = (g_info.sectors - align) / align * align;
    build_mbr((uint8_t *)g_bounce, align, part_sectors, g_info.psn);
    st = hal_storage_write(0, g_bounce, 1);

    // 2. exFAT inside partition 1, data area aligned to the AU, 128 KB clusters.
    if (st == HAL_STORAGE_OK) {
        VolToPart[0].pt = 1;
        MKFS_PARM opt = {.fmt = FM_EXFAT, .n_fat = 1, .align = align, .n_root = 0,
                         .au_size = cluster};
        FRESULT fr = f_mkfs(sd_get_drive_prefix(g_sd), &opt, g_mkfs_work, sizeof g_mkfs_work);
        VolToPart[0].pt = 0;
        if (fr != FR_OK) {
            st = fr == FR_NOT_READY ? HAL_STORAGE_NO_CARD : HAL_STORAGE_IO;
        }
    }
    // 3. Label (needs a mounted volume), then leave unmounted.
    if (st == HAL_STORAGE_OK) {
        st = hal_storage_mount();
    }
    if (st == HAL_STORAGE_OK) {
        char path[48];
        snprintf(path, sizeof path, "%s%s", sd_get_drive_prefix(g_sd), label);
        st = f_setlabel(path) == FR_OK ? HAL_STORAGE_OK : HAL_STORAGE_IO;
    }
    hal_storage_unmount();
    g_write_enabled = prev_enabled;
    return st;
}

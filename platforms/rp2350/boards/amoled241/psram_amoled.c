// SPDX-License-Identifier: MPL-2.0
/*
 * PSRAM on the Waveshare RP2350-Touch-AMOLED-2.41 (8 MB APS6404L-class on QMI CS1 = GPIO47), UI-25
 * (specs/001-amoled-full-art, research R4 to R6; docs/rp2350-amoled241.md "PSRAM").
 *
 * The Pico SDK's hardware_psram brings the chip up in runtime_init (before main) at the SDK's boot clock, from the board header's
 * PICO_PSRAM_CS_PIN with PICO_AUTO_DETECT_PSRAM_SIZE (the chip's ID is read first). This file adds three things around it:
 *
 *  1. A warm-start reset, as a runtime_init step just before the SDK's: "quad mode off" and a reset on CS1, so the ID read works
 *     after a reset that left the chip in quad mode (a picotool reboot or the reset button do not power-cycle it). Taken from our
 *     bring-up probe (testbed/amoled241/bringup/src/psram.c), written from the RP2350 and APS6404L datasheets.
 *  2. psram_amoled_after_clock(), called by hal_system_init() right after the clock drops from 150 to 125 MHz: the SDK computed
 *     the QMI timing for 150 MHz, and its MAX_SELECT (in units of 64 system clocks) would hold CS low 9.2 us at 125 MHz, over the
 *     chip's 8 us limit. The timing is computed again for the real clock. Then the check (the chip answered, and its size covers
 *     the neupod section) and the zeroing of the section the NEUPOD_PSRAM_BSS variables live in (NOLOAD: nothing else clears it).
 *  3. hal_psram_ok() / hal_psram_reason() / hal_psram_print(): the runtime refuses to start when the check failed (runtime.c), so
 *     nothing ever touches PSRAM that is not there (an access would bus-fault).
 *
 * The runtime_init step lives in this file next to hal_psram_ok(), which the runtime references: the file is linked from a static
 * library, and an object with no referenced symbol would be dropped together with the step.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal_system.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/psram.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include "pico/runtime.h"

extern uint32_t __psram_start__, __psram_load_end__, __psram_end__; // the SDK's PSRAM sections (sections_psram.incl)

// ---- 1. warm-start reset (runs from RAM: flash XIP is unavailable while the QMI is in direct mode) ----------------------

#define CMD_RESET_EN 0x66
#define CMD_RESET 0x99
#define CMD_QUAD_OFF 0xF5
#define DIRECT_CLKDIV 30 // clk_sys / 30: far below any limit of the chip

static void __no_inline_not_in_flash_func(qmi_wait_idle)(void) {
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
    }
}

static void __no_inline_not_in_flash_func(qmi_command)(uint8_t c, bool quad) {
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint32_t w = c | QMI_DIRECT_TX_OE_BITS;
    if (quad) {
        w |= QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB;
    }
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_TXFULL_BITS) {
    }
    qmi_hw->direct_tx = w;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_RXEMPTY_BITS) {
    }
    (void)qmi_hw->direct_rx;
    qmi_wait_idle();
    qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
}

static void __no_inline_not_in_flash_func(psram_warm_reset_ram)(void) {
    qmi_hw->direct_csr = DIRECT_CLKDIV << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    qmi_wait_idle(); // let the last XIP access finish
    qmi_command(CMD_QUAD_OFF, true); // in case the chip was left in quad mode; ignored by a chip in SPI mode
    qmi_command(CMD_RESET_EN, false);
    qmi_command(CMD_RESET, false);
    uint32_t t0 = timer_hw->timerawl;
    while (timer_hw->timerawl - t0 < 100u) { // tRST: 50 ns; 100 us of margin, as the probe
    }
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);
}

static void psram_amoled_warm_reset(void) {
    gpio_set_function(PICO_PSRAM_CS_PIN, GPIO_FUNC_XIP_CS1);
    uint32_t irq = save_and_disable_interrupts();
    psram_warm_reset_ram();
    restore_interrupts(irq);
}
// "11070": after the clocks and the timer ("00500"), just before the SDK's PSRAM set-up (PICO_RUNTIME_INIT_PSRAM, "11080").
PICO_RUNTIME_INIT_FUNC_RUNTIME(psram_amoled_warm_reset, "11070");

// ---- 2. after the clock change: re-time, check, zero ---------------------------------------------------------------------

static bool g_ok;
static const char *g_reason = "not checked";

void psram_amoled_after_clock(void) {
#if NEUPOD_PSRAM_FORCE_FAIL
    g_reason = "forced"; // debug aid (-DNEUPOD_PSRAM_FORCE_FAIL=ON): exercises the failure path on a healthy board
    return;
#endif
    if (!psram_is_available()) {
        g_reason = "no chip";
        return;
    }
    uint32_t irq = save_and_disable_interrupts(); // core 1 is not running yet; nothing else uses the XIP now
    int r = psram_configure_params(PICO_DEFAULT_PSRAM_MAX_FREQ, PICO_DEFAULT_PSRAM_MAX_SELECT, PICO_DEFAULT_PSRAM_MIN_DESELECT);
    if (r == PICO_OK) {
        r = psram_reinitialize();
    }
    restore_interrupts(irq);
    if (r != PICO_OK) {
        g_reason = "timing";
        return;
    }
    uint8_t *start = (uint8_t *)&__psram_load_end__; // the uninitialised (NOLOAD) part; the initialised part is empty here
    uint8_t *end = (uint8_t *)&__psram_end__;
    if (end > start && !psram_check_address(end - 1)) {
        g_reason = "too small";
        return;
    }
    memset(start, 0, (size_t)(end - start)); // the NEUPOD_PSRAM_BSS statics start at zero, as C promises
    g_ok = true;
    g_reason = "";
}

// ---- 3. what the runtime asks ---------------------------------------------------------------------------------------------

bool hal_psram_ok(void) { return g_ok; }

const char *hal_psram_reason(void) { return g_reason; }

void hal_psram_print(void) {
    if (!g_ok) {
        printf("psram: not usable (%s)\n", g_reason);
        return;
    }
    uint32_t sys = clock_get_hz(clk_sys);
    uint32_t div = (qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB;
    size_t used = (size_t)((uint8_t *)&__psram_end__ - (uint8_t *)&__psram_start__);
    printf("psram: %u KB at %u kHz, neupod section %u KB\n", (unsigned)(psram_get_size() / 1024u),
           (unsigned)(div != 0u ? sys / div / 1000u : 0u), (unsigned)(used / 1024u));
}

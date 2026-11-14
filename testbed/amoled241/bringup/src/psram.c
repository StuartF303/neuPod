// SPDX-License-Identifier: MPL-2.0
// See psram.h. Everything that runs while the QMI is in direct mode must execute from RAM: flash XIP is unavailable then.
#include "psram.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/timer.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/sync.h"

// APS6404L limits relied on (from the datasheet as recalled, not re-checked against the PDF here):
//   fast quad read 0xEB / quad write 0x38 run up to 133 MHz; this probe keeps to 109 MHz (the 3.3 V figure the old header used);
//   tCEM (CS low, max) = 8 us; tCPH (CS high, min) = 18 ns.
#define PSRAM_MAX_SCK_HZ 109000000u
#define PSRAM_TCEM_NS    8000u
#define PSRAM_TCPH_NS    18u

#define CMD_RESET_EN 0x66
#define CMD_RESET    0x99
#define CMD_READ_ID  0x9F
#define CMD_QUAD_ON  0x35
#define CMD_QUAD_OFF 0xF5
#define CMD_QUAD_RD  0xEB
#define CMD_QUAD_WR  0x38

#define DIRECT_CLKDIV 30  // reset/ID/mode commands at clk_sys/30, far below any limit

static void __no_inline_not_in_flash_func(wait_idle)(void) {
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
    }
}

static void __no_inline_not_in_flash_func(cs)(bool on) {
    if (on) {
        qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    } else {
        wait_idle();
        qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    }
}

// One byte out (and one byte in) in direct mode; @p quad selects 4-bit width for the byte.
static uint8_t __no_inline_not_in_flash_func(xfer)(uint8_t b, bool quad) {
    uint32_t w = b | QMI_DIRECT_TX_OE_BITS;
    if (quad) w |= QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_TXFULL_BITS) {
    }
    qmi_hw->direct_tx = w;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_RXEMPTY_BITS) {
    }
    return (uint8_t)qmi_hw->direct_rx;
}

// A byte clocked in only (output disabled), single width.
static uint8_t __no_inline_not_in_flash_func(xfer_in)(void) {
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_TXFULL_BITS) {
    }
    qmi_hw->direct_tx = 0xFF;  // OE clear: the pins are inputs
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_RXEMPTY_BITS) {
    }
    return (uint8_t)qmi_hw->direct_rx;
}

static void __no_inline_not_in_flash_func(wait_us)(uint32_t us) {
    uint32_t t0 = timer_hw->timerawl;
    while (timer_hw->timerawl - t0 < us) {
    }
}

static void __no_inline_not_in_flash_func(command)(uint8_t c, bool quad) {
    cs(true);
    xfer(c, quad);
    cs(false);
}

// Direct-mode part: reset, read ID, enter quad mode. Returns false if no PSRAM answered.
static bool __no_inline_not_in_flash_func(chip_setup)(psram_info_t *o) {
    qmi_hw->direct_csr = DIRECT_CLKDIV << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    wait_idle();  // let the last XIP access finish before the first direct transfer

    command(CMD_QUAD_OFF, true);  // in case an earlier run left the chip in quad mode (a picotool reboot does not reset it)
    command(CMD_RESET_EN, false);
    command(CMD_RESET, false);
    wait_us(100);

    cs(true);
    xfer(CMD_READ_ID, false);
    for (int i = 0; i < 3; i++) xfer(0x00, false);  // 24-bit don't-care address
    o->mf = xfer_in();
    o->kgd = xfer_in();
    o->eid = xfer_in();
    cs(false);

    o->present = o->kgd == 0x5D;
    if (o->present) {
        command(CMD_QUAD_ON, false);
    }
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);
    return o->present;
}

void psram_init(uint cs_pin, psram_info_t *o) {
    *o = (psram_info_t){0};
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    uint32_t irq = save_and_disable_interrupts();
    bool ok = chip_setup(o);
    restore_interrupts(irq);
    if (!ok) return;

    // EID bits 7:5 are the density: 0 = 16 Mbit (2 MB), 1 = 32 Mbit (4 MB), 2 = 64 Mbit (8 MB).
    uint32_t d = o->eid >> 5;
    o->size = d <= 2 ? (2u << 20) << d : 0;

    // ---- QMI window 1 timing for the actual clk_sys ----
    uint64_t sys = clock_get_hz(clk_sys);
    uint32_t div = (uint32_t)((sys + PSRAM_MAX_SCK_HZ - 1) / PSRAM_MAX_SCK_HZ);
    if (div < 1) div = 1;
    o->clkdiv = div;
    o->sck_hz = (uint32_t)(sys / div);

    // MAX_SELECT is in units of 64 sys cycles and must keep CS low under tCEM; leave two units of margin for the transfer in flight.
    uint32_t max_sel = (uint32_t)(sys * PSRAM_TCEM_NS / 1000000000ull / 64);
    max_sel = max_sel > 3 ? max_sel - 2 : 1;
    if (max_sel > 63) max_sel = 63;
    // MIN_DESELECT in sys cycles: tCPH plus a little margin (25 ns).
    uint32_t min_des = (uint32_t)((sys * (PSRAM_TCPH_NS + 7) + 999999999ull) / 1000000000ull);
    if (min_des < 1) min_des = 1;
    if (min_des > 31) min_des = 31;
    // Read data is sampled later as the PSRAM clock rises: one extra sys cycle of delay above ~90 MHz. Only the <= 75 MHz setting
    // has been run on hardware; the pattern test in main.c catches a wrong value.
    uint32_t rxd = o->sck_hz > 90000000u ? 2 : 1;
    o->rxdelay = rxd;
    o->max_select = max_sel;
    o->min_deselect = min_des;

    qmi_hw->m[1].timing = QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |  // 1 KB pages: no burst across one
                          2u << QMI_M1_TIMING_SELECT_HOLD_LSB | 1u << QMI_M1_TIMING_COOLDOWN_LSB | rxd << QMI_M1_TIMING_RXDELAY_LSB |
                          max_sel << QMI_M1_TIMING_MAX_SELECT_LSB | min_des << QMI_M1_TIMING_MIN_DESELECT_LSB |
                          div << QMI_M1_TIMING_CLKDIV_LSB;

    // Quad everything: 8-bit command, 24-bit address, 24 dummy bits (6 clocks) on reads, quad data.
    qmi_hw->m[1].rfmt = QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB |
                        QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_RFMT_ADDR_WIDTH_LSB |
                        QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_SUFFIX_WIDTH_LSB |
                        QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_RFMT_DUMMY_WIDTH_LSB |
                        QMI_M1_RFMT_DATA_WIDTH_VALUE_Q << QMI_M1_RFMT_DATA_WIDTH_LSB |
                        QMI_M1_RFMT_PREFIX_LEN_VALUE_8 << QMI_M1_RFMT_PREFIX_LEN_LSB |
                        QMI_M1_RFMT_DUMMY_LEN_VALUE_24 << QMI_M1_RFMT_DUMMY_LEN_LSB |
                        QMI_M1_RFMT_SUFFIX_LEN_VALUE_NONE << QMI_M1_RFMT_SUFFIX_LEN_LSB;
    qmi_hw->m[1].rcmd = CMD_QUAD_RD << QMI_M1_RCMD_PREFIX_LSB;

    qmi_hw->m[1].wfmt = QMI_M1_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_PREFIX_WIDTH_LSB |
                        QMI_M1_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_WFMT_ADDR_WIDTH_LSB |
                        QMI_M1_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_SUFFIX_WIDTH_LSB |
                        QMI_M1_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_WFMT_DUMMY_WIDTH_LSB |
                        QMI_M1_WFMT_DATA_WIDTH_VALUE_Q << QMI_M1_WFMT_DATA_WIDTH_LSB |
                        QMI_M1_WFMT_PREFIX_LEN_VALUE_8 << QMI_M1_WFMT_PREFIX_LEN_LSB |
                        QMI_M1_WFMT_DUMMY_LEN_VALUE_NONE << QMI_M1_WFMT_DUMMY_LEN_LSB |
                        QMI_M1_WFMT_SUFFIX_LEN_VALUE_NONE << QMI_M1_WFMT_SUFFIX_LEN_LSB;
    qmi_hw->m[1].wcmd = CMD_QUAD_WR << QMI_M1_WCMD_PREFIX_LSB;

    xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;  // allow stores to window 1
}

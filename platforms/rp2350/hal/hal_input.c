// SPDX-License-Identifier: MPL-2.0
/*
 * Click-wheel receiver (RP2350): a PIO state machine samples the data line on the wheel's clock
 * and autopushes 32-bit words; this file drains them, recovers frame alignment, decodes, and
 * keeps the latest state. SKELETON: the pin mapping, sample edge and init-packet transmit are
 * unconfirmed hardware facts, marked TODO(hw).
 */
#include <stdio.h>

#include "clickwheel_frame.h"
#include "clickwheel_rx.pio.h"
#include "hal_input.h"
#include "hardware/gpio.h"
#include "hal_system.h"
#include "hardware/pio.h"

// ################################################################################################
// # TODO(hw): PLACEHOLDER PIN MAPPING.                                                          #
// # GP19, GP26, GP27 and GP28 are RESERVED for the click wheel, but which wheel line goes on     #
// # which pin is NOT CONFIRMED. The assignment below is arbitrary. Check against the wiring      #
// # before connecting a real wheel.                                                              #
// ################################################################################################
#define CLICKWHEEL_PIN_CLK 19      // TODO(hw): wheel clock (open drain, ~53 kHz)
#define CLICKWHEEL_PIN_DATA_IN 26  // TODO(hw): wheel -> host data line (which of SDO/SDI is unknown)
#define CLICKWHEEL_PIN_DATA_OUT 27 // TODO(hw): host -> wheel line for the init packet
#define CLICKWHEEL_PIN_AUX 28      // TODO(hw): the 4th wheel line (function to be confirmed)

// Pull-ups: the wheel side has 18 kOhm pull-ups per the protocol notes, but where they are
// fitted on this testbed is unconfirmed, so the RP2350's internal pull-ups are NOT enabled.
// TODO(hw): enable gpio_pull_up() on the open-drain lines if no external pull-ups are present.

// If no valid frame arrives for this long the wheel is treated as idle/absent and the reported
// state reverts to "nothing pressed, not touched".
#define CLICKWHEEL_STALE_MS 100u

static PIO g_pio;
static uint g_sm;
static uint g_offset;      // program load address (needed to reset the PC on recovery)
static uint32_t g_last_ms; // hal_millis() of the last valid frame
static bool g_started;
static clickwheel_sync g_sync;
static hal_wheel_state g_latest;

// TODO(hw): transmit CLICKWHEEL_INIT_PACKET. How the host clocks out the packet (which line,
// which clock, timing) is unknown, so nothing is sent yet.
static hal_status_t send_init_packet(void) {
    printf("input: TODO(hw) init packet 0x%08X not sent (transmit method unknown)\n",
           (unsigned)CLICKWHEEL_INIT_PACKET);
    return HAL_ENOTSUP;
}

hal_status_t hal_input_init(void) {
    if (g_started) {
        return HAL_OK;
    }
    uint offset; // no failure path exists after the claim below, so nothing to release
    if (!pio_claim_free_sm_and_add_program(&clickwheel_rx_program, &g_pio, &g_sm, &offset)) {
        return HAL_EBUSY;
    }

    pio_gpio_init(g_pio, CLICKWHEEL_PIN_CLK);
    pio_gpio_init(g_pio, CLICKWHEEL_PIN_DATA_IN);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, CLICKWHEEL_PIN_CLK, 1, false);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, CLICKWHEEL_PIN_DATA_IN, 1, false);

    pio_sm_config c = clickwheel_rx_program_get_default_config(offset);
    sm_config_set_in_pins(&c, CLICKWHEEL_PIN_CLK);
    sm_config_set_jmp_pin(&c, CLICKWHEEL_PIN_DATA_IN);
    sm_config_set_in_shift(&c, true, true, 32); // shift right (LSB first), autopush at 32
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(g_pio, g_sm, offset, &c);

    g_offset = offset;
    g_last_ms = hal_millis();
    clickwheel_sync_init(&g_sync);
    g_latest = (hal_wheel_state){0};
    pio_sm_set_enabled(g_pio, g_sm, true);
    g_started = true;

    if (send_init_packet() != HAL_OK) {
        printf("input: receiver running, but wheel was not initialised\n");
    }
    return HAL_OK;
}

bool hal_input_is_simulated(void) { return false; }

// The Pico 2 W bench rig has no Power key and no Hold switch: Hold is the console key 'l' there (docs/adr/0020-hold.md). The final board's
// slide switch is a GPIO still to be assigned (TODO(hw)); until then no switch is reported.
bool hal_input_power_key(void) { return false; }
bool hal_input_has_power_key(void) { return false; }
hal_hold_switch_t hal_input_hold_switch(void) { return HAL_HOLD_SWITCH_NONE; }

hal_status_t hal_input_reinit(void) {
    if (!g_started) {
        return HAL_ENODEV;
    }
    return send_init_packet();
}

// REQUIREMENT: call at least every ~4 ms. The 8-word RX FIFO (joined) fills in 8 x 32 clocks,
// i.e. ~4.8 ms at 53 kHz; if it overflows the state machine stalls mid-frame and bit
// alignment is lost. Recovery below: clear the FIFOs, restart the SM, jump back to the start of
// the program (restart does not reset the PC) and drop the software frame sync.
bool hal_input_poll(hal_wheel_state *out) {
    bool fresh = false;
    if (g_started) {
        uint32_t stall = 1u << (PIO_FDEBUG_RXSTALL_LSB + g_sm);
        if (g_pio->fdebug & stall) {
            g_pio->fdebug = stall;
            pio_sm_set_enabled(g_pio, g_sm, false);
            pio_sm_clear_fifos(g_pio, g_sm);
            pio_sm_restart(g_pio, g_sm);
            pio_sm_exec(g_pio, g_sm, pio_encode_jmp(g_offset));
            pio_sm_set_enabled(g_pio, g_sm, true);
            clickwheel_sync_init(&g_sync);
        }
        while (!pio_sm_is_rx_fifo_empty(g_pio, g_sm)) {
            uint32_t word = pio_sm_get(g_pio, g_sm);
            hal_wheel_state st;
            if (clickwheel_sync_push_word(&g_sync, word, &st) > 0) {
                g_latest = st;
                g_last_ms = hal_millis();
                fresh = true;
            }
        }
        if (!fresh && g_latest.valid && hal_millis() - g_last_ms > CLICKWHEEL_STALE_MS) {
            g_latest = (hal_wheel_state){.valid = true}; // stale: nothing pressed, not touched
        }
    }
    if (out != NULL) {
        *out = g_latest;
    }
    return fresh;
}

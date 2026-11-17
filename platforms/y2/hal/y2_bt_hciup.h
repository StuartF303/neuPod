// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_bt_hciup.h
 * @brief Bench switches that make the Y2's controller set-up match the stock MediaTek stack's, and the plan of commands they add at HCI-up.
 *
 * Three environment variables (in /etc/neupod-fw.env, like NEUPOD_BT_HCI_DUMP), both off unless set:
 *   NEUPOD_BT_INQ_TXPWR=<-70..20>  Write Inquiry Transmit Power Level (0x0C59) with this many dBm. Stock sends +6 before every inquiry.
 *   NEUPOD_BT_MTK_FC66=1           the MediaTek vendor command 0xFC66 with 00 10 40 9C (stock sends it once after HCI_Reset; meaning unknown).
 *   NEUPOD_BT_INQ_MODE=<0|1|2>     Write Inquiry Mode (0x0C45): 0 standard, 1 with RSSI, 2 with RSSI or extended inquiry response (EIR). BTstack's init sets 2;
 *                                  this overrides it after every HCI reset, to A/B whether the MT6582 drops AirPods' inquiry results in EIR mode.
 * Read Inquiry Response Transmit Power Level (0x0C58) is always sent, as a diagnostic.
 *
 * Pure (no BTstack, no I/O), so the parsing and the order of the commands are unit-tested on the host (tests/test_y2_bt_hciup.c).
 * The port (bt_port_stpbt.c) sends what y2_hciup_next() hands out, one command at a time.
 */
#ifndef NEUPOD_Y2_BT_HCIUP_H
#define NEUPOD_Y2_BT_HCIUP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Y2_INQ_TXPWR_MIN (-70)
#define Y2_INQ_TXPWR_MAX 20

typedef enum {
    Y2_INQ_TXPWR_UNSET = 0, /**< not set (or empty): do not send */
    Y2_INQ_TXPWR_OK,        /**< *out holds the value */
    Y2_INQ_TXPWR_INVALID,   /**< set, but not an integer in Y2_INQ_TXPWR_MIN..MAX: do not send */
} y2_inq_txpwr_result_t;

/** Parse NEUPOD_BT_INQ_TXPWR (@p s may be NULL). Plain decimal with an optional sign; no trailing characters. @p out is untouched unless OK. */
y2_inq_txpwr_result_t y2_bt_parse_inq_txpwr(const char *s, int8_t *out);

typedef enum {
    Y2_INQ_MODE_UNSET = 0, /**< not set (or empty): do not send */
    Y2_INQ_MODE_OK,        /**< *out holds 0, 1 or 2 */
    Y2_INQ_MODE_INVALID,   /**< set, but not exactly "0", "1" or "2": do not send */
} y2_inq_mode_result_t;

/** Parse NEUPOD_BT_INQ_MODE (@p s may be NULL). Exactly "0", "1" or "2". @p out is untouched unless OK. */
y2_inq_mode_result_t y2_bt_parse_inq_mode(const char *s, uint8_t *out);

/** NEUPOD_BT_MTK_FC66: true only for exactly "1". */
bool y2_bt_parse_fc66(const char *s);

typedef enum {
    Y2_HCIUP_FC66 = 0,    /**< MediaTek vendor command 0xFC66 */
    Y2_HCIUP_WRITE_INQ_MODE, /**< 0x0C45, Write Inquiry Mode */
    Y2_HCIUP_READ_TXPWR,  /**< 0x0C58, the controller's own value */
    Y2_HCIUP_WRITE_TXPWR, /**< 0x0C59 */
    Y2_HCIUP_READBACK,    /**< 0x0C58 again, after the write */
} y2_hciup_step_t;

typedef struct {
    y2_hciup_step_t step;
    uint16_t opcode;
    uint8_t len;
    uint8_t params[4];
} y2_hciup_cmd_t;

typedef struct {
    bool fc66;
    bool write_inq_mode;
    uint8_t inq_mode;
    bool write_txpwr;
    int8_t txpwr;
    uint8_t next; /**< internal: the next step to consider */
} y2_hciup_plan_t;

/** A plan for the given switches. */
void y2_hciup_plan_init(y2_hciup_plan_t *p, bool fc66, bool write_inq_mode, uint8_t inq_mode, bool write_txpwr, int8_t txpwr);

/** The next command to send, in order FC66, 0C45, 0C58, 0C59, 0C58 (skipping what the switches leave out). false: the plan is done. */
bool y2_hciup_next(y2_hciup_plan_t *p, y2_hciup_cmd_t *out);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_BT_HCIUP_H

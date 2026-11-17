// SPDX-License-Identifier: MPL-2.0
#include "y2_bt_hciup.h"

#include <stddef.h>
#include <string.h>

y2_inq_txpwr_result_t y2_bt_parse_inq_txpwr(const char *s, int8_t *out) {
    if (s == NULL || s[0] == '\0') {
        return Y2_INQ_TXPWR_UNSET;
    }
    const char *p = s;
    bool neg = false;
    if (*p == '-' || *p == '+') {
        neg = *p == '-';
        p++;
    }
    if (*p < '0' || *p > '9') {
        return Y2_INQ_TXPWR_INVALID;
    }
    int v = 0;
    for (; *p >= '0' && *p <= '9'; p++) {
        v = v * 10 + (*p - '0');
        if (v > 1000) {
            return Y2_INQ_TXPWR_INVALID; /* keep the arithmetic small; far out of range anyway */
        }
    }
    if (*p != '\0') {
        return Y2_INQ_TXPWR_INVALID;
    }
    if (neg) {
        v = -v;
    }
    if (v < Y2_INQ_TXPWR_MIN || v > Y2_INQ_TXPWR_MAX) {
        return Y2_INQ_TXPWR_INVALID;
    }
    *out = (int8_t)v;
    return Y2_INQ_TXPWR_OK;
}

y2_inq_mode_result_t y2_bt_parse_inq_mode(const char *s, uint8_t *out) {
    if (s == NULL || s[0] == '\0') {
        return Y2_INQ_MODE_UNSET;
    }
    if (s[0] < '0' || s[0] > '2' || s[1] != '\0') {
        return Y2_INQ_MODE_INVALID;
    }
    *out = (uint8_t)(s[0] - '0');
    return Y2_INQ_MODE_OK;
}

bool y2_bt_parse_fc66(const char *s) { return s != NULL && s[0] == '1' && s[1] == '\0'; }

y2_stock_init_result_t y2_bt_parse_stock_init(const char *s) {
    if (s == NULL || s[0] == '\0') {
        return Y2_STOCK_INIT_UNSET;
    }
    return s[0] == '1' && s[1] == '\0' ? Y2_STOCK_INIT_ON : Y2_STOCK_INIT_INVALID;
}

void y2_hciup_plan_init(y2_hciup_plan_t *p, bool fc66, bool write_inq_mode, uint8_t inq_mode, bool stock_init, bool write_txpwr, int8_t txpwr) {
    p->fc66 = fc66;
    p->write_inq_mode = write_inq_mode;
    p->inq_mode = inq_mode;
    p->stock_init = stock_init;
    p->write_txpwr = write_txpwr;
    p->txpwr = txpwr;
    p->next = 0;
}

bool y2_hciup_next(y2_hciup_plan_t *p, y2_hciup_cmd_t *out) {
    while (p->next <= (uint8_t)Y2_HCIUP_READBACK) {
        y2_hciup_step_t s = (y2_hciup_step_t)p->next++;
        out->step = s;
        out->len = 0;
        switch (s) {
        case Y2_HCIUP_FC66:
            if (!p->fc66) {
                continue;
            }
            out->opcode = 0xFC66;
            out->len = 4;
            out->params[0] = 0x00; /* the constant stock sends */
            out->params[1] = 0x10;
            out->params[2] = 0x40;
            out->params[3] = 0x9C;
            return true;
        case Y2_HCIUP_WRITE_INQ_MODE:
            if (!p->write_inq_mode) {
                continue;
            }
            out->opcode = 0x0C45;
            out->len = 1;
            out->params[0] = p->inq_mode;
            return true;
        case Y2_HCIUP_EVENT_MASK:
            if (!p->stock_init) {
                continue;
            }
            out->opcode = 0x0C01;
            out->len = 8;
            memset(out->params, 0xFF, 8); /* every event enabled, as stock does */
            return true;
        case Y2_HCIUP_LE_HOST:
            if (!p->stock_init) {
                continue;
            }
            out->opcode = 0x0C6D;
            out->len = 2;
            out->params[0] = 0x01; /* LE supported by the host */
            out->params[1] = 0x00; /* simultaneous LE: off */
            return true;
        case Y2_HCIUP_PAGE_TIMEOUT:
            if (!p->stock_init) {
                continue;
            }
            out->opcode = 0x0C18;
            out->len = 2;
            out->params[0] = 0x00; /* 0x3800 slots (8.96 s), little-endian */
            out->params[1] = 0x38;
            return true;
        case Y2_HCIUP_READ_TXPWR:
            out->opcode = 0x0C58;
            return true;
        case Y2_HCIUP_WRITE_TXPWR:
            if (!p->write_txpwr) {
                continue;
            }
            out->opcode = 0x0C59;
            out->len = 1;
            out->params[0] = (uint8_t)p->txpwr; /* signed dBm as one byte */
            return true;
        case Y2_HCIUP_READBACK:
            if (!p->write_txpwr) {
                continue;
            }
            out->opcode = 0x0C58;
            return true;
        }
    }
    return false;
}

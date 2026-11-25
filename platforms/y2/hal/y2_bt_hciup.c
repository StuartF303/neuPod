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

y2_switch_result_t y2_bt_parse_switch(const char *s) {
    if (s == NULL || s[0] == '\0') {
        return Y2_SWITCH_UNSET;
    }
    return s[0] == '1' && s[1] == '\0' ? Y2_SWITCH_ON : Y2_SWITCH_INVALID;
}

void y2_hciup_plan_init(y2_hciup_plan_t *p, bool fc66, bool write_inq_mode, uint8_t inq_mode, bool stock_init, bool fc54, bool le_scan, bool write_txpwr, int8_t txpwr) {
    p->fc66 = fc66;
    p->write_inq_mode = write_inq_mode;
    p->inq_mode = inq_mode;
    p->stock_init = stock_init;
    p->fc54 = fc54;
    p->le_scan = le_scan;
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
        case Y2_HCIUP_FC54:
            if (!p->fc54) {
                continue;
            }
            out->opcode = 0xFC54;
            out->len = 1;
            out->params[0] = 0x01; /* the constant stock sends */
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
        case Y2_HCIUP_LE_SCAN_PARAMS:
            if (!p->le_scan) {
                continue;
            }
            out->opcode = 0x200B;
            out->len = 7;
            out->params[0] = 0x01;                /* active scan */
            out->params[1] = out->params[3] = 0x40; /* interval and window 0x0140 (200 ms), little-endian */
            out->params[2] = out->params[4] = 0x01;
            out->params[5] = 0x00;                /* own address: public */
            out->params[6] = 0x00;                /* accept all advertisers */
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
        case Y2_HCIUP_LE_SCAN_DISABLE:
        case Y2_HCIUP_LE_SCAN_ENABLE:
            break; /* per-inquiry steps, not part of this plan */
        }
    }
    return false;
}

void y2_inqseq_init(y2_inqseq_t *s, y2_inqseq_kind_t kind, bool write_txpwr, int8_t txpwr) {
    s->kind = kind;
    s->write_txpwr = write_txpwr;
    s->txpwr = txpwr;
    s->next = 0;
}

bool y2_inqseq_next(y2_inqseq_t *s, y2_hciup_cmd_t *out) {
    /* BEFORE: 0 disable, 1 write tx power (if set), 2 enable.  AFTER: 0 disable. */
    while (s->next < (s->kind == Y2_INQSEQ_BEFORE ? 3u : 1u)) {
        uint8_t i = s->next++;
        out->len = 0;
        if (i == 0 || i == 2) {
            out->step = i == 0 ? Y2_HCIUP_LE_SCAN_DISABLE : Y2_HCIUP_LE_SCAN_ENABLE;
            out->opcode = 0x200C;
            out->len = 2;
            out->params[0] = i == 0 ? 0x00 : 0x01; /* scan enable */
            out->params[1] = i == 0 ? 0x00 : 0x01; /* filter duplicates (ignored when disabling) */
            return true;
        }
        if (!s->write_txpwr) {
            continue;
        }
        out->step = Y2_HCIUP_WRITE_TXPWR;
        out->opcode = 0x0C59;
        out->len = 1;
        out->params[0] = (uint8_t)s->txpwr;
        return true;
    }
    return false;
}

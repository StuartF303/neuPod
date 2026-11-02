// SPDX-License-Identifier: MPL-2.0
// Storage code shared by every HAL: status/type names and the SD register decoders.
#include <string.h>

#include "hal_storage_decode.h"

const char *hal_storage_status_str(hal_storage_status_t st) {
    switch (st) {
    case HAL_STORAGE_OK: return "OK";
    case HAL_STORAGE_NO_CARD: return "NO_CARD";
    case HAL_STORAGE_TIMEOUT: return "TIMEOUT";
    case HAL_STORAGE_CRC: return "CRC";
    case HAL_STORAGE_IO: return "IO";
    case HAL_STORAGE_NOT_INIT: return "NOT_INIT";
    case HAL_STORAGE_NOT_MOUNTED: return "NOT_MOUNTED";
    case HAL_STORAGE_NO_FILESYSTEM: return "NO_FILESYSTEM";
    case HAL_STORAGE_UNSUPPORTED: return "UNSUPPORTED";
    case HAL_STORAGE_BAD_PARAM: return "BAD_PARAM";
    case HAL_STORAGE_WRITE_PROTECT: return "WRITE_PROTECT";
    case HAL_STORAGE_BUSY: return "BUSY";
    }
    return "?";
}

const char *hal_storage_card_type_str(hal_card_type_t t) {
    switch (t) {
    case HAL_CARD_NONE: return "none";
    case HAL_CARD_SDSC: return "SDSC";
    case HAL_CARD_SDHC: return "SDHC";
    case HAL_CARD_SDXC: return "SDXC";
    case HAL_CARD_IMAGE: return "image";
    }
    return "?";
}

// Bits msb..lsb (inclusive, msb >= lsb, at most 32 wide) of an n-byte big-endian register whose
// byte 0 holds the top eight bits.
static uint32_t bits(const uint8_t *reg, unsigned nbytes, unsigned msb, unsigned lsb) {
    uint32_t v = 0;
    for (unsigned b = msb + 1; b-- > lsb;) {
        unsigned idx = nbytes * 8u - 1u - b; // bit position counted from the top
        v = (v << 1) | ((reg[idx / 8u] >> (7u - idx % 8u)) & 1u);
    }
    return v;
}

void hal_storage_decode_cid(const uint8_t cid[16], hal_card_info_t *info) {
    info->mid = (uint8_t)bits(cid, 16, 127, 120);
    info->oid[0] = (char)cid[1];
    info->oid[1] = (char)cid[2];
    info->oid[2] = '\0';
    memcpy(info->pnm, &cid[3], 5);
    info->pnm[5] = '\0';
    info->prv_major = (uint8_t)bits(cid, 16, 63, 60);
    info->prv_minor = (uint8_t)bits(cid, 16, 59, 56);
    info->psn = bits(cid, 16, 55, 24);
    info->mdt_year = (uint16_t)(2000u + bits(cid, 16, 19, 12));
    info->mdt_month = (uint8_t)bits(cid, 16, 11, 8);
}

bool hal_storage_decode_csd(const uint8_t csd[16], hal_card_info_t *info) {
    static const uint32_t unit_hz[4] = {100000u, 1000000u, 10000000u, 100000000u};
    static const uint8_t value_x10[16] = {0, 10, 12, 13, 15, 20, 25, 30,
                                          35, 40, 45, 50, 55, 60, 70, 80};
    info->csd_version = (uint8_t)bits(csd, 16, 127, 126);
    info->tran_speed_raw = csd[3];
    unsigned unit = info->tran_speed_raw & 7u;
    info->max_bus_hz = unit < 4 ? unit_hz[unit] * value_x10[(info->tran_speed_raw >> 3) & 0xFu] / 10u : 0;
    info->write_protected = bits(csd, 16, 13, 12) != 0;

    uint64_t sectors = 0;
    if (info->csd_version == 0) {
        uint32_t c_size = bits(csd, 16, 73, 62);
        uint32_t mult = bits(csd, 16, 49, 47);
        uint32_t read_bl_len = bits(csd, 16, 83, 80);
        uint64_t bytes = ((uint64_t)c_size + 1u) << (mult + 2u);
        bytes <<= read_bl_len;
        sectors = bytes / HAL_STORAGE_SECTOR_SIZE;
        info->type = HAL_CARD_SDSC;
    } else if (info->csd_version == 1) {
        sectors = ((uint64_t)bits(csd, 16, 69, 48) + 1u) * 1024u;
        // 32 GiB = 67108864 sectors; above that the card is SDXC.
        info->type = sectors > 67108864u ? HAL_CARD_SDXC : HAL_CARD_SDHC;
    } else {
        info->sectors = 0;
        info->capacity_bytes = 0;
        return false; // CSD v3 (SDUC) is not supported
    }
    info->sectors = sectors > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)sectors;
    info->capacity_bytes = (uint64_t)info->sectors * HAL_STORAGE_SECTOR_SIZE;
    return true;
}

void hal_storage_decode_ssr(const uint8_t ssr[64], hal_card_info_t *info) {
    static const uint8_t speed_class[5] = {0, 2, 4, 6, 10};
    static const uint32_t au_kib[16] = {0,    16,   32,   64,   128,  256,  512,   1024,
                                        2048, 4096, 8192, 12288, 16384, 24576, 32768, 65536};
    unsigned sc = bits(ssr, 64, 447, 440);
    info->speed_class = sc < 5 ? speed_class[sc] : 0;
    info->au_size_bytes = au_kib[bits(ssr, 64, 431, 428)] * 1024u;
    info->uhs_speed_grade = (uint8_t)bits(ssr, 64, 399, 396);
    info->video_speed_class = (uint8_t)bits(ssr, 64, 391, 384);
    info->ssr_valid = true;
}

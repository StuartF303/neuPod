// SPDX-License-Identifier: MPL-2.0
// SD register decoders and the host storage HAL's write lock.
#include <string.h>

#include "hal_storage.h"
#include "hal_storage_decode.h"
#include "unit.h"

TEST(test_cid) {
    // MID 0x03, OID "SD", PNM "SL16G", PRV 8.0, PSN 0x12345678, date 2021-05
    static const uint8_t cid[16] = {0x03, 'S', 'D', 'S', 'L', '1', '6', 'G',
                                    0x80, 0x12, 0x34, 0x56, 0x78, 0x01, 0x55, 0x01};
    hal_card_info_t ci;
    memset(&ci, 0, sizeof ci);
    hal_storage_decode_cid(cid, &ci);
    ASSERT_EQ(0x03, ci.mid);
    ASSERT_EQ(0, strcmp(ci.oid, "SD"));
    ASSERT_EQ(0, strcmp(ci.pnm, "SL16G"));
    ASSERT_EQ(8, ci.prv_major);
    ASSERT_EQ(0, ci.prv_minor);
    ASSERT_EQ(0x12345678, ci.psn);
    ASSERT_EQ(2021, ci.mdt_year);
    ASSERT_EQ(5, ci.mdt_month);
}

TEST(test_csd_v2_sdhc) {
    uint8_t csd[16] = {0x40, 0x0E, 0x00, 0x32, 0x5B, 0x59, 0x00, 0x00,
                       0xEE, 0x7F, 0x7F, 0x80, 0x0A, 0x40, 0x00, 0x01};
    hal_card_info_t ci;
    memset(&ci, 0, sizeof ci);
    ASSERT_TRUE(hal_storage_decode_csd(csd, &ci));
    ASSERT_EQ(1, ci.csd_version);
    ASSERT_EQ(0x32, ci.tran_speed_raw);
    ASSERT_EQ(25000000, ci.max_bus_hz);
    ASSERT_EQ((0xEE7Fu + 1u) * 1024u, ci.sectors); // C_SIZE = 0x00EE7F
    ASSERT_EQ(HAL_CARD_SDHC, ci.type);
    ASSERT_FALSE(ci.write_protected);
    csd[14] |= 0x10; // TMP_WRITE_PROTECT
    ASSERT_TRUE(hal_storage_decode_csd(csd, &ci));
    ASSERT_TRUE(ci.write_protected);
    csd[7] = 0x3F; // C_SIZE high bits -> > 32 GiB
    ASSERT_TRUE(hal_storage_decode_csd(csd, &ci));
    ASSERT_EQ(HAL_CARD_SDXC, ci.type);
}

TEST(test_csd_v1_sdsc_and_speeds) {
    // CSD v1.0: READ_BL_LEN=9, C_SIZE=0xFFF, C_SIZE_MULT=7 -> 4096 * 512 blocks * 512 B = 1 GiB
    uint8_t csd[16] = {0};
    csd[0] = 0x00;
    csd[3] = 0x5A; // 50 MHz
    csd[5] = 0x09; // READ_BL_LEN [83:80]
    csd[6] = 0x03; // C_SIZE [73:72]
    csd[7] = 0xFF; // C_SIZE [71:64]
    csd[8] = 0xC0; // C_SIZE [63:62]
    csd[9] = 0x03; // C_SIZE_MULT [49:47] high bits
    csd[10] = 0x80; // C_SIZE_MULT low bit
    hal_card_info_t ci;
    memset(&ci, 0, sizeof ci);
    ASSERT_TRUE(hal_storage_decode_csd(csd, &ci));
    ASSERT_EQ(HAL_CARD_SDSC, ci.type);
    ASSERT_EQ(50000000, ci.max_bus_hz);
    ASSERT_EQ(4096u * 512u, ci.sectors);
    csd[3] = 0x0B;
    hal_storage_decode_csd(csd, &ci);
    ASSERT_EQ(100000000, ci.max_bus_hz);
    csd[3] = 0x2B;
    hal_storage_decode_csd(csd, &ci);
    ASSERT_EQ(200000000, ci.max_bus_hz);
    csd[0] = 0xC0; // CSD structure 3: unsupported
    ASSERT_FALSE(hal_storage_decode_csd(csd, &ci));
    ASSERT_EQ(0, ci.sectors);
}

TEST(test_ssr) {
    uint8_t ssr[64] = {0};
    hal_card_info_t ci;
    memset(&ci, 0, sizeof ci);
    // Bit 447 is at index 511-447 = 64 -> byte 8; AU_SIZE [431:428] -> index 80..83 -> byte 10 high nibble.
    memset(ssr, 0, sizeof ssr);
    ssr[8] = 0x04;  // speed class code 4 -> Class 10
    ssr[10] = 0x90; // AU_SIZE = 9 -> 4 MiB
    ssr[14] = 0x30; // UHS_SPEED_GRADE [399:396] -> index 112..115 -> byte 14 high nibble = 3
    ssr[15] = 0x0A; // VIDEO_SPEED_CLASS [391:384] -> byte 15 = 10
    hal_storage_decode_ssr(ssr, &ci);
    ASSERT_TRUE(ci.ssr_valid);
    ASSERT_EQ(10, ci.speed_class);
    ASSERT_EQ(4u * 1024u * 1024u, ci.au_size_bytes);
    ASSERT_EQ(3, ci.uhs_speed_grade);
    ASSERT_EQ(10, ci.video_speed_class);
}

TEST(test_status_strings_and_write_lock) {
    ASSERT_EQ(0, strcmp("OK", hal_storage_status_str(HAL_STORAGE_OK)));
    ASSERT_EQ(0, strcmp("NO_CARD", hal_storage_status_str(HAL_STORAGE_NO_CARD)));
    ASSERT_EQ(0, strcmp("SDXC", hal_storage_card_type_str(HAL_CARD_SDXC)));
    // Writes are locked by default, even before init (nothing reaches the medium).
    static uint8_t sector[HAL_STORAGE_SECTOR_SIZE];
    ASSERT_FALSE(hal_storage_get_write_enabled());
    ASSERT_EQ(HAL_STORAGE_WRITE_PROTECT, hal_storage_write(0, sector, 1));
    ASSERT_EQ(HAL_STORAGE_NOT_INIT, hal_storage_read(0, sector, 1));
}

int main(void) {
    RUN(test_cid);
    RUN(test_csd_v2_sdhc);
    RUN(test_csd_v1_sdsc_and_speeds);
    RUN(test_ssr);
    RUN(test_status_strings_and_write_lock);
    return RESULT();
}

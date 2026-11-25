// SPDX-License-Identifier: MPL-2.0
// Golden bytes for the MediaTek vendor init script. Commands are as BTstack hands them to the transport:
// opcode (LE), parameter length, parameters (the H4 type byte is added by the transport).
#include <string.h>

#include "btstack_config.h"
#include "btstack_util.h"

#include "btstack_chipset_mtk_consys.h"

#include "../../../../../tests/unit.h"

// 11:22:33:44:55:66 in BTstack order (MSB first); the chip wants it LSB first.
static const bd_addr_t addr = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

static const uint8_t g_fc1a[] = { 0x1a, 0xfc, 0x06, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
static const uint8_t g_fc79[] = { 0x79, 0xfc, 0x06, 0x07, 0x80, 0x00, 0x06, 0x05, 0x07 };
static const uint8_t g_fc93[] = { 0x93, 0xfc, 0x03, 0xff, 0xff, 0xff };
static const uint8_t g_fc7a[] = { 0x7a, 0xfc, 0x07, 0x03, 0x40, 0x1f, 0x40, 0x1f, 0x00, 0x04 };

#define ASSERT_CMD(res_expected, golden)                                                          \
    do {                                                                                          \
        memset(buf, 0xaa, sizeof(buf));                                                           \
        btstack_chipset_result_t r_ = chipset->next_command(buf);                                 \
        ASSERT_EQ(res_expected, r_);                                                              \
        if (r_ == BTSTACK_CHIPSET_VALID_COMMAND) {                                                \
            ASSERT_TRUE(memcmp(buf, golden, sizeof(golden)) == 0);                                \
            ASSERT_EQ(0xaa, buf[sizeof(golden)]); /* nothing written past the command */         \
        }                                                                                         \
    } while (0)

// Must run first: no address has been set yet, so FC1A is skipped.
TEST(script_without_address){
    const btstack_chipset_t * chipset = btstack_chipset_mtk_consys_instance();
    uint8_t buf[32];
    chipset->init(NULL);
    ASSERT_CMD(BTSTACK_CHIPSET_VALID_COMMAND, g_fc79);
    ASSERT_CMD(BTSTACK_CHIPSET_VALID_COMMAND, g_fc93);
    ASSERT_CMD(BTSTACK_CHIPSET_VALID_COMMAND, g_fc7a);
    ASSERT_CMD(BTSTACK_CHIPSET_DONE, g_fc7a);
}

TEST(script_with_address){
    const btstack_chipset_t * chipset = btstack_chipset_mtk_consys_instance();
    uint8_t buf[32];
    btstack_chipset_mtk_consys_set_bd_addr(addr);
    chipset->init(NULL);
    ASSERT_CMD(BTSTACK_CHIPSET_VALID_COMMAND, g_fc1a); // address first, before HCI_Reset
    ASSERT_CMD(BTSTACK_CHIPSET_VALID_COMMAND, g_fc79);
    ASSERT_CMD(BTSTACK_CHIPSET_VALID_COMMAND, g_fc93);
    ASSERT_CMD(BTSTACK_CHIPSET_VALID_COMMAND, g_fc7a);
    ASSERT_CMD(BTSTACK_CHIPSET_DONE, g_fc7a);
    ASSERT_CMD(BTSTACK_CHIPSET_DONE, g_fc7a); // stays done
}

TEST(init_restarts_the_script){
    const btstack_chipset_t * chipset = btstack_chipset_mtk_consys_instance();
    uint8_t buf[32];
    chipset->init(NULL);
    ASSERT_CMD(BTSTACK_CHIPSET_VALID_COMMAND, g_fc1a);
}

TEST(set_bd_addr_command_matches){
    const btstack_chipset_t * chipset = btstack_chipset_mtk_consys_instance();
    uint8_t buf[32];
    bd_addr_t a;
    memcpy(a, addr, sizeof(a));
    memset(buf, 0xaa, sizeof(buf));
    chipset->set_bd_addr_command(a, buf);
    ASSERT_TRUE(memcmp(buf, g_fc1a, sizeof(g_fc1a)) == 0);
    ASSERT_EQ(0xaa, buf[sizeof(g_fc1a)]);
}

TEST(no_baudrate_command){
    ASSERT_TRUE(btstack_chipset_mtk_consys_instance()->set_baudrate_command == NULL);
}

int main(void){
    RUN(script_without_address);
    RUN(script_with_address);
    RUN(init_restarts_the_script);
    RUN(set_bd_addr_command_matches);
    RUN(no_baudrate_command);
    return RESULT();
}

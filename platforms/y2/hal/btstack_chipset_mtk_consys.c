// SPDX-License-Identifier: MPL-2.0
#define BTSTACK_FILE__ "btstack_chipset_mtk_consys.c"

#include "btstack_chipset_mtk_consys.h"

#include <string.h>

#include "btstack_util.h"

#define OPCODE_SET_BD_ADDR       0xfc1a
#define OPCODE_SET_RADIO         0xfc79
#define OPCODE_SET_TX_POWER_OFFS 0xfc93
#define OPCODE_SET_SLEEP_TIMEOUT 0xfc7a

// Fixed script (the chip's stock defaults; not per-unit calibration).
static const uint8_t radio_params[]    = { 0x07, 0x80, 0x00, 0x06, 0x05, 0x07 };
static const uint8_t tx_power_params[] = { 0xff, 0xff, 0xff };
static const uint8_t sleep_params[]    = { 0x03, 0x40, 0x1f, 0x40, 0x1f, 0x00, 0x04 };

static bd_addr_t bd_addr;
static bool      bd_addr_valid;
static uint8_t   script_index;

static void put_cmd(uint8_t * buf, uint16_t opcode, const uint8_t * params, uint8_t len){
    little_endian_store_16(buf, 0, opcode);
    buf[2] = len;
    memcpy(&buf[3], params, len);
}

static void chipset_init(const void * config){
    (void) config;
    script_index = 0;
}

static void chipset_set_bd_addr_command(bd_addr_t addr, uint8_t * hci_cmd_buffer){
    uint8_t reversed[6]; // the chip wants the address LSB first
    reverse_bd_addr(addr, reversed);
    put_cmd(hci_cmd_buffer, OPCODE_SET_BD_ADDR, reversed, sizeof(reversed));
}

static btstack_chipset_result_t chipset_next_command(uint8_t * hci_cmd_buffer){
    // step 0 is FC1A when an address is set; steps 1..3 are the fixed commands
    for (;;){
        switch (script_index){
            case 0:
                script_index++;
                if (!bd_addr_valid) continue;
                chipset_set_bd_addr_command(bd_addr, hci_cmd_buffer);
                return BTSTACK_CHIPSET_VALID_COMMAND;
            case 1:
                script_index++;
                put_cmd(hci_cmd_buffer, OPCODE_SET_RADIO, radio_params, sizeof(radio_params));
                return BTSTACK_CHIPSET_VALID_COMMAND;
            case 2:
                script_index++;
                put_cmd(hci_cmd_buffer, OPCODE_SET_TX_POWER_OFFS, tx_power_params, sizeof(tx_power_params));
                return BTSTACK_CHIPSET_VALID_COMMAND;
            case 3:
                script_index++;
                put_cmd(hci_cmd_buffer, OPCODE_SET_SLEEP_TIMEOUT, sleep_params, sizeof(sleep_params));
                return BTSTACK_CHIPSET_VALID_COMMAND;
            default:
                return BTSTACK_CHIPSET_DONE;
        }
    }
}

static const btstack_chipset_t chipset_mtk_consys = {
    "MediaTek CONSYS",
    chipset_init,
    chipset_next_command,
    NULL, // no UART baud rate: /dev/stpbt is not a serial port
    chipset_set_bd_addr_command,
};

const btstack_chipset_t * btstack_chipset_mtk_consys_instance(void){
    return &chipset_mtk_consys;
}

void btstack_chipset_mtk_consys_set_bd_addr(const bd_addr_t addr){
    memcpy(bd_addr, addr, sizeof(bd_addr_t));
    bd_addr_valid = true;
}

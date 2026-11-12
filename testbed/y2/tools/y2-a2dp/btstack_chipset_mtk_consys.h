// SPDX-License-Identifier: MPL-2.0
/*
 * BTstack chipset driver for the MediaTek MT6582 combo chip (CONSYS) behind /dev/stpbt.
 *
 * The kernel driver powers the chip and loads its patch; what userspace still owes is a short vendor script that has
 * to run BEFORE HCI_Reset (so main.c calls hci_enable_custom_pre_init()):
 *   FC1A Set_Local_BD_Addr  (only when an address was given with btstack_chipset_mtk_consys_set_bd_addr)
 *   FC79 Set_Radio, FC93 Set_TX_Power_Offset, FC7A Set_Sleep_Timeout  (fixed MediaTek defaults)
 */
#ifndef BTSTACK_CHIPSET_MTK_CONSYS_H
#define BTSTACK_CHIPSET_MTK_CONSYS_H

#include "btstack_chipset.h"

#if defined __cplusplus
extern "C" {
#endif

const btstack_chipset_t * btstack_chipset_mtk_consys_instance(void);

// Address (BTstack order, MSB first) that the pre-init script programs with FC1A. Call before hci_power_control().
void btstack_chipset_mtk_consys_set_bd_addr(const bd_addr_t addr);

#if defined __cplusplus
}
#endif

#endif

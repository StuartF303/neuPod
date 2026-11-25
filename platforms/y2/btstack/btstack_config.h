// SPDX-License-Identifier: MPL-2.0
/*
 * BTstack configuration for the Y2 firmware (docs/bluetooth.md): Classic Bluetooth only (device discovery, pairing and the link now,
 * A2DP source + AVRCP later), the POSIX run loop on its own thread, malloc-backed pools (the Y2 has the memory). No BLE, no SCO.
 * The y2-a2dp tool has its own copy (testbed/y2/tools/y2-a2dp/btstack_config.h).
 */
#ifndef NEUPOD_Y2_BTSTACK_CONFIG_H
#define NEUPOD_Y2_BTSTACK_CONFIG_H

#define HAVE_ASSERT
#define HAVE_MALLOC
#define HAVE_POSIX_FILE_IO
#define HAVE_POSIX_TIME

#define ENABLE_CLASSIC
// Errors only reach the console through printf in our own code; BTstack's log hooks print nothing without an HCI dump.
#define ENABLE_LOG_ERROR

// Same as the RP2350: a Pair keeps its ACL link and reports a good bonding at once (see platforms/rp2350/btstack/btstack_config.h).
#define ENABLE_EXPLICIT_DEDICATED_BONDING_DISCONNECT

// ACL payload of the combo chip is 1021 bytes (Read_Buffer_Size); +4 is the ACL header. BTstack fragments to what the controller reports.
#define HCI_ACL_PAYLOAD_SIZE (1021 + 4)

// Link key database on top of TLV (platforms/y2/hal/y2_bt_tlv.c): number of remembered headsets. HAL_BT_KEY_SLOTS must match.
#define NVM_NUM_LINK_KEYS 8
#define NVM_NUM_DEVICE_DB_ENTRIES 0

#endif // NEUPOD_Y2_BTSTACK_CONFIG_H

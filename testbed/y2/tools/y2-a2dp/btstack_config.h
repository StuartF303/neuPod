// SPDX-License-Identifier: MPL-2.0
/*
 * BTstack configuration for the y2-a2dp testbed tool: Classic Bluetooth only (A2DP source + AVRCP), POSIX run loop,
 * malloc-backed memory pools. No BLE, no SCO.
 */
#ifndef Y2_A2DP_BTSTACK_CONFIG_H
#define Y2_A2DP_BTSTACK_CONFIG_H

#define HAVE_ASSERT
#define HAVE_MALLOC
#define HAVE_POSIX_FILE_IO
#define HAVE_POSIX_TIME

#define ENABLE_CLASSIC
// Log lines only reach a file when --hci-dump is given (BTstack routes them through the HCI dump).
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO

// ACL payload of the combo chip is 1021 bytes (Read_Buffer_Size); +4 is the ACL header. If the controller reports a
// smaller size BTstack fragments to that, so this is only an upper bound.
#define HCI_ACL_PAYLOAD_SIZE (1021 + 4)

#define NVM_NUM_LINK_KEYS 8
#define NVM_NUM_DEVICE_DB_ENTRIES 0

#endif

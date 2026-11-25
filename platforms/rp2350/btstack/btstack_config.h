// SPDX-License-Identifier: MPL-2.0
/*
 * BTstack configuration for neuPod on the RP2350 + CYW43439 (docs/bluetooth.md).
 *
 * Classic Bluetooth only: A2DP source + AVRCP (stage 2), device discovery (stage 1). No BLE, no mesh, no malloc:
 * BTstack's memory pools are static (counts below, in .bss). The SDK's pico_btstack_classic target defines
 * ENABLE_CLASSIC. Sizes follow the SDK's pico-examples config, trimmed to what an A2DP source needs.
 */
#ifndef NEUPOD_BTSTACK_CONFIG_H
#define NEUPOD_BTSTACK_CONFIG_H

// Logging: errors only, and no HCI dump (the log hooks print nothing unless a dump is initialised, which we do not do).
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

// Dedicated bonding (a Pair) keeps its ACL link and reports a good result at once. Without this define BTstack reports GAP_EVENT_DEDICATED_BONDING_COMPLETED only
// AFTER it has disconnected, and our SDP check for an A2DP sink needs the link: found on the bench, every Pair then timed out although the bonding itself
// had succeeded. A failed bonding still disconnects.
#define ENABLE_EXPLICIT_DEDICATED_BONDING_DISCONNECT

// L2CAP enhanced retransmission is only needed by OBEX/cover art, which we do not use.

// Packet buffers. HCI_OUTGOING_PRE_BUFFER_SIZE and the chunk alignment are required by the CYW43 HCI transport
// (it prepends a 4-byte header and writes 32-bit words). 1021 is the controller's ACL payload; +4 is the ACL header.
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1021 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

// Pools (everything not listed is 0 and costs nothing).
#define MAX_NR_HCI_CONNECTIONS 1        // one headset at a time
#define MAX_NR_L2CAP_SERVICES 3         // SDP, AVDTP, AVRCP
#define MAX_NR_L2CAP_CHANNELS 4         // AVDTP signalling + media, AVRCP control (+ spare)
#define MAX_NR_AVDTP_CONNECTIONS 1
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 2 // SBC source (+ spare)
#define MAX_NR_AVRCP_CONNECTIONS 2      // one link = two entries (BTstack creates the controller role and the target role); 640 bytes each
#define MAX_NR_SERVICE_RECORD_ITEMS 3   // A2DP source, AVRCP target, AVRCP controller
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 0 // we use the TLV link key database (SD-backed), not the RAM one

// Link key database on top of TLV (the SD-card settings store, app/settings): number of remembered headsets.
#define NVM_NUM_LINK_KEYS 8
#define NVM_NUM_DEVICE_DB_ENTRIES 8

// The CYW43439 shares one SPI bus between Wi-Fi and Bluetooth: limit what the controller may have in flight and
// let the host pace it (values from the SDK's examples).
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// Time base: milliseconds from the SDK (the async-context run loop provides them).
#define HAVE_EMBEDDED_TIME_MS

// Some controllers take longer to answer the first HCI reset.
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

#endif // NEUPOD_BTSTACK_CONFIG_H

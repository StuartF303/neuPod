// SPDX-License-Identifier: MPL-2.0
/*
 * btstack_uart_t for a character device that is not a tty and delivers exactly one H4 packet per read() (the MediaTek
 * /dev/stpbt). Use it with the stock hci_transport_h4:
 *
 *   hci_init(hci_transport_h4_instance_for_uart(btstack_uart_stpbt_instance()), &config);
 *
 * H4 asks for 1 byte (type), then the header, then the payload, but the device hands over a whole packet per read().
 * The driver therefore reads a packet into a buffer and serves the requested byte counts from it. Writes are one
 * write() per H4 packet. Baud rate, parity and flow control requests succeed and do nothing.
 */
#ifndef BTSTACK_UART_STPBT_H
#define BTSTACK_UART_STPBT_H

#include "btstack_uart.h"

#if defined __cplusplus
extern "C" {
#endif

const btstack_uart_t * btstack_uart_stpbt_instance(void);

// Use an already open descriptor instead of open()ing config->device_name (unit tests: a SOCK_SEQPACKET socket
// reproduces the one-packet-per-read behaviour). The driver takes ownership and closes it. -1 = open the device.
void btstack_uart_stpbt_set_fd(int fd);

// Called (from the run loop) when read or write fails or the device reports EOF, e.g. after a chip reset.
// Without a handler the message goes to stderr. Reads and writes are disabled before the call.
void btstack_uart_stpbt_set_error_handler(void (*handler)(const char * message));

#if defined __cplusplus
}
#endif

#endif

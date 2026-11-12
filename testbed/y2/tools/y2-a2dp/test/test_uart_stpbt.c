// SPDX-License-Identifier: MPL-2.0
/*
 * Host test for btstack_uart_stpbt: /dev/stpbt returns one whole H4 packet per read(), a SOCK_SEQPACKET socket pair does
 * the same, so the driver and BTstack's stock H4 transport run here against the same framing. Run loop: BTstack POSIX.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "btstack_config.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_posix.h"
#include "hci.h"
#include "hci_transport.h"
#include "hci_transport_h4.h"

#include "btstack_uart_stpbt.h"

#include "../../../../../tests/unit.h"

#define MAX_RECORDED 8

static int peer = -1; // the "chip" end of the socket pair

static void open_pair(void){
    int sv[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0, sv));
    btstack_uart_stpbt_set_fd(sv[0]);
    peer = sv[1];
}

static void chip_sends(const uint8_t * data, size_t len){
    ASSERT_EQ((long long) len, (long long) send(peer, data, len, 0)); // one send() = one message = one read()
}

static btstack_timer_source_t stop_timer;
static void stop_handler(btstack_timer_source_t * t){ (void) t; btstack_run_loop_trigger_exit(); }

// Run the BTstack run loop for a short while.
static void pump(uint32_t ms){
    btstack_run_loop_set_timer_handler(&stop_timer, stop_handler);
    btstack_run_loop_set_timer(&stop_timer, ms);
    btstack_run_loop_add_timer(&stop_timer);
    btstack_run_loop_execute();
}

static hci_transport_config_uart_t config = {
    .type = HCI_TRANSPORT_CONFIG_UART,
    .device_name = "unused",
    .baudrate_init = 115200,
    .baudrate_main = 0,
    .flowcontrol = 0,
    .parity = 0,
};

// --- uart level: split requests are served from one packet --------------------------------------------------------

static int blocks_received;
static void on_block_received(void){ blocks_received++; }
static int blocks_sent;
static void on_block_sent(void){ blocks_sent++; }
static int errors;
static char last_error[96];
static void on_error(const char * msg){ errors++; snprintf(last_error, sizeof(last_error), "%s", msg); }

TEST(split_requests_from_one_packet){
    const btstack_uart_t * uart = btstack_uart_stpbt_instance();
    open_pair();
    uart->init(&(btstack_uart_config_t){ .baudrate = 115200, .device_name = "unused" });
    uart->set_block_received(on_block_received);
    uart->set_block_sent(on_block_sent);
    ASSERT_EQ(0, uart->open());

    // Command Complete for HCI_Reset: type, event, len, ncmd, opcode(2), status
    const uint8_t pkt[] = { 0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };
    chip_sends(pkt, sizeof(pkt));

    uint8_t a[1] = { 0xee }, b[2] = { 0xee, 0xee }, c[4] = { 0xee, 0xee, 0xee, 0xee };
    blocks_received = 0;
    uart->receive_block(a, 1);
    pump(30);
    ASSERT_EQ(1, blocks_received);
    ASSERT_EQ(0x04, a[0]);
    // the rest of the packet is buffered; the next requests must not wait for the device
    uart->receive_block(b, 2);
    pump(30);
    ASSERT_EQ(2, blocks_received);
    ASSERT_TRUE(memcmp(b, &pkt[1], 2) == 0);
    uart->receive_block(c, 4);
    pump(30);
    ASSERT_EQ(3, blocks_received);
    ASSERT_TRUE(memcmp(c, &pkt[3], 4) == 0);

    uart->close();
    close(peer);
}

TEST(next_packet_starts_fresh){
    const btstack_uart_t * uart = btstack_uart_stpbt_instance();
    open_pair();
    uart->init(&(btstack_uart_config_t){ .baudrate = 115200, .device_name = "unused" });
    uart->set_block_received(on_block_received);
    ASSERT_EQ(0, uart->open());

    // two packets queued back to back before anything is requested
    const uint8_t p1[] = { 0x04, 0x05, 0x04, 0xaa, 0xbb, 0xcc, 0xdd };
    const uint8_t p2[] = { 0x02, 0x01, 0x20, 0x01, 0x00, 0x99 };
    chip_sends(p1, sizeof(p1));
    chip_sends(p2, sizeof(p2));

    uint8_t t1[1], rest1[6], t2[1], rest2[5];
    blocks_received = 0;
    uart->receive_block(t1, 1);   pump(30);
    uart->receive_block(rest1, 6); pump(30);
    uart->receive_block(t2, 1);   pump(30);
    uart->receive_block(rest2, 5); pump(30);
    ASSERT_EQ(4, blocks_received);
    ASSERT_EQ(0x04, t1[0]);
    ASSERT_TRUE(memcmp(rest1, &p1[1], 6) == 0);
    ASSERT_EQ(0x02, t2[0]);
    ASSERT_TRUE(memcmp(rest2, &p2[1], 5) == 0);

    uart->close();
    close(peer);
}

TEST(write_is_one_packet_per_write){
    const btstack_uart_t * uart = btstack_uart_stpbt_instance();
    open_pair();
    uart->init(&(btstack_uart_config_t){ .baudrate = 115200, .device_name = "unused" });
    uart->set_block_received(on_block_received);
    uart->set_block_sent(on_block_sent);
    ASSERT_EQ(0, uart->open());

    const uint8_t cmd[] = { 0x01, 0x03, 0x0c, 0x00 };
    blocks_sent = 0;
    uart->send_block(cmd, sizeof(cmd));
    pump(30);
    ASSERT_EQ(1, blocks_sent);
    uint8_t got[16];
    ASSERT_EQ((long long) sizeof(cmd), (long long) recv(peer, got, sizeof(got), 0));
    ASSERT_TRUE(memcmp(got, cmd, sizeof(cmd)) == 0);

    // baud rate / parity / flow control are accepted and ignored
    ASSERT_EQ(0, uart->set_baudrate(921600));
    ASSERT_EQ(0, uart->set_parity(BTSTACK_UART_PARITY_EVEN));
    ASSERT_EQ(0, uart->set_flowcontrol(1));

    uart->close();
    close(peer);
}

TEST(eof_reports_error){
    const btstack_uart_t * uart = btstack_uart_stpbt_instance();
    open_pair();
    btstack_uart_stpbt_set_error_handler(on_error);
    uart->init(&(btstack_uart_config_t){ .baudrate = 115200, .device_name = "unused" });
    uart->set_block_received(on_block_received);
    ASSERT_EQ(0, uart->open());
    uint8_t a[1];
    errors = 0;
    uart->receive_block(a, 1);
    close(peer); // chip went away
    pump(30);
    ASSERT_EQ(1, errors);
    ASSERT_TRUE(strstr(last_error, "EOF") != NULL);
    uart->close();
    btstack_uart_stpbt_set_error_handler(NULL);
}

// --- through the stock H4 transport -------------------------------------------------------------------------------

typedef struct {
    uint8_t  type;
    uint16_t size;
    uint8_t  data[1100];
} recorded_t;
static recorded_t recorded[MAX_RECORDED];
static int num_recorded;

static void on_h4_packet(uint8_t packet_type, uint8_t * packet, uint16_t size){
    if (num_recorded >= MAX_RECORDED || size > sizeof(recorded[0].data)) return;
    recorded[num_recorded].type = packet_type;
    recorded[num_recorded].size = size;
    memcpy(recorded[num_recorded].data, packet, size);
    num_recorded++;
}

TEST(h4_transport_receives_whole_packets){
    const hci_transport_t * h4 = hci_transport_h4_instance_for_uart(btstack_uart_stpbt_instance());
    open_pair();
    h4->init(&config);
    h4->register_packet_handler(on_h4_packet);
    ASSERT_EQ(0, h4->open());

    // 1) event with parameters, 2) event without parameters (no payload read), 3) small ACL, 4) 1000-byte ACL
    const uint8_t ev1[] = { 0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };
    const uint8_t ev2[] = { 0x04, 0x13, 0x00 };
    uint8_t acl1[] = { 0x02, 0x2a, 0x20, 0x04, 0x00, 1, 2, 3, 4 };
    uint8_t acl2[5 + 1000];
    acl2[0] = 0x02; acl2[1] = 0x2a; acl2[2] = 0x20; acl2[3] = 1000 & 0xff; acl2[4] = 1000 >> 8;
    for (int i = 0; i < 1000; i++) acl2[5 + i] = (uint8_t) (i * 7 + 1);

    num_recorded = 0;
    chip_sends(ev1, sizeof(ev1));
    chip_sends(ev2, sizeof(ev2));
    chip_sends(acl1, sizeof(acl1));
    chip_sends(acl2, sizeof(acl2));
    pump(100);

    ASSERT_EQ(4, num_recorded);
    if (num_recorded == 4) {
        ASSERT_EQ(0x04, recorded[0].type);
        ASSERT_EQ(sizeof(ev1) - 1, recorded[0].size);
        ASSERT_TRUE(memcmp(recorded[0].data, &ev1[1], sizeof(ev1) - 1) == 0);
        ASSERT_EQ(0x04, recorded[1].type);
        ASSERT_EQ(2, recorded[1].size);
        ASSERT_TRUE(memcmp(recorded[1].data, &ev2[1], 2) == 0);
        ASSERT_EQ(0x02, recorded[2].type);
        ASSERT_EQ(sizeof(acl1) - 1, recorded[2].size);
        ASSERT_TRUE(memcmp(recorded[2].data, &acl1[1], sizeof(acl1) - 1) == 0);
        ASSERT_EQ(0x02, recorded[3].type);
        ASSERT_EQ(sizeof(acl2) - 1, recorded[3].size);
        ASSERT_TRUE(memcmp(recorded[3].data, &acl2[1], sizeof(acl2) - 1) == 0);
    }

    // packets that arrive later, one at a time, still work after the buffer ran dry
    num_recorded = 0;
    chip_sends(ev1, sizeof(ev1));
    pump(30);
    ASSERT_EQ(1, num_recorded);
    chip_sends(ev1, sizeof(ev1));
    pump(30);
    ASSERT_EQ(2, num_recorded);

    h4->close();
    close(peer);
}

TEST(h4_transport_sends_command_with_type_byte){
    const hci_transport_t * h4 = hci_transport_h4_instance_for_uart(btstack_uart_stpbt_instance());
    open_pair();
    h4->init(&config);
    h4->register_packet_handler(on_h4_packet);
    ASSERT_EQ(0, h4->open());

    // BTstack hands the transport a packet with one byte of room in front for the type
    uint8_t buffer[1 + 4] = { 0, 0x03, 0x0c, 0x00 };
    ASSERT_TRUE(h4->can_send_packet_now(HCI_COMMAND_DATA_PACKET));
    ASSERT_EQ(0, h4->send_packet(HCI_COMMAND_DATA_PACKET, &buffer[1], 3));
    pump(30);
    uint8_t got[16];
    ssize_t n = recv(peer, got, sizeof(got), 0);
    ASSERT_EQ(4, n);
    const uint8_t expect[] = { 0x01, 0x03, 0x0c, 0x00 };
    ASSERT_TRUE(n == 4 && memcmp(got, expect, 4) == 0);
    ASSERT_TRUE(h4->can_send_packet_now(HCI_COMMAND_DATA_PACKET));

    h4->close();
    close(peer);
}

int main(void){
    btstack_run_loop_init(btstack_run_loop_posix_get_instance());
    RUN(split_requests_from_one_packet);
    RUN(next_packet_starts_fresh);
    RUN(write_is_one_packet_per_write);
    RUN(eof_reports_error);
    RUN(h4_transport_receives_whole_packets);
    RUN(h4_transport_sends_command_with_type_byte);
    return RESULT();
}

// SPDX-License-Identifier: MPL-2.0
/*
 * Real BTstack HCI layer + stock H4 transport + btstack_uart_stpbt + the MediaTek chipset driver against a fake chip
 * (a child process on a SOCK_SEQPACKET socket that answers every command with a successful Command Complete).
 * Checks the order on the wire: FC1A, FC79, FC93, FC7A first, then HCI_Reset.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "btstack_config.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_posix.h"
#include "btstack_memory.h"
#include "hci.h"
#include "hci_transport_h4.h"

#include "btstack_chipset_mtk_consys.h"
#include "btstack_uart_stpbt.h"

#include "../../../../../tests/unit.h"

#define NUM_EXPECTED 5

// Fake chip: log "h4 type + opcode + length + params" of each command to log_fd, answer with Command Complete.
static void fake_chip(int fd, int log_fd){
    uint8_t pkt[300];
    for (;;) {
        ssize_t n = recv(fd, pkt, sizeof(pkt), 0);
        if (n <= 0) _exit(0);
        if (pkt[0] != 0x01) continue;
        uint8_t line[300];
        memcpy(line, pkt, (size_t) n);
        if (write(log_fd, &(uint8_t){ (uint8_t) n }, 1) != 1 || write(log_fd, line, (size_t) n) != n) _exit(1);
        uint8_t cc[] = { 0x04, 0x0e, 0x04, 0x01, pkt[1], pkt[2], 0x00 };
        if (send(fd, cc, sizeof(cc), 0) != (ssize_t) sizeof(cc)) _exit(1);
    }
}

static btstack_timer_source_t stop_timer;
static void stop_handler(btstack_timer_source_t * t){ (void) t; btstack_run_loop_trigger_exit(); }

static hci_transport_config_uart_t config = {
    .type = HCI_TRANSPORT_CONFIG_UART, .device_name = "unused", .baudrate_init = 115200,
};

TEST(vendor_script_runs_before_reset){
    int sv[2], lp[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv));
    ASSERT_EQ(0, pipe(lp));
    pid_t pid = fork();
    if (pid == 0) {
        close(sv[0]); close(lp[0]);
        fake_chip(sv[1], lp[1]);
    }
    close(sv[1]); close(lp[1]);

    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_posix_get_instance());
    btstack_uart_stpbt_set_fd(sv[0]);
    hci_init(hci_transport_h4_instance_for_uart(btstack_uart_stpbt_instance()), &config);
    const bd_addr_t addr = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    btstack_chipset_mtk_consys_set_bd_addr(addr);
    hci_set_chipset(btstack_chipset_mtk_consys_instance());
    hci_enable_custom_pre_init();
    hci_power_control(HCI_POWER_ON);

    btstack_run_loop_set_timer_handler(&stop_timer, stop_handler);
    btstack_run_loop_set_timer(&stop_timer, 500);
    btstack_run_loop_add_timer(&stop_timer);
    btstack_run_loop_execute();

    // what the fake chip saw, in order
    static const uint8_t expect[NUM_EXPECTED][12] = {
        { 10, 0x01, 0x1a, 0xfc, 0x06, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 },
        { 10, 0x01, 0x79, 0xfc, 0x06, 0x07, 0x80, 0x00, 0x06, 0x05, 0x07 },
        { 7,  0x01, 0x93, 0xfc, 0x03, 0xff, 0xff, 0xff },
        { 11, 0x01, 0x7a, 0xfc, 0x07, 0x03, 0x40, 0x1f, 0x40, 0x1f, 0x00, 0x04 },
        { 4,  0x01, 0x03, 0x0c, 0x00 },
    };
    for (int i = 0; i < NUM_EXPECTED; i++) {
        uint8_t len = 0, got[300];
        ASSERT_EQ(1, read(lp[0], &len, 1));
        ASSERT_EQ(expect[i][0], len);
        ASSERT_EQ(len, read(lp[0], got, len));
        ASSERT_TRUE(len == expect[i][0] && memcmp(got, &expect[i][1], len) == 0);
    }

    hci_power_control(HCI_POWER_OFF);
    close(lp[0]);
    kill(pid, 9);
    waitpid(pid, NULL, 0);
}

int main(void){
    RUN(vendor_script_runs_before_reset);
    return RESULT();
}

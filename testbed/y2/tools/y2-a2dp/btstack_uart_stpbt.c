// SPDX-License-Identifier: MPL-2.0
/*
 * Based on BTstack's platform/posix/btstack_uart_posix.c (block mode), minus termios and with packet buffering.
 */
#define BTSTACK_FILE__ "btstack_uart_stpbt.c"

#include "btstack_uart_stpbt.h"

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "btstack_debug.h"
#include "btstack_run_loop.h"

// One H4 packet per read(); larger than any HCI_ACL_PAYLOAD_SIZE we configure plus headers.
#define RX_BUFFER_SIZE 2048

static const btstack_uart_config_t * uart_config;
static int                           preset_fd = -1;
static void (*error_handler)(const char * message);

static btstack_data_source_t transport_data_source;
static bool                  is_open;
static btstack_timer_source_t deliver_timer; // delivers buffered bytes from the run loop, not from receive_block()

// block write
static uint16_t        write_len;
static const uint8_t * write_data;

// block read: what H4 asked for ...
static uint16_t  read_len;
static uint8_t * read_data;
// ... and the packet read() returned, not yet consumed
static uint8_t  rx_buffer[RX_BUFFER_SIZE];
static uint16_t rx_len;
static uint16_t rx_pos;

static void (*block_received)(void);
static void (*block_sent)(void);

static void process(btstack_data_source_t * ds, btstack_data_source_callback_type_t callback_type);

static void fail(const char * what){
    int err = errno;
    char msg[96];
    if (err) {
        snprintf(msg, sizeof(msg), "%s: %s", what, strerror(err));
    } else {
        snprintf(msg, sizeof(msg), "%s", what);
    }
    log_error("stpbt: %s", msg);
    btstack_run_loop_disable_data_source_callbacks(&transport_data_source, DATA_SOURCE_CALLBACK_WRITE | DATA_SOURCE_CALLBACK_READ);
    btstack_run_loop_remove_timer(&deliver_timer);
    if (error_handler) {
        error_handler(msg);
    } else {
        fprintf(stderr, "stpbt: %s\n", msg);
    }
}

// Hand H4 as many of the requested bytes as the buffered packet holds; notify H4 once its request is complete.
static void serve_from_buffer(void){
    if (read_len == 0) return;
    uint16_t avail = (uint16_t) (rx_len - rx_pos);
    uint16_t n     = avail < read_len ? avail : read_len;
    memcpy(read_data, &rx_buffer[rx_pos], n);
    rx_pos    = (uint16_t) (rx_pos + n);
    read_data += n;
    read_len  = (uint16_t) (read_len - n);
    if (rx_pos == rx_len) {
        rx_pos = 0;
        rx_len = 0;
    }
    if (read_len > 0) return; // need the next packet: wait for the device

    btstack_run_loop_disable_data_source_callbacks(&transport_data_source, DATA_SOURCE_CALLBACK_READ);
    if (block_received) block_received();
}

static void process_read(btstack_data_source_t * ds){
    if (read_len == 0) {
        btstack_run_loop_disable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_READ);
        return;
    }
    if (rx_len == 0) {
        ssize_t n;
        do {
            n = read(ds->source.fd, rx_buffer, sizeof(rx_buffer));
        } while (n < 0 && errno == EINTR);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n == 0) {
            errno = 0;
            fail("device closed (EOF)");
            return;
        }
        if (n < 0) {
            fail("read");
            return;
        }
        rx_len = (uint16_t) n;
        rx_pos = 0;
    }
    serve_from_buffer();
}

static void deliver_timer_handler(btstack_timer_source_t * timer){
    (void) timer;
    if (read_len > 0 && rx_len > 0) serve_from_buffer();
}

static void process_write(btstack_data_source_t * ds){
    if (write_len == 0) return;
    ssize_t n = write(ds->source.fd, write_data, write_len);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        btstack_run_loop_enable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_WRITE);
        return;
    }
    if (n <= 0) {
        if (n == 0) errno = 0;
        fail("write");
        return;
    }
    if ((uint16_t) n != write_len) {
        // The device takes one packet per write(); a short write means a corrupt packet on the wire.
        errno = 0;
        fail("short write");
        return;
    }
    write_len = 0;
    btstack_run_loop_disable_data_source_callbacks(ds, DATA_SOURCE_CALLBACK_WRITE);
    if (block_sent) block_sent();
}

static void process(btstack_data_source_t * ds, btstack_data_source_callback_type_t callback_type){
    switch (callback_type) {
        case DATA_SOURCE_CALLBACK_READ:
            process_read(ds);
            break;
        case DATA_SOURCE_CALLBACK_WRITE:
            process_write(ds);
            break;
        default:
            break;
    }
}

static int uart_init(const btstack_uart_config_t * config){
    uart_config = config;
    return 0;
}

static int uart_open(void){
    int fd = preset_fd;
    preset_fd = -1;
    if (fd < 0) {
        // The same flags as the vendor stack. Opening powers the chip on and takes about 1.3 s.
        fd = open(uart_config->device_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
            fail("open device");
            return -1;
        }
    }
    read_len = 0;
    write_len = 0;
    rx_len = 0;
    rx_pos = 0;
    is_open = true;
    btstack_run_loop_set_data_source_fd(&transport_data_source, fd);
    btstack_run_loop_set_data_source_handler(&transport_data_source, &process);
    btstack_run_loop_add_data_source(&transport_data_source);
    btstack_run_loop_set_timer_handler(&deliver_timer, &deliver_timer_handler);
    log_info("stpbt: open %s", uart_config && uart_config->device_name ? uart_config->device_name : "(fd)");
    return 0;
}

static int uart_close(void){
    btstack_run_loop_remove_timer(&deliver_timer);
    read_len = 0;
    write_len = 0;
    rx_len = 0;
    rx_pos = 0;
    if (is_open) {
        btstack_run_loop_remove_data_source(&transport_data_source);
        close(transport_data_source.source.fd); // powers the chip's BT function off
        is_open = false;
    }
    return 0;
}

static void uart_set_block_received(void (*handler)(void)){
    read_len = 0;
    block_received = handler;
}

static void uart_set_block_sent(void (*handler)(void)){
    write_len = 0;
    block_sent = handler;
}

// Not a serial port: nothing to configure.
static int uart_set_baudrate(uint32_t baudrate){ (void) baudrate; return 0; }
static int uart_set_parity(int parity){ (void) parity; return 0; }
static int uart_set_flowcontrol(int flowcontrol){ (void) flowcontrol; return 0; }

static void uart_receive_block(uint8_t * buffer, uint16_t len){
    btstack_assert(read_len == 0);
    read_data = buffer;
    read_len  = len;
    btstack_run_loop_enable_data_source_callbacks(&transport_data_source, DATA_SOURCE_CALLBACK_READ);
    if (rx_len > 0) {
        // Bytes are already buffered, so poll() may never fire for this request. Deliver from the run loop.
        btstack_run_loop_remove_timer(&deliver_timer);
        btstack_run_loop_set_timer(&deliver_timer, 0);
        btstack_run_loop_add_timer(&deliver_timer);
    }
}

static void uart_send_block(const uint8_t * data, uint16_t size){
    btstack_assert(write_len == 0);
    write_data = data;
    write_len  = size;
    btstack_run_loop_enable_data_source_callbacks(&transport_data_source, DATA_SOURCE_CALLBACK_WRITE);
}

static const btstack_uart_t uart_stpbt = {
    /* init */                    uart_init,
    /* open */                    uart_open,
    /* close */                   uart_close,
    /* set_block_received */      uart_set_block_received,
    /* set_block_sent */          uart_set_block_sent,
    /* set_baudrate */            uart_set_baudrate,
    /* set_parity */              uart_set_parity,
    /* set_flowcontrol */         uart_set_flowcontrol,
    /* receive_block */           uart_receive_block,
    /* send_block */              uart_send_block,
    /* get_supported_sleep_modes */ NULL,
    /* set_sleep */               NULL,
    /* set_wakeup_handler */      NULL,
    /* set_frame_received */      NULL,
    /* set_frame_sent */          NULL,
    /* receive_frame */           NULL,
    /* send_frame */              NULL,
};

const btstack_uart_t * btstack_uart_stpbt_instance(void){
    return &uart_stpbt;
}

void btstack_uart_stpbt_set_fd(int fd){
    preset_fd = fd;
}

void btstack_uart_stpbt_set_error_handler(void (*handler)(const char * message)){
    error_handler = handler;
}

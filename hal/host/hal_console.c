// SPDX-License-Identifier: MPL-2.0
/*
 * Host console input: stdin in raw mode (no line buffering, no echo, Ctrl-C still works) when it
 * is a terminal; plain non-blocking reads when piped. Waits in short slices so the simulator
 * window keeps pumping events.
 */
#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "hal_console.h"
#include "hal_system.h"
#include "host_internal.h"

static struct termios g_saved;
static bool g_raw_active;
static bool g_setup_done;

static void restore_terminal(void) {
    if (g_raw_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
        g_raw_active = false;
    }
}

static void on_signal(int sig) {
    restore_terminal(); // tcsetattr is async-signal-safe
    signal(sig, SIG_DFL);
    raise(sig);
}

static void setup(void) {
    g_setup_done = true;
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &g_saved) != 0) {
        return;
    }
    struct termios raw = g_saved;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return;
    }
    g_raw_active = true;
    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGQUIT, on_signal);
}

int hal_console_getc_timeout(uint32_t ms) {
    if (!g_setup_done) {
        setup();
    }
    uint32_t start = hal_millis();
    for (;;) {
        host_pump_events();
        uint32_t elapsed = hal_millis() - start;
        uint32_t left = elapsed >= ms ? 0 : ms - elapsed;
        struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
        int r = poll(&pfd, 1, (int)(left < 10 ? left : 10));
        if (r > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
            unsigned char c;
            ssize_t n = read(STDIN_FILENO, &c, 1);
            if (n == 1) {
                return c;
            }
            if (n == 0) { // EOF: nobody left to type
                fflush(stdout);
                exit(0);
            }
        }
        if (left == 0) {
            return -1;
        }
    }
}

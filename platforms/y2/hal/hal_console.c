// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 console keys, for driving the UI remotely over ssh: stdin when it is a terminal (raw mode, as on the host)
 * or a pipe/socket, plus a control FIFO (/run/neupod.keys, or $NEUPOD_KEYS_FIFO):  echo s > /run/neupod.keys
 * Keys are w/s scroll, e select, m menu, a/d left/right, p play (input_events_from_key). A stdin that is
 * /dev/null or at end-of-file (the init script's case) is simply dropped: unlike the host build, the process
 * never exits because nobody is typing.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "hal_console.h"
#include "hal_system.h"
#include "y2_hal.h"
#include "y2_input.h"

static struct termios g_saved;
static bool g_raw_active;
static bool g_setup_done;
static int g_stdin = -1; // -1: not a usable key source
static bool g_stdin_tty;
static int g_fifo = -1;
static uint32_t g_idle_since_ms;

static void restore_terminal(void) {
    if (g_raw_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
        g_raw_active = false;
    }
}

static void on_signal(int sig) {
    restore_terminal();
    y2_audio_emergency_pa_off(); // this handler ends the process without exit(): the speaker amplifier must not stay on
    signal(sig, SIG_DFL);
    raise(sig);
}

static void setup_stdin(void) {
    struct stat st;
    if (fstat(STDIN_FILENO, &st) != 0) {
        return;
    }
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_saved) == 0) {
        struct termios raw = g_saved;
        raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            g_raw_active = true;
            atexit(restore_terminal);
            signal(SIGINT, on_signal);
            signal(SIGTERM, on_signal);
            signal(SIGHUP, on_signal);
            signal(SIGQUIT, on_signal);
        }
        g_stdin = STDIN_FILENO;
        g_stdin_tty = true;
    } else if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) {
        g_stdin = STDIN_FILENO;
    }
}

static void setup_fifo(void) {
    const char *path = getenv("NEUPOD_KEYS_FIFO");
    if (path == NULL || path[0] == '\0') {
        path = Y2_KEYS_FIFO;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkfifo(path, 0600) != 0) {
            printf("console: cannot create control FIFO %s: %s\n", path, strerror(errno));
            return;
        }
    } else if (!S_ISFIFO(st.st_mode)) {
        printf("console: %s exists and is not a FIFO, not used\n", path);
        return;
    }
    // O_RDWR: we are also a writer, so the FIFO never reports end-of-file when the last `echo` closes.
    g_fifo = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (g_fifo < 0) {
        printf("console: cannot open %s: %s\n", path, strerror(errno));
        return;
    }
    printf("console: control FIFO %s (echo s > %s)\n", path, path);
}

static void setup(void) {
    g_setup_done = true;
    setup_stdin();
    setup_fifo();
}

int hal_console_getc_timeout(uint32_t ms) {
    if (!g_setup_done) {
        setup();
    }
    // The input task polls with ms == 0 every 2 ms: once a poll found nothing, wait 8 ms before the next syscall (a key
    // waits in the pipe meanwhile); while keys keep coming they are drained back to back.
    if (ms == 0 && (uint32_t)(hal_millis() - g_idle_since_ms) < 8u) {
        return -1;
    }
    struct pollfd pfd[2];
    int n = 0, i_stdin = -1, i_fifo = -1;
    if (g_stdin >= 0) {
        i_stdin = n;
        pfd[n++] = (struct pollfd){.fd = g_stdin, .events = POLLIN};
    }
    if (g_fifo >= 0) {
        i_fifo = n;
        pfd[n++] = (struct pollfd){.fd = g_fifo, .events = POLLIN};
    }
    if (n == 0) {
        if (ms > 0) {
            poll(NULL, 0, (int)ms);
        }
        return -1;
    }
    int r = poll(pfd, (nfds_t)n, (int)ms);
    if (r <= 0) {
        g_idle_since_ms = hal_millis();
        return -1;
    }
    for (int k = 0; k < n; k++) {
        if ((pfd[k].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
            continue;
        }
        unsigned char c;
        ssize_t got = read(pfd[k].fd, &c, 1);
        if (got == 1) {
            int key = y2_console_key(c, k == i_stdin && g_stdin_tty);
            if (key >= 0) {
                return key;
            }
        } else if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR)) {
            if (k == i_stdin) { // nobody is typing any more: stop watching stdin, keep running
                g_stdin = -1;
            } else if (k == i_fifo) {
                close(g_fifo);
                g_fifo = -1;
            }
        }
    }
    return -1;
}

// SPDX-License-Identifier: MPL-2.0
/*
 * neuPod on the Innioasis Y2 (Linux, MT6582, armv7 hard-float): the same FreeRTOS tasks, UI and boot checks as the
 * other targets, on the FreeRTOS POSIX port, with the Y2 HALs in platforms/y2/hal. Usage:
 *   neupod                       run until stopped (SIGTERM from the init script)
 *   neupod --run-ms N            stop after N ms (exit status 0 if the run looked healthy)
 *   neupod --splash-hold-ms N    how long the splash stays up when a boot check failed (no card, no radio ...)
 *   neupod --script "s s s ..."  feed keys like the host tour does
 * Keys can also be sent while it runs:  echo s > /run/neupod.keys   (w/s scroll, e select, m menu, a/d, p play).
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio_engine.h"
#include "hal_system.h"
#include "os.h"
#include "player.h"
#include "runtime.h"
#include "y2_hal.h"

static void on_volume(int32_t steps) { (void)player_volume_step(steps); }

static void on_done(void *ctx) {
    (void)ctx;
    audio_stats_t a = {0};
    int rc = 0;
    if (!audio_stats_get(&a) || a.blocks < 100) { // 100 blocks = 0.58 s of audio clock: the audio task must have run
        fprintf(stderr, "y2: FAIL audio core served %u blocks\n", (unsigned)a.blocks);
        rc = 2;
    }
    printf("y2: run finished, status %d\n", rc);
    fflush(stdout);
    exit(rc);
}

static void on_term(int sig) {
    (void)sig;
    static const char msg[] = "y2: terminated\n";
    ssize_t r = write(STDOUT_FILENO, msg, sizeof msg - 1);
    (void)r;
    _exit(0);
}

int main(int argc, char **argv) {
    neupod_runtime_config_t cfg = {.console_keys = true, .storage_hotplug = true};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--run-ms") == 0 && i + 1 < argc) {
            cfg.run_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
            cfg.on_done = on_done;
        } else if (strcmp(argv[i], "--splash-hold-ms") == 0 && i + 1 < argc) {
            cfg.splash_hold_ms = (uint32_t)strtoul(argv[++i], NULL, 10); // how long the splash stays when a check failed
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            cfg.ui_script = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--run-ms N] [--splash-hold-ms N] [--script KEYS]\n", argv[0]);
            return 64;
        }
    }
    if (hal_system_init() != HAL_OK) {
        fprintf(stderr, "neuPod: hal_system_init failed\n");
        return 1;
    }
    signal(SIGTERM, on_term);
    signal(SIGPIPE, SIG_IGN); // a closed ssh session must not kill the player
    y2_set_delay_hook(os_delay_ms); // HAL sleeps go through the scheduler
    y2_input_set_volume_handler(on_volume);
    neupod_runtime_run(&cfg);
    return 0;
}

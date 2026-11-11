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
#include "charging.h"
#include "hal_system.h"
#include "os.h"
#include "player.h"
#include "runtime.h"
#include "y2_hal.h"
#include "y2_poweroff.h"
#include "y2_sysfs.h"

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

// SIGTERM only sets a flag: the monitor task sees it (stop_requested), lets the storage task finish, locks card writes and
// exits in order. Doing that work in the handler could cut a card write in half.
static volatile sig_atomic_t g_stop;
static void on_term(int sig) {
    (void)sig;
    g_stop = 1;
}
static bool stop_requested(void) { return g_stop != 0; }

// Power key held: the same orderly stop as SIGTERM (the monitor task finishes the storage task, locks card writes and calls
// exit(0)), then, from an atexit hook, the poweroff command. Running it only after that stop means the card is already
// flushed and unmounted when init starts its own stop scripts; S91neupod-fw's stop then finds this process gone (or, if it
// is still in exit(), SIGTERMs it, which only sets g_stop again). poweroff is spawned detached and never waited for, so
// nothing can deadlock. Exit status 77 tells the init script's respawn wrapper not to restart the firmware; if poweroff
// cannot be started the exit stays 0, the wrapper respawns the firmware and the device keeps running.
#define Y2_EXIT_POWEROFF 77
static volatile sig_atomic_t g_poweroff;
static void poweroff_at_exit(void) {
    if (g_poweroff == 0) {
        return;
    }
    g_poweroff = 0;
    printf("y2: stopped, requesting poweroff (%s)\n", y2_poweroff_command());
    fflush(NULL);
    if (y2_poweroff_spawn(NULL)) {
        _exit(Y2_EXIT_POWEROFF);
    }
    fprintf(stderr, "y2: cannot start %s, staying up\n", y2_poweroff_command());
}
static void on_power_hold(void) {
    g_poweroff = 1;
    g_stop = 1;
}

int main(int argc, char **argv) {
    neupod_runtime_config_t cfg = {.console_keys = true, .storage_hotplug = true, .stop_requested = stop_requested};
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
    // Powered on by the charger while "off" (boot mode 8/9): charging-only mode, nothing of the player starts. Absent or
    // unreadable means a normal boot. Decided before any HAL init so no storage, audio or RTOS is touched.
    int boot_mode = y2_boot_mode_read(Y2_BOOT_MODE_PATH);
    if (boot_mode < 0) {
        printf("y2: boot_mode unavailable (%s), normal start\n", Y2_BOOT_MODE_PATH);
    } else {
        printf("y2: boot_mode %d (%s)\n", boot_mode, y2_boot_mode_is_charging(boot_mode) ? "charging" : "normal");
    }
    fflush(stdout);
    if (y2_boot_mode_is_charging(boot_mode)) {
        if (argc > 1) {
            printf("y2: charging mode ignores the command line arguments (--run-ms, --script, ...)\n");
        }
        return y2_charging_main();
    }
    if (hal_system_init() != HAL_OK) {
        fprintf(stderr, "neuPod: hal_system_init failed\n");
        return 1;
    }
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGPIPE, SIG_IGN); // a closed ssh session must not kill the player
    y2_set_delay_hook(os_delay_ms); // HAL sleeps go through the scheduler
    y2_input_set_volume_handler(on_volume);
    // Must stay the FIRST atexit registration: handlers run last-in first-out and poweroff_at_exit ends with _exit(77),
    // which would skip any handler registered before it.
    atexit(poweroff_at_exit);
    y2_input_set_power_handler(on_power_hold);
    neupod_runtime_run(&cfg);
    return 0;
}

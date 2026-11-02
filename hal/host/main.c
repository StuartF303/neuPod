// SPDX-License-Identifier: MPL-2.0
/*
 * Host build of the neuPod runtime: the same tasks, UI and boot checks as the RP2350, on the
 * FreeRTOS POSIX port. Usage:
 *   neupod                        run until the window is closed (SDL build) or interrupted
 *   neupod --run-ms N             stop after N ms (exit status 0 if the run looked healthy)
 *   neupod --splash-hold-ms N     shorten the splash hold shown when a boot check failed (default 8000)
 *   neupod --tour                 walk through the screens by script (with --snapshots: PPMs of each)
 *   neupod --script "s s s _ ..."  feed keys like the tour does (snap:NAME writes a snapshot)
 *   neupod --snapshots DIR        also write DIR/splash.ppm and DIR/home.ppm (works headless)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_engine.h"
#include "hal_host.h"
#include "hal_system.h"
#include "os.h"
#include "runtime.h"

// Keys: s/w scroll, _ = Enter (Select), m = Menu, x = nothing (lets simulated time pass). snap:NAME writes NAME.ppm.
#define TOUR                                                                                       \
    "snap:home _ snap:music-menu _ snap:music-empty m m s s s _ snap:extras _ snap:files _ "        \
    "snap:files-sub m s s _ snap:files-many 260*s snap:files-many-end m m m m s _ snap:settings "                \
    "s s s snap:settings-bt _ snap:bt-idle s s _ snap:bt-scanning x x x x x x x x x snap:bt-found s _ snap:bt-pair "     \
    "m w w w _ snap:about m m s s _ snap:nowplaying m"

static const char *g_snapshot_dir;
static unsigned g_snapshots;
static bool cfgp_tour; // a tour was requested: expect all its snapshots

static void on_screen(const char *name, void *ctx) {
    (void)ctx;
    if (g_snapshot_dir == NULL) {
        return;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", g_snapshot_dir, name);
    if (hal_host_display_dump_ppm(path)) {
        printf("host: wrote %s\n", path);
        g_snapshots++;
    }
}

static void on_done(void *ctx) {
    (void)ctx;
    audio_stats_t a = {0};
    int rc = 0;
    if (!audio_stats_get(&a)) {
        fprintf(stderr, "host: FAIL audio stats unavailable\n");
        rc = 4;
    }
    if (a.blocks < 100) { // 100 blocks = 0.58 s of audio clock: the audio task must have been running
        fprintf(stderr, "host: FAIL audio task served only %u blocks\n", (unsigned)a.blocks);
        rc = 2;
    }
    if (g_snapshot_dir != NULL && g_snapshots < (cfgp_tour ? 18u : 2u)) {
        fprintf(stderr, "host: FAIL expected splash and home snapshots, got %u\n", g_snapshots);
        rc = 3;
    }
    printf("host: run finished, status %d\n", rc);
    fflush(stdout);
    exit(rc);
}

int main(int argc, char **argv) {
    neupod_runtime_config_t cfg = {.on_screen = on_screen, .on_done = on_done};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--run-ms") == 0 && i + 1 < argc) {
            cfg.run_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--tour") == 0) {
            cfg.ui_script = TOUR;
            cfgp_tour = true; // scripted walk through the screens, snapshots written to --snapshots DIR
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            cfg.ui_script = argv[++i]; // ad-hoc key script, same tokens as the tour
        } else if (strcmp(argv[i], "--splash-hold-ms") == 0 && i + 1 < argc) {
            cfg.splash_hold_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--snapshots") == 0 && i + 1 < argc) {
            g_snapshot_dir = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--run-ms N] [--splash-hold-ms N] [--tour] [--script KEYS] [--snapshots DIR]\n", argv[0]);
            return 64;
        }
    }
    if (hal_system_init() != HAL_OK) {
        fprintf(stderr, "neuPod: hal_system_init failed\n");
        return 1;
    }
    hal_host_set_delay_hook(os_delay_ms); // HAL sleeps must go through the scheduler
    neupod_runtime_run(&cfg);
    return 0;
}

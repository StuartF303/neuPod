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
    "m m w w w _ snap:about m m s s _ snap:nowplaying m"

// Wheel input widgets (Extras > Input widgets): text, password with show, live search, number, slider, choice, multi choice, date/time.
#define TOUR_INPUT                                                                                 \
    "x s s s _ snap:extras-in s s _ snap:in-list s _ s _ s _ snap:in-text p snap:in-result "         \
    "s _ _ s _ s s _ w w _ snap:in-password p s _ snap:in-search-empty _ snap:in-search m m "  \
    "s _ s s s snap:in-number p s _ s s s s s snap:in-slider p s _ s snap:in-choice p "              \
    "s _ _ s s _ snap:in-multi p s _ s d s snap:in-datetime p snap:in-end"

// Wi-Fi: Settings > Wi-Fi > Scan (Networks screen), pick a network, type the password on the wheel keyboard, join, NTP time on the home screen.
#define TOUR_WIFI                                                                                  \
    "x s s s s _ s s s s s snap:settings-wifi _ snap:wifi-idle s s _ snap:wifi-scan x x x x x x "      \
    "snap:wifi-found s _ snap:wifi-kb-empty _ s _ s s _ snap:wifi-kb p x x x x x x "          \
    "snap:wifi-joined m m m x x x snap:home-clock"

// Album art: Extras > Files > three albums, with a trip back to Home while the embedded-art album is paused (a Cover.JPG beside the track; a
// JPEG inside the track; a PNG inside it plus a progressive cover.jpg beside it). The test tracks last 0.3 s: Settings > Repeat is set to
// One first (a track that ended would drop Now Playing to idle before the snapshot), each track is paused so the art service stops
// re-decoding on every loop, and Repeat goes back to Off at the end.
#define TOUR_ART                                                                                   \
    "x s s s s _ s s s s s s _ m w _ _ _ s _ x x _ x x x x x x snap:art-cover m m s _ _ x x _ x x x x x x snap:art-embedded "  \
    "m m m m x x snap:art-home _ _ s s _ s _ x x _ x x x x x x snap:art-none m m m m s _ s s s s s s _ _ x x"

static unsigned g_expect = 18;
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
    if (g_snapshot_dir != NULL && g_snapshots < (cfgp_tour ? g_expect : 2u)) {
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
        } else if (strcmp(argv[i], "--tour-input") == 0) {
            cfg.ui_script = TOUR_INPUT;
            cfgp_tour = true;
            g_expect = 14;
        } else if (strcmp(argv[i], "--tour-wifi") == 0) {
            cfg.ui_script = TOUR_WIFI;
            cfgp_tour = true;
            g_expect = 8;
        } else if (strcmp(argv[i], "--tour-art") == 0) {
            cfg.ui_script = TOUR_ART;
            cfgp_tour = true;
            g_expect = 4;
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            cfg.ui_script = argv[++i]; // ad-hoc key script, same tokens as the tour
        } else if (strcmp(argv[i], "--splash-hold-ms") == 0 && i + 1 < argc) {
            cfg.splash_hold_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--snapshots") == 0 && i + 1 < argc) {
            g_snapshot_dir = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--run-ms N] [--splash-hold-ms N] [--tour|--tour-art] [--script KEYS] [--snapshots DIR]\n", argv[0]);
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

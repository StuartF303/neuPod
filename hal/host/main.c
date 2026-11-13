// SPDX-License-Identifier: MPL-2.0
/*
 * Host build of the neuPod runtime: the same tasks, UI and boot checks as the RP2350, on the
 * FreeRTOS POSIX port. Usage:
 *   neupod                        run until the window is closed (SDL build) or interrupted
 *   neupod --run-ms N             stop after N ms (exit status 0 if the run looked healthy)
 *                                 exit status: 0 healthy; 2 audio task starved; 3 fewer snapshots than the tour or the splash+home
 *                                 pair promised (typically --run-ms too short for the script); 4 no audio stats
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
    "s s s snap:settings-bt _ snap:bt-idle s _ snap:bt-scanning x x x x x x x x x snap:bt-found _ snap:bt-pair "     \
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

// Bluetooth audio (stage 2b), after the pairing tour (the card has the WAV album): pair a headset again (its stream opens by itself and becomes the
// output), play the first track on it (Now Playing shows the codec in the badge), look at Settings > Bluetooth while it streams, then the headset
// goes away (bt:drop): the player pauses, the banner says so and the row says why. Stage 2c adds the headset's volume (the wheel moves it: np-bt-volume; a swipe on the stem
// to 0 %: np-bt-volume0, bt:vol:N) and its buttons (bt:key:next: bt-key-next, the next track plays).
#define TOUR_BT_AUDIO                                                                              \
    "w w w w w s s s s _ x s s s s s s s s _ x s _ x x x x x x x x x _ x x x x x x x x m m m m "   \
    "w w w w w s s s _ _ _ _ x x x x x x snap:np-bt-badge 4*s snap:np-bt-volume bt:vol:0 x x x snap:np-bt-volume0 bt:vol:48 x x bt:key:next x x x x snap:bt-key-next m m m m "  \
    "w w w w w s s s s _ _ snap:bt-about s _ 14*s snap:bt-diag m m m m w w w w w s s s s _ x s s s s s s s s _ x s _ x x snap:bt-streaming m m snap:settings-bt-streaming _ s _ x "  \
    "bt:drop x x x snap:bt-dropped x x x x snap:bt-reconnected toast:warn:Couldn't_reconnect_\xE2\x80\x94_paused x snap:bt-gave-up strips:off m m m m"

// Bluetooth pairing (BT-1 to BT-3): Settings > Bluetooth (the Audio output row cycles Auto, Wired, Bluetooth, Auto), Devices (scanning dots, found list, three-bar signal),
// pair the first headset (busy dots, then Connected), its options (Connect / Disconnect, Forget), Forget asks (Menu keeps it), a second headset asks "Replace ...?" (Menu keeps),
// Disconnect (the row rests at Saved), Forget (Select = yes), then a pairing the car kit refuses (Failed: pairing refused).
#define TOUR_BT                                                                                    \
    "x x x x s s s s _ x s s s s s s s s snap:settings-bt _ snap:bt-idle _ _ snap:bt-output-bt _ s _ x x x x snap:bt-scanning x x x x x snap:bt-found "  \
    "_ snap:bt-pairing x x x x snap:bt-paired _ snap:bt-options s _ snap:bt-forget-asks m m s _ snap:bt-replace-asks m w _ _ x x x snap:bt-disconnected "  \
    "_ s _ _ x x x snap:bt-forgotten s s s _ x x x x snap:bt-failed m m m " TOUR_BT_AUDIO

// Album art: Extras > Files > three albums, with a trip back to Home while the embedded-art album is paused (a Cover.JPG beside the track; a
// JPEG inside the track; a PNG inside it plus a progressive cover.jpg beside it). The test tracks last 0.3 s: Settings > Repeat is set to
// One first (a track that ended would drop Now Playing to idle before the snapshot), each track is paused (wait: tokens hold the script until the player / art service get there, so no wall-clock timing) so the art
// service stops re-decoding on every loop, the ctest fixture wipes the tour's settings so it always starts from Off.
#define TOUR_ART                                                                                   \
    "x s s s s _ s s s s s s _ m w _ _ _ s _ wait:pause wait:art x snap:art-cover m m s _ _ wait:pause wait:art x snap:art-embedded "  \
    "m m m m wait:art x x snap:art-home _ _ s s _ s _ wait:pause x snap:art-none m"

// Now Playing (NP-1 to NP-4): a 20 s WAV album (seekable) is paused (np-nowplaying); HOLDING Select (sel:down, sleep, sel:up) enters seek mode, the wheel
// scrubs, Menu cancels, a second hold and Select applies the scrub (seek-go); a short Select opens page 2 (np-page2) and a volume change from outside
// shows the HUD there (np-volume-hud); then an MP3 (seekable too: seek-mp3) and a FLAC without a seek table: the hold answers "Seek not available". Settings > Repeat is set to One first
// (the MP3 and FLAC last 1 s: they must still be there to pause). The tracks are paused at once (wait:pause) so the pictures do not
// depend on the clock; the fixture is made by ctest (tests/CMakeLists.txt, seek card).
#define TOUR_SEEK                                                                                  \
    "x s s s s _ s s s s s s _ m w _ _ _ _ x wait:pause snap:seek-volume snap:np-nowplaying sel:down sleep:700 sel:up x snap:seek-mode 8*s snap:seek-scrub "  \
    "m x snap:seek-cancel sel:down sleep:700 sel:up x 3*s _ x x snap:seek-go _ x snap:np-page2 vol:2 x snap:np-volume-hud strips:off "  \
    "m m m s _ _ x wait:pause sel:down sleep:700 sel:up x snap:seek-mp3 m m m s _ _ x wait:pause sel:down sleep:700 sel:up x snap:seek-unavailable m"

// Music library (ADR-0017, L2): every list of Music on a card with a small tagged library (tests/tools/make_library_card.py): the first open builds the
// index (NEUPOD_LIB_SLOW_MS slows the scan so the progress screen can be seen), Artists > artist > album > songs with a greyed AAC track and a refused
// Select, Albums, Songs scrolled, Genres > genre > All Songs, Compilations, Update Library (rebuild), Shuffle Songs, and an album played from its list:
// the 3 s tracks hand over gaplessly (the log shows it). wait:pause holds the script until the player is paused, so no snapshot depends on the clock.
#define TOUR_LIBRARY                                                                                                                      \
    "x _ snap:lib-music-menu _ x x x x snap:lib-building wait:lib x x snap:lib-artists "                                                 \
    "_ x x snap:lib-artist s _ x x snap:lib-album m m s _ x x s _ x x s snap:lib-album-greyed _ x snap:lib-cant-play "                  \
    "m m m s _ x x x snap:lib-albums m s _ x x x snap:lib-songs vol:2 x snap:lib-volume-hud strips:off 8*s rail:hold x snap:lib-songs-fastscroll rail:release x x x snap:lib-songs-scrolled "                                        \
    "m s _ x x x snap:lib-genres _ x x snap:lib-genre _ x x snap:lib-genre-songs "                                                     \
    "m m m s _ x x x snap:lib-comps _ x x snap:lib-comp-songs "                                                                          \
    "m m s s _ x x x snap:lib-update-building x x x x x x x x x x x x snap:lib-update-done m m "                                                             \
    "_ 6*s _ x snap:lib-bg-building m x snap:lib-bg-dots x x x x x snap:lib-bg-done strips:off m m "                                                    \
    "5*s _ wait:pause snap:lib-shuffle m x x 5*w _ s _ x x s _ x x snap:lib-longplay _ x x x x x x x x x x x x x x wait:pause snap:lib-nowplaying"

// The same on a write-protected card (NEUPOD_DISK_READ_ONLY=1): the first open and Update Library both say "Library needs a writable card" and offer to try again.
#define TOUR_LIBRARY_RO "x _ _ x x x x x snap:lib-needs-writable m 6*s _ x x x x snap:lib-update-needs-writable"

// An empty library (LB-4): a card whose Music folder holds no audio. Artists builds the index at once, finds nothing and shows the empty pane; its button runs Update Library,
// which finishes with "0 songs" (the Done pane); Select returns and the list looks again.
#define TOUR_LIBRARY_EMPTY "x _ _ x x x x snap:lib-empty _ x x x snap:lib-empty-done _ x x snap:lib-empty-again m m"

// Text beyond ASCII (UI-17): Extras > Files on a card whose names hold accents, the ripper's U+00BF, curly quotes, CJK and an emoji
// (tests/tools/make_text_card.py), into the accented folder, then its FLAC (accented tags) on Now Playing. The track lasts 0.3 s: as in the art tour,
// Settings > Repeat is set to One first so the player ends up paused on it (wait:pause) instead of dropping to idle.
#define TOUR_TEXT "x s s s s _ s s s s s s _ m w _ _ x snap:text-files _ x snap:text-folder _ wait:pause x snap:text-nowplaying m m m m x x snap:text-home _ _ 5*s x snap:text-longrow"

// Hold (docs/adr/0020-hold.md): the Power key is simulated by pwr:down / pwr:up and sleep:MS lets time pass between them. A 1.2 s press shows the ring and
// does nothing; 2.1 s locks (confirmation toast, lock icon); keys while locked only remind; held on to 5.9 s the power-off countdown shows (and the
// 2 s mark unlocks again); 10.5 s asks for the power-off (the host only logs it) and locks once more; the hardware switch (sw:on / sw:off) takes it
// off and on; a tap (down, then up one 300 ms step later) turns the screen off (auto-lock locks), the next key only wakes it, a 2.3 s hold unlocks and the keys work again; Settings >
// Display shows the Screen off after and Auto-lock rows.
#define TOUR_HOLD                                                                                  \
    "x pwr:down sleep:800 snap:hold-ring pwr:up sleep:900 "                                        \
    "pwr:down sleep:1800 snap:hold-locked pwr:up sleep:300 s s snap:hold-swallowed x x x x x x x "  \
    "snap:hold-locked-idle pwr:down sleep:5400 snap:hold-countdown pwr:up sleep:600 snap:hold-cancelled "  \
    "pwr:down sleep:10200 snap:hold-poweroff pwr:up sleep:600 "                                     \
    "sw:off x x snap:hold-switch-off x x x x x sw:on x x snap:hold-switch-on sw:off x x x x x x x "    \
    "pwr:down pwr:up sleep:600 snap:hold-screen-off s x snap:hold-woke x x x x x x x "    \
    "pwr:down sleep:1800 pwr:up sleep:600 s snap:hold-unlocked-nav s s s _ s _ snap:hold-display "   \
    "s s s s _ x snap:hold-autolock-off m m"

// Settings > About > Licences (the third-party components and their licences): the About list, the Licences list, and the longest notice (BTstack).
#define TOUR_LICENCES "x s s s s _ _ snap:about _ snap:licences 11*s _ snap:licence-btstack m m m"

// The design system (docs/ui.md, DS-1 to DS-5): the toast strip (warning, info) and the volume HUD over Home (toast: / hud: / strips:off tokens, they stay
// until taken down), Settings > Display with its Theme row, the dark theme on Home and on a list (Select on the Theme row cycles Light, Dark, Auto;
// theme: sets it directly), and Auto: light while the backlight is at full, dark once it dims (dim:10 makes that 10 s; sleep: lets the UI clock run), light
// again on the next key. The ctest fixture wipes the stored settings first.
#define TOUR_DESIGN                                                                                \
    "x snap:ds-home toast:warn:Headset_disconnected x snap:ds-toast-warn strips:off x "            \
    "toast:info:Headset_connected x snap:ds-toast-info strips:off x hud:Volume_-18_dB:62 x snap:ds-hud strips:off x " \
    "s s s s _ x snap:ds-settings s x snap:ds-settings-display s s s x snap:ds-settings-gapless _ x snap:ds-settings-toggled _ w w w _ x snap:ds-display _ x snap:ds-display-dark m m "  \
    "x snap:ds-home-dark w w w w _ x snap:ds-music-dark m theme:auto x x snap:ds-home-auto "          \
    "dim:10 sleep:11500 snap:ds-home-auto-dim s x snap:ds-home-auto-wake theme:light x x"

// Music > Playlists (LB-7) on the library card with its playlist files (tests/tools/make_library_card.py --playlists: On-The-Go seeded with three
// songs and a deleted one, /Playlists/Night drive.m3u with relative paths and bad entries, /Running.m3u8 with a byte-order mark and CRLF). Artists builds
// the index first. The catalog; On-The-Go with its greyed row (Select: toast), a long press of Select removing it; a song played from the list for
// more than half of its 3 s (Most played then has it); the empty states (Top rated); Recently added, Unplayed; the two playlist files; and Add to
// On-The-Go from Now Playing page 2 (the track is paused first so the pictures do not depend on the clock).
#define TOUR_PLAYLISTS                                                                                                                    \
    "x _ _ wait:lib x x m 5*s _ x x x snap:pl-catalog-first "                                                                           \
    "_ x x x snap:pl-otg 3*s _ x snap:pl-otg-greyed sel:down sleep:700 sel:up x x snap:pl-otg-removed 4*w _ x x x x snap:pl-np "         \
    "sleep:1800 wait:pause m x x snap:pl-otg-back m x x x snap:pl-catalog "                                                                          \
    "s _ x x x x x snap:pl-recent m s _ x x x snap:pl-most m s _ x x x snap:pl-top-empty m s _ x x x x x snap:pl-unplayed "              \
    "m s _ x x x snap:pl-m3u-night 3*s _ x snap:pl-m3u-greyed m s _ x x x snap:pl-m3u-running "                                           \
    "2*s _ x wait:pause x _ x 3*s x snap:pl-np-page2 _ x x snap:pl-np-added m m m m m s s s s s s _ x x x snap:pl-otg-after m m m"

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
        fprintf(stderr, "host: FAIL expected %u snapshots, got %u (the script did not finish before --run-ms ran out?)\n", cfgp_tour ? g_expect : 2u, g_snapshots);
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
        } else if (strcmp(argv[i], "--tour-bt") == 0) {
            cfg.ui_script = TOUR_BT;
            cfgp_tour = true;
            g_expect = 2 + 24; // the splash, home and the named snapshots of the script
        } else if (strcmp(argv[i], "--tour-art") == 0) {
            cfg.ui_script = TOUR_ART;
            cfgp_tour = true;
            g_expect = 4;
        } else if (strcmp(argv[i], "--tour-seek") == 0) {
            cfg.ui_script = TOUR_SEEK;
            cfgp_tour = true;
            g_expect = 12; // the splash, home and the ten named snapshots
        } else if (strcmp(argv[i], "--tour-library") == 0) {
            cfg.ui_script = TOUR_LIBRARY;
            cfgp_tour = true;
            g_expect = 27; // the splash, home and the twenty-five named snapshots
        } else if (strcmp(argv[i], "--tour-library-ro") == 0) {
            cfg.ui_script = TOUR_LIBRARY_RO;
            cfgp_tour = true;
            g_expect = 4;
        } else if (strcmp(argv[i], "--tour-library-empty") == 0) {
            cfg.ui_script = TOUR_LIBRARY_EMPTY;
            cfgp_tour = true;
            g_expect = 5; // the splash, home and the three named snapshots
        } else if (strcmp(argv[i], "--tour-licences") == 0) {
            cfg.ui_script = TOUR_LICENCES;
            cfgp_tour = true;
            g_expect = 5; // the splash, home and the three named snapshots
        } else if (strcmp(argv[i], "--tour-hold") == 0) {
            cfg.ui_script = TOUR_HOLD;
            cfgp_tour = true;
            g_expect = 16; // the splash, home and the fourteen named snapshots
        } else if (strcmp(argv[i], "--tour-design") == 0) {
            cfg.ui_script = TOUR_DESIGN;
            cfgp_tour = true;
            g_expect = 17; // the splash, home and the fifteen named snapshots
        } else if (strcmp(argv[i], "--tour-text") == 0) {
            cfg.ui_script = TOUR_TEXT;
            cfgp_tour = true;
            g_expect = 7; // the splash, home and the five named snapshots
        } else if (strcmp(argv[i], "--tour-playlists") == 0) {
            cfg.ui_script = TOUR_PLAYLISTS;
            cfgp_tour = true;
            g_expect = 2 + 17; // the splash, home and the seventeen named snapshots
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            cfg.ui_script = argv[++i]; // ad-hoc key script, same tokens as the tour
        } else if (strcmp(argv[i], "--splash-hold-ms") == 0 && i + 1 < argc) {
            cfg.splash_hold_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--snapshots") == 0 && i + 1 < argc) {
            g_snapshot_dir = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--run-ms N] [--splash-hold-ms N] [--tour|--tour-wifi|--tour-bt|--tour-art|--tour-seek|--tour-library|--tour-library-ro|--tour-library-empty|--tour-playlists|--tour-text|--tour-hold|--tour-licences|--tour-design] [--script KEYS] [--snapshots DIR]\n", argv[0]);
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

// SPDX-License-Identifier: MPL-2.0
/*
 * UI task. The only code that calls LVGL (LV_USE_OS = NONE), so no locking is needed.
 * Timeline: display init -> first splash frame -> start the boot checks -> animate until all
 * checks resolved (min 1.5 s, max 5 s) -> fade to the home screen -> event loop.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battery_store.h"
#include "art_service.h"
#include "boot.h"
#include "capabilities.h"
#include "hal_display.h"
#include "hal_system.h"
#include "input_events.h"
#include "os.h"
#include "player.h"
#include "runtime.h"
#include "runtime_config.h"
#include "ui.h"
#include "ui_internal.h"

#define SPLASH_MIN_MS RT_SPLASH_MIN_MS
#define SPLASH_MAX_MS 5000u
#define FADE_MS 400u
#define HOME_SNAPSHOT_AFTER_MS 900u // after the fade has finished

// Two partial-render buffers of UI_BUF_W_MAX x UI_BUF_LINES_MAX RGB565: 320 x 24 = 15 KB each on the product build; the
// hosted (NEUPOD_UI_HIRES) build sizes them for the 480x360 profile. Only the profile's own w x buf_lines is handed to LVGL.
static uint8_t g_buf1[UI_BUF_W_MAX * UI_BUF_LINES_MAX * 2] __attribute__((aligned(4)));
static uint8_t g_buf2[UI_BUF_W_MAX * UI_BUF_LINES_MAX * 2] __attribute__((aligned(4)));

// Blocking flush: set the window, DMA the strip, wait. Async (render the next strip while the
// previous one is on the wire) is possible later because there are two buffers.
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint16_t w = (uint16_t)lv_area_get_width(area);
    uint16_t h = (uint16_t)lv_area_get_height(area);
    if (hal_display_set_window((uint16_t)area->x1, (uint16_t)area->y1, w, h) == HAL_OK) {
        (void)hal_display_blit((const hal_pixel_t *)(const void *)px_map, (size_t)w * h);
        hal_display_wait();
    }
    if (lv_display_flush_is_last(disp)) {
        hal_display_present(); // host: show / dump the frame; hardware: no-op
    }
    lv_display_flush_ready(disp);
}

static bool lvgl_setup(void) {
#ifndef NEUPOD_RP2350 // the product panel is fixed hardware: nothing to select or warn about (and no flash spent on the messages)
    const uint32_t pw = hal_display_width(), ph = hal_display_height();
    ui_metrics_set(ui_metrics_select(pw, ph)); // the product build has the one 320x240 profile (compile-time constant)
    printf("ui: profile %dx%d on %ux%u panel\n", (int)UI_W, (int)UI_H, (unsigned)pw, (unsigned)ph);
    if (pw > (uint32_t)UI_W || ph > (uint32_t)UI_H) {
#ifdef NEUPOD_UI_HIRES
        const char *hint = "";
#else
        const char *hint = " (this build has no hires profile: configure with -DNEUPOD_UI_HIRES=ON)";
#endif
        printf("ui: WARNING panel %ux%u is larger than the %dx%d profile: the UI fills the top-left corner only%s\n", (unsigned)pw, (unsigned)ph, (int)UI_W,
               (int)UI_H, hint);
    }
#endif
    lv_init();
    lv_tick_set_cb(os_tick_ms);
    lv_display_t *d = lv_display_create(UI_W, UI_H);
    if (d == NULL) {
        return false;
    }
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, g_buf1, g_buf2, (uint32_t)(UI_W * UIM->buf_lines * 2), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    return true;
}

static uint32_t clamp_ms(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- Perf counters (printed by the monitor): single writer (this task), plain 32-bit fields ----
static volatile ui_perf_t g_perf;
static volatile uint32_t g_perf_reset;
static uint32_t g_perf_reset_seen;
static uint64_t g_perf_sum_us;

bool ui_perf_get(ui_perf_t *out) {
    out->frames = g_perf.frames;
    out->frame_max_us = g_perf.frame_max_us;
    out->frame_avg_us = g_perf.frame_avg_us;
    out->mem_used = g_perf.mem_used;
    out->mem_total = g_perf.mem_total;
    out->mem_max_used = g_perf.mem_max_used;
    out->mem_frag_pct = g_perf.mem_frag_pct;
    return true;
}

void ui_perf_new_window(void) { g_perf_reset++; }

static void sample_lvgl_memory(void) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    g_perf.mem_total = (uint32_t)m.total_size;
    g_perf.mem_used = (uint32_t)(m.total_size - m.free_size);
    if (g_perf.mem_used > g_perf.mem_max_used) {
        g_perf.mem_max_used = g_perf.mem_used;
    }
    g_perf.mem_frag_pct = m.frag_pct;
}

// One pass of the UI machinery; returns how long to sleep.
static uint32_t pump(void) {
    uint64_t t0 = hal_micros();
    uint32_t next = lv_timer_handler();
    uint32_t dt = (uint32_t)(hal_micros() - t0);
    if (g_perf_reset_seen != g_perf_reset) {
        g_perf_reset_seen = g_perf_reset;
        g_perf.frames = g_perf.frame_max_us = g_perf.frame_avg_us = 0;
        g_perf_sum_us = 0;
    }
    if (dt > 500u) { // a pass that rendered something (an idle pass costs a few tens of microseconds)
        g_perf.frames++;
        g_perf_sum_us += dt;
        g_perf.frame_avg_us = (uint32_t)(g_perf_sum_us / g_perf.frames);
        if (dt > g_perf.frame_max_us) {
            g_perf.frame_max_us = dt;
        }
    }
    hal_display_service();
    return clamp_ms(next, 5, 30);
}

#define SCRIPT_STEP_MS 300u

// Host tour: run the next token of the script. Returns the rest of the script.
static void screen_ready(const char *name);
static const char *script_step(const char *p, uint32_t now, bool *done) {
    while (*p == ' ') {
        p++;
    }
    if (*p == '\0') {
        *done = true;
        const neupod_runtime_config_t *cfg = rt_config();
        if (cfg->on_done != NULL) {
            cfg->on_done(cfg->ctx);
        }
        return p;
    }
    const char *end = p;
    while (*end != '\0' && *end != ' ') {
        end++;
    }
    if (strncmp(p, "wait:", 5) == 0) {
        /* Tour only: hold the script (the step repeats every 300 ms) until the player / art service reach the state ("pause" presses Select until paused), so snapshots do
         * not depend on wall-clock timing. A wait that never ends runs into the tour's time limit and fails its snapshot count. */
        player_info_t pi;
        player_get(&pi);
        art_view_t av;
        art_view_get(&av);
        if (strncmp(p + 5, "pause", 5) == 0 && pi.state == PLAYER_PLAYING) {
            /* Select pauses; a press that lands on a repeat-one restart is lost, so it is repeated until the player says paused */
            input_event_t evs[2];
            size_t cnt = input_events_from_key('\r', evs, 2);
            for (size_t i = 0; i < cnt; i++) {
                ui_frame_input(&evs[i], now);
            }
        }
        bool ok = (strncmp(p + 5, "pause", 5) == 0 && pi.state == PLAYER_PAUSED) ||
                  (strncmp(p + 5, "art", 3) == 0 && pi.state == PLAYER_PAUSED && av.state == ART_VIEW_READY);
        return ok ? end : p;
    }
    if (strncmp(p, "snap:", 5) == 0) {
        char name[32];
        size_t n = (size_t)(end - (p + 5));
        n = n < sizeof name - 1 ? n : sizeof name - 1;
        memcpy(name, p + 5, n);
        name[n] = '\0';
        screen_ready(name);
    } else {
        unsigned repeat = 1; /* "N*keys" repeats the keys N times in one step */
        const char *keys = p;
        unsigned n = 0;
        const char *d = p;
        while (d < end && *d >= '0' && *d <= '9') {
            n = n * 10u + (unsigned)(*d - '0');
            d++;
        }
        if (d > p && d < end && *d == '*') {
            repeat = n;
            keys = d + 1;
        }
        for (unsigned r = 0; r < repeat; r++) {
            for (const char *k = keys; k < end; k++) {
                input_event_t evs[2];
                size_t cnt = input_events_from_key(*k == '_' ? '\r' : *k, evs, 2); /* "_" = Enter */
                for (size_t i = 0; i < cnt; i++) {
                    ui_frame_input(&evs[i], now + r * 300u); /* spaced out so acceleration stays at 1x */
                }
            }
        }
    }
    return end;
}

// Any input event (wheel, buttons, testbed console keys, host keyboard: all arrive through the input
// queue) skips the splash hold.
static bool skip_requested(void) {
    input_event_t ev;
    bool skip = false;
    while (os_queue_recv(rt_input_queue(), &ev, OS_NO_WAIT)) {
        skip = true;
    }
    return skip;
}

static void screen_ready(const char *name) {
    lv_refr_now(NULL); // make sure the screen's latest state is on the panel before anyone looks
    sample_lvgl_memory();
    printf("ui: screen '%s' lvgl_mem=%u/%uB depth=%d\n", name, (unsigned)g_perf.mem_used, (unsigned)g_perf.mem_total, ui_frame_depth());
#ifdef NEUPOD_UI_HIRES
    if (getenv("NEUPOD_UI_AUDIT") != NULL) { // host tests: every snapshot is also a layout check
        int bad = ui_layout_audit(name);
        if (bad != 0) {
            printf("ui: AUDIT FAIL screen '%s': %d layout violation(s)\n", name, bad);
        }
    }
#endif
    const neupod_runtime_config_t *cfg = rt_config();
    if (cfg->on_screen != NULL) {
        cfg->on_screen(name, cfg->ctx);
    }
}

// Every push and pop goes through here (the bench has no wheel and no view of the panel: the UART tells where we are).
void ui_log_screen(const char *title, const char *what, const char *now_top) {
    sample_lvgl_memory();
    printf("ui: screen '%s' %s", title, what);
    if (now_top != NULL) {
        printf(" -> '%s'", now_top);
    }
    printf(" depth=%d lvgl_mem=%u/%uB\n", ui_frame_depth(), (unsigned)g_perf.mem_used, (unsigned)g_perf.mem_total);
}

void ui_task(void *arg) {
    (void)arg;
    // Display first: the splash must be the first thing the user sees.
    hal_status_t dst = hal_display_init();
    bool ok = dst == HAL_OK && lvgl_setup();
    if (!ok) {
        caps_resolve(CAP_DISPLAY, false, "init failed", "display init failed");
        boot_start(); // still run (and log) the other checks
        for (;;) {
            (void)boot_poll();
            os_delay_ms(250);
        }
    }

    ui_splash_create();
    caps_resolve(CAP_DISPLAY, true, NULL, "%s %ux%u", hal_display_name(), (unsigned)hal_display_width(),
                 (unsigned)hal_display_height());
    capabilities_t caps;
    caps_get(&caps);
    ui_splash_update(&caps);
    lv_refr_now(NULL);
    hal_display_backlight(255); // only now: no flash of garbage before the first frame
    uint32_t t0 = os_tick_ms();
    boot_start();

    // --- Splash: animate while the checks run; hold if any came out not fully correct ---
    const uint32_t hold_ms = rt_config()->splash_hold_ms != 0 ? rt_config()->splash_hold_ms : RT_SPLASH_HOLD_MS;
    bool holding = false;
    uint32_t hold_t0 = 0;
    for (;;) {
        bool all = boot_poll();
        caps_get(&caps);
        ui_splash_update(&caps);
        uint32_t now = os_tick_ms();
        uint32_t elapsed = now - t0;
        if ((all && elapsed >= SPLASH_MIN_MS) || elapsed >= SPLASH_MAX_MS) {
            (void)boot_poll(); // anything still pending at the 5 s cap is marked degraded
            caps_get(&caps);
            ui_splash_update(&caps);
            bool any_bad = false;
            for (int i = 0; i < CAP_COUNT; i++) {
                any_bad = any_bad || caps.state[i] == CAP_DEGRADED;
            }
            if (!any_bad) {
                break; // everything passed: no extra delay
            }
            if (!holding) {
                holding = true;
                hold_t0 = now; // the hold runs from the moment the checks are done (and the minimum has passed)
            }
            if (now - hold_t0 >= hold_ms || skip_requested()) {
                break;
            }
            ui_splash_hold_update(&caps, now - hold_t0, hold_ms);
        }
        os_delay_ms(pump());
    }
    screen_ready("splash"); // final state of the splash, for the host snapshot hook

    // --- Transition to the home menu ---
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_frame_init(scr);
    ui_frame_push(ui_screen_home_create());
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_FADE_IN, FADE_MS, 0, true);
    uint32_t home_at = os_tick_ms();
    bool home_reported = false;
    const char *script = rt_config()->ui_script;
    uint32_t script_next = home_at + FADE_MS + HOME_SNAPSHOT_AFTER_MS + 400u;
    bool script_done = false;
    uint32_t mem_at = 0;

    for (;;) {
        uint32_t now = os_tick_ms();
        input_event_t ev;
        while (os_queue_recv(rt_input_queue(), &ev, OS_NO_WAIT)) {
            ui_frame_input(&ev, now);
        }
        if (!home_reported && now - home_at >= FADE_MS + HOME_SNAPSHOT_AFTER_MS) {
            home_reported = true;
            screen_ready("home");
        }
        if (script != NULL && !script_done && home_reported && (int32_t)(now - script_next) >= 0) {
            script_next = now + SCRIPT_STEP_MS;
            script = script_step(script, now, &script_done);
        }
        ui_frame_tick(now);
        if (now - mem_at >= 1000u) {
            mem_at = now;
            sample_lvgl_memory();
        }
        os_delay_ms(pump());
    }
}

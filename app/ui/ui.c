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
#include "hal_radio.h"
#include "hal_system.h"
#include "input_events.h"
#include "library_service.h"
#include "os.h"
#include "player.h"
#include "runtime.h"
#include "runtime_config.h"
#include "ui.h"
#include "ui_input.h"
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
extern void hal_host_bt_drop_link(void) __attribute__((weak));
extern void hal_host_bt_headset_volume(uint8_t volume) __attribute__((weak));
extern void hal_host_bt_headset_key(hal_bt_key_t key) __attribute__((weak));
extern void hal_host_power_key(bool down) __attribute__((weak));
extern void hal_host_hold_switch(hal_hold_switch_t sw) __attribute__((weak));
static uint32_t g_script_sleep_until;
static bool g_script_sleeping;
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
        /* Tour only: hold the script (the step repeats every 300 ms) until the player / art service reach the state ("pause" presses Play until paused), so snapshots do
         * not depend on wall-clock timing. A wait that never ends runs into the tour's time limit and fails its snapshot count. */
        player_info_t pi;
        player_get(&pi);
        art_view_t av;
        art_view_get(&av);
        if (strncmp(p + 5, "pause", 5) == 0 && pi.state == PLAYER_PLAYING) {
            /* Play / Pause pauses (Select is the seek-mode key on Now Playing); a press that lands on a repeat-one restart is lost, so it is
             * repeated until the player says paused */
            input_event_t evs[2];
            size_t cnt = input_events_from_key('p', evs, 2);
            for (size_t i = 0; i < cnt; i++) {
                ui_frame_input(&evs[i], now);
            }
        }
        bool ok = (strncmp(p + 5, "lib", 3) == 0 && library_known_ready()) || /* the library index has been opened (the first build is done) */
                  (strncmp(p + 5, "pause", 5) == 0 && pi.state == PLAYER_PAUSED) ||
                  (strncmp(p + 5, "art", 3) == 0 && pi.state == PLAYER_PAUSED && av.state == ART_VIEW_READY);
        return ok ? end : p;
    }
    if (strncmp(p, "sleep:", 6) == 0) {
        /* Tour only: let N ms of the UI clock pass before the next token (a Power hold of 2 s needs 2 s, not seven 300 ms steps). */
        if (!g_script_sleeping) {
            g_script_sleeping = true;
            g_script_sleep_until = now + (uint32_t)atoi(p + 6);
        }
        if ((int32_t)(now - g_script_sleep_until) < 0) {
            return p;
        }
        g_script_sleeping = false;
        return end;
    }
    if (strncmp(p, "sel:", 4) == 0) {
        /* Tour only: Select held (sel:down ... sleep:MS ... sel:up): the hold that enters Now Playing's seek mode. */
        input_event_t ev = {.type = strncmp(p + 4, "down", 4) == 0 ? INPUT_BUTTON_DOWN : INPUT_BUTTON_UP, .button = HAL_BTN_SELECT};
        ui_frame_input(&ev, now);
        return end;
    }
    if (strncmp(p, "menu:", 5) == 0) {
        /* Tour only: Menu held (menu:down ... sleep:MS ... menu:up): the long press that clears a text entry. */
        input_event_t ev = {.type = strncmp(p + 5, "down", 4) == 0 ? INPUT_BUTTON_DOWN : INPUT_BUTTON_UP, .button = HAL_BTN_MENU};
        ui_frame_input(&ev, now);
        return end;
    }
    if (strncmp(p, "vol:", 4) == 0) {
        /* Tour only: the volume changes by N steps from outside the screen (a side key, like the Y2's): the HUD shows on screens that do not draw it. */
        (void)player_volume_step(atoi(p + 4));
        return end;
    }
    if (strncmp(p, "rail:", 5) == 0) {
        ui_letter_overlay_tour_hold(strncmp(p + 5, "hold", 4) == 0); /* Tour only: keep the fast-scroll overlay up for a snapshot */
        return end;
    }
    if (strncmp(p, "pwr:", 4) == 0) {
        /* Tour only (host simulator): the Power key goes down (pwr:down) or up (pwr:up); the input task turns the level into events. */
        if (hal_host_power_key != NULL) {
            hal_host_power_key(strncmp(p + 4, "down", 4) == 0);
        }
        return end;
    }
    if (strncmp(p, "sw:", 3) == 0) {
        /* Tour only: the hardware Hold switch (sw:on / sw:off) */
        if (hal_host_hold_switch != NULL) {
            hal_host_hold_switch(strncmp(p + 3, "on", 2) == 0 ? HAL_HOLD_SWITCH_ON : HAL_HOLD_SWITCH_OFF);
        }
        return end;
    }
    if (strncmp(p, "toast:", 6) == 0 || strncmp(p, "hud:", 4) == 0) {
        /* Tour only (design-system strips): toast:warn:Text_with_underscores / toast:info:Text, hud:Label_text:PCT (PCT -1 = no bar); settled at once for the snapshot. */
        char txt[64];
        bool hud = p[0] == 'h';
        const char *a = p + (hud ? 4 : 6);
        bool info = !hud && strncmp(a, "info:", 5) == 0;
        if (!hud) {
            a += 5; /* "warn:" and "info:" are both five characters */
        }
        size_t n = 0;
        while (a + n < end && a[n] != ':' && n < sizeof txt - 1) {
            txt[n] = a[n] == '_' ? ' ' : a[n];
            n++;
        }
        txt[n] = '\0';
        if (hud) {
            ui_hud_show(txt, a + n < end ? atoi(a + n + 1) : -1, 60000u);
        } else {
            ui_toast_show(txt, info ? UI_TOAST_INFO : UI_TOAST_WARN, 60000u);
        }
        return end;
    }
    if (strncmp(p, "theme:", 6) == 0) {
        /* Tour only: Settings > Display > Theme set directly (light, dark, auto). */
        ui_theme_mode_t m = strncmp(p + 6, "dark", 4) == 0 ? UI_THEME_DARK : strncmp(p + 6, "auto", 4) == 0 ? UI_THEME_AUTO : UI_THEME_LIGHT;
        g_ui_state.theme = m;
        ui_apply_theme();
        return end;
    }
    if (strncmp(p, "dim:", 4) == 0) {
        /* Tour only: Auto-dim on, dimming after the first choice (10 s), so a tour can reach the dimmed backlight with sleep:. */
        g_ui_state.autodim = true;
        g_ui_state.dim_idx = 0;
        return end;
    }
    if (strncmp(p, "strips:off", 10) == 0) {
        ui_toast_hide(); /* Tour only: take the strips down again */
        ui_hud_hide();
        return end;
    }
    if (strncmp(p, "bt:drop", 7) == 0) {
        /* Tour only (host simulator): the headset goes away now. Weak, so builds without the simulator simply skip it. */
        if (hal_host_bt_drop_link != NULL) {
            hal_host_bt_drop_link();
        }
        return end;
    }
    if (strncmp(p, "bt:vol:", 7) == 0) {
        /* Tour only: the headset's own volume changes (a swipe on the stem), in percent. */
        if (hal_host_bt_headset_volume != NULL) {
            hal_host_bt_headset_volume((uint8_t)((unsigned)atoi(p + 7) * HAL_BT_VOLUME_MAX / 100u));
        }
        return end;
    }
    if (strncmp(p, "bt:key:", 7) == 0) {
        /* Tour only: a button on the headset (next, prev, play, pause). */
        hal_bt_key_t k = strncmp(p + 7, "next", 4) == 0 ? HAL_BT_KEY_NEXT : strncmp(p + 7, "prev", 4) == 0 ? HAL_BT_KEY_PREV : strncmp(p + 7, "pause", 5) == 0 ? HAL_BT_KEY_PAUSE : HAL_BT_KEY_PLAY;
        if (hal_host_bt_headset_key != NULL) {
            hal_host_bt_headset_key(k);
        }
        return end;
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

// Input during the splash. Events that arrive while the checks run are kept (up to BOOT_KEYS) and handed to the UI once the splash is over, as before;
// the state of the Menu key is followed so that Menu held at start (down, not yet released when the checks end) asks for the debug splash.
// Any other input event skips the debug splash's hold; the event that does is dropped, and so is Menu's own press and release.
#define BOOT_KEYS 16u
typedef struct {
    input_event_t ev[BOOT_KEYS];
    unsigned n;
    bool menu_down;
} boot_in_t;

static bool boot_input(boot_in_t *b, bool holding) {
    input_event_t ev;
    bool skip = false;
    while (os_queue_recv(rt_input_queue(), &ev, OS_NO_WAIT)) {
        bool menu = (ev.type == INPUT_BUTTON_DOWN || ev.type == INPUT_BUTTON_UP) && ev.button == HAL_BTN_MENU;
        if (menu) {
            b->menu_down = ev.type == INPUT_BUTTON_DOWN;
        } else if (holding) {
            skip = skip || ev.type == INPUT_SCROLL || ev.type == INPUT_BUTTON_DOWN || ev.type == INPUT_BUTTON_UP || ev.type == INPUT_POWER_DOWN;
        }
        if (!holding && !menu && b->n < BOOT_KEYS) {
            b->ev[b->n++] = ev;
        }
    }
    return skip;
}

static void screen_ready(const char *name) {
    ui_frame_settle(); // a slide or a strip still moving jumps to its end: the picture (and the layout audit) is of the settled screen
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

    // --- Splash: the release splash (wordmark, progress line, check icons, version) for at least SPLASH_MIN_MS while the checks run. If a check came out not fully
    // correct, or Menu is held, it becomes the debug splash (the list of failures) and is held ---
    const uint32_t hold_ms = rt_config()->splash_hold_ms != 0 ? rt_config()->splash_hold_ms : RT_SPLASH_HOLD_MS;
    const uint8_t mode = rt_config()->splash_mode;
    boot_in_t boot_in = {.n = 0, .menu_down = false};
    bool holding = false;
    uint32_t hold_t0 = 0;
    for (;;) {
        bool all = boot_poll();
        caps_get(&caps);
        ui_splash_update(&caps);
        (void)boot_input(&boot_in, false);
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
            bool debug = mode == 1 || (mode != 2 && (any_bad || boot_in.menu_down));
            if (!debug) {
                break; // everything passed: no extra delay
            }
            if (!holding) {
                holding = true;
                hold_t0 = now; // the hold runs from the moment the checks are done (and the minimum has passed)
                boot_in.n = 0; // the hold drops every key and release: a kept press whose release was dropped would reach Home as a press that never ends
                ui_splash_show_debug(&caps);
            }
            if (now - hold_t0 >= hold_ms || boot_input(&boot_in, true)) {
                break;
            }
            ui_splash_hold_update(now - hold_t0, hold_ms);
        }
        os_delay_ms(pump());
    }
    screen_ready("splash"); // final state of the splash, for the host snapshot hook

    for (unsigned i = 0; i < boot_in.n; i++) { // what was pressed while the splash showed goes to the UI now
        (void)os_queue_send(rt_input_queue(), &boot_in.ev[i], OS_NO_WAIT);
    }
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
    // The hardware Hold switch may have been slid before the UI existed (its event was dropped with the splash's): take its position now.
    if (hal_input_hold_switch() == HAL_HOLD_SWITCH_ON) {
        ui_frame_input(&(input_event_t){.type = INPUT_HOLD_SWITCH, .steps = 1}, os_tick_ms());
    }

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
        // Sleep until the next UI pass is due, or until an input event arrives: a press must not wait out the sleep (UI-6).
        uint32_t wait = pump();
        if (os_queue_recv(rt_input_queue(), &ev, wait)) {
            ui_frame_input(&ev, os_tick_ms());
        }
    }
}

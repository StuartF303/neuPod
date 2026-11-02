// SPDX-License-Identifier: MPL-2.0
/*
 * UI task. The only code that calls LVGL (LV_USE_OS = NONE), so no locking is needed.
 * Timeline: display init -> first splash frame -> start the boot checks -> animate until all
 * checks resolved (min 1.5 s, max 5 s) -> fade to the home screen -> event loop.
 */
#include <stdio.h>

#include "battery_store.h"
#include "boot.h"
#include "capabilities.h"
#include "hal_display.h"
#include "input_events.h"
#include "os.h"
#include "runtime.h"
#include "runtime_config.h"
#include "ui.h"
#include "ui_internal.h"

#define SPLASH_MIN_MS RT_SPLASH_MIN_MS
#define SPLASH_MAX_MS 5000u
#define FADE_MS 400u
#define HOME_SNAPSHOT_AFTER_MS 900u // after the fade has finished

#define BUF_LINES 24 // two partial-render buffers of 320 x 24 RGB565 = 15 KB each
static uint8_t g_buf1[UI_W * BUF_LINES * 2] __attribute__((aligned(4)));
static uint8_t g_buf2[UI_W * BUF_LINES * 2] __attribute__((aligned(4)));

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
    lv_init();
    lv_tick_set_cb(os_tick_ms);
    lv_display_t *d = lv_display_create(UI_W, UI_H);
    if (d == NULL) {
        return false;
    }
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, g_buf1, g_buf2, sizeof g_buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    return true;
}

static uint32_t clamp_ms(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

// One pass of the UI machinery; returns how long to sleep.
static uint32_t pump(void) {
    uint32_t next = lv_timer_handler();
    hal_display_service();
    return clamp_ms(next, 5, 30);
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
    const neupod_runtime_config_t *cfg = rt_config();
    if (cfg->on_screen != NULL) {
        cfg->on_screen(name, cfg->ctx);
    }
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
    caps_resolve(CAP_DISPLAY, true, NULL, "ST7789 %ux%u", (unsigned)hal_display_width(),
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

    // --- Transition to the placeholder home screen ---
    lv_obj_t *home = ui_home_create(&caps);
    lv_screen_load_anim(home, LV_SCREEN_LOAD_ANIM_FADE_IN, FADE_MS, 0, true);
    uint32_t home_at = os_tick_ms();
    bool home_reported = false;

    for (;;) {
        hal_battery_status_t bat;
        bool have = battery_store_get(&bat);
        ui_home_set_battery(have, &bat);
        input_event_t ev;
        while (os_queue_recv(rt_input_queue(), &ev, OS_NO_WAIT)) {
            ui_home_set_input(&ev);
        }
        if (!home_reported && os_tick_ms() - home_at >= FADE_MS + HOME_SNAPSHOT_AFTER_MS) {
            home_reported = true;
            screen_ready("home");
        }
        os_delay_ms(pump());
    }
}

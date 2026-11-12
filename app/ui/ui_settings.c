// SPDX-License-Identifier: MPL-2.0
/*
 * Settings: groups on the left, a live summary pane on the right (docs/ui.md mock-up). Select opens a
 * detail screen (About, Storage, Battery: live read-only lists; Display: backlight; Bluetooth: status and device discovery)
 * or, for Click, cycles Piezo / Haptic / Both / Off. Haptics opens the actuator (ERM / LRA) and Calibrate rows.
 * Backlight, click mode and haptics choices are stored in the settings store (settings_store.h).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "audio_engine.h"
#include "capabilities.h"
#include "hal_battery.h"
#include "hal_core.h"
#include "hal_display.h"
#include "hal_radio.h"
#include "hal_storage.h"
#include "hal_system.h"
#include "haptic_service.h"
#include "neupod_version.h"
#include "nfs.h"
#include "os.h"
#include "player.h"
#include "storage_service.h"
#include "ui_input.h"
#include "wifi_service.h"
#include "ui_internal.h"

static uint32_t g_space_seq;

/* ---- helpers ---------------------------------------------------------------------------------------- */

static void fmt_uptime(char *out, size_t n) {
    uint32_t sec = os_tick_ms() / 1000u;
    snprintf(out, n, "%u:%02u:%02u", (unsigned)(sec / 3600u), (unsigned)(sec / 60u % 60u), (unsigned)(sec % 60u));
}

static void put(ui_list_item_t *o, const char *label, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void put(ui_list_item_t *o, const char *label, const char *fmt, ...) {
    snprintf(o->label, sizeof o->label, "%s", label);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(o->detail, sizeof o->detail, fmt, ap);
    va_end(ap);
}

/* ---- detail screens: a live read-only list -------------------------------------------------------- */

typedef enum { INFO_ABOUT, INFO_STORAGE, INFO_BATTERY } info_kind_t;

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    info_kind_t kind;
    audio_stats_t audio;
    bool audio_ok;
    capabilities_t caps;
    hal_battery_status_t bat;
    bool bat_ok;
    bool space_ok;
    uint64_t space_total, space_free;
    uint32_t last_ms;
} info_t;

#define ABOUT_FIXED 10

static int32_t info_count(const info_t *in) {
    switch (in->kind) {
    case INFO_ABOUT:
        return 1 + ABOUT_FIXED + CAP_COUNT; /* row 0 opens the Licences screen */
    case INFO_STORAGE:
        return 5;
    case INFO_BATTERY:
        return 6;
    }
    return 0;
}

static void about_get(const info_t *in, int32_t i, ui_list_item_t *o) {
    char b[24];
    if (i == 0) {
        snprintf(o->label, sizeof o->label, "Licences");
        o->chevron = true;
        return;
    }
    i--; /* the rows below are the live read-outs */
    switch (i) {
    case 0: put(o, "Version", "%s", NEUPOD_VERSION); break;
    case 1: put(o, "Build", "%s", NEUPOD_GIT_HASH); break;
    case 2: put(o, "Platform", "%s", hal_platform_name()); break;
    case 3: fmt_uptime(b, sizeof b); put(o, "Uptime", "%s", b); break;
    case 4: ui_format_size(os_heap_free(), b, sizeof b); put(o, "Heap free", "%s", b); break;
    case 5: put(o, "Audio blocks", in->audio_ok ? "%u" : "-", (unsigned)in->audio.blocks); break;
    case 6: put(o, "Underruns", in->audio_ok ? "%u" : "-", (unsigned)in->audio.underruns); break;
    case 7: put(o, "Jitter max", in->audio_ok ? "%u us" : "-", (unsigned)in->audio.jitter_max_us); break;
    case 8: put(o, "Core 1 stack free", "%u B", (unsigned)hal_core1_stack_free_min()); break;
    case 9: {
        hal_bt_stats_t bs;
        hal_bt_stats(&bs);
        if (hal_bt_stream_state() == HAL_BT_STREAM_CLOSED && bs.frames_encoded == 0) {
            put(o, "BT audio", "-");
        } else {
            put(o, "BT audio", "%u fr, %u under, %u drift", (unsigned)bs.frames_encoded, (unsigned)bs.underruns, (unsigned)bs.drift_corrections);
        }
        break;
    }
    default: {
        cap_id_t c = (cap_id_t)(i - ABOUT_FIXED);
        if (in->caps.state[c] == CAP_OK) {
            put(o, cap_name(c), "ok");
        } else if (in->caps.state[c] == CAP_PENDING) {
            put(o, cap_name(c), "checking");
        } else {
            put(o, cap_name(c), "%s", in->caps.brief[c]);
            o->dim = true;
        }
    }
    }
}

static void storage_get(const info_t *in, int32_t i, ui_list_item_t *o) {
    hal_card_info_t ci;
    bool have = hal_storage_ready() && hal_storage_get_info(&ci) == HAL_STORAGE_OK;
    char b[24];
    switch (i) {
    case 0: put(o, "Card", "%s", have ? hal_storage_card_type_str(ci.type) : "none"); break;
    case 1:
        if (have) {
            ui_format_size(ci.capacity_bytes, b, sizeof b);
        } else {
            snprintf(b, sizeof b, "-");
        }
        put(o, "Size", "%s", b);
        break;
    case 2: put(o, "Filesystem", "%s", hal_storage_is_mounted() ? (hal_storage_fs_type() == HAL_FS_EXFAT ? "exFAT" : hal_storage_fs_type() == HAL_FS_FAT32 ? "FAT32" : hal_storage_fs_type() == HAL_FS_FAT16 ? "FAT16" : hal_storage_fs_type() == HAL_FS_FAT12 ? "FAT12" : "?") : "not mounted"); break;
    case 3: put(o, "Mode", "%s", hal_storage_get_write_enabled() ? "read-write" : "read-only"); break;
    default:
        if (in->space_ok) {
            ui_format_size(in->space_free, b, sizeof b);
            put(o, "Free", "%s", b);
        } else {
            put(o, "Free", "%s", caps_available(CAP_STORAGE) ? "computing..." : "-");
        }
    }
}

static void battery_get(const info_t *in, int32_t i, ui_list_item_t *o) {
    const hal_battery_status_t *b = &in->bat;
    int32_t r = b->rate_mpct_per_h;
    switch (i) {
    case 0: put(o, "Voltage", in->bat_ok ? "%u mV" : "-", (unsigned)b->mv); break;
    case 1: put(o, "Charge", in->bat_ok ? "%u %%" : "-", (unsigned)b->soc_pct); break;
    case 2:
        if (in->bat_ok) {
            put(o, "Rate", "%s%d.%d %%/h", r < 0 ? "-" : "", (int)((r < 0 ? -r : r) / 1000), (int)(((r < 0 ? -r : r) % 1000) / 100));
        } else {
            put(o, "Rate", "-");
        }
        break;
    case 3: put(o, "State", "%s", !in->bat_ok ? "-" : r > 2000 ? "charging" : r < -2000 ? "discharging" : "idle"); break;
    case 4: put(o, "Alerts", in->bat_ok ? "0x%02X" : "-", (unsigned)b->alerts); break;
    default: put(o, "Gauge", "%s", caps_available(CAP_BATTERY) ? "ok" : "unavailable"); break;
    }
}

static void info_get(void *ctx, int32_t i, ui_list_item_t *o) {
    const info_t *in = ctx;
    switch (in->kind) {
    case INFO_ABOUT: about_get(in, i, o); break;
    case INFO_STORAGE: storage_get(in, i, o); break;
    case INFO_BATTERY: battery_get(in, i, o); break;
    }
}

static void info_sample(info_t *in) {
    in->audio_ok = audio_stats_get(&in->audio);
    caps_get(&in->caps);
    in->bat_ok = battery_store_get(&in->bat);
    storage_result_t r;
    while (in->kind == INFO_STORAGE && storage_result_poll(&r)) {
        if (r.kind == STORAGE_RES_SPACE && r.seq == g_space_seq && r.err == NFS_OK) {
            in->space_ok = true;
            in->space_total = r.total_bytes;
            in->space_free = r.free_bytes;
        }
    }
}

static void info_tick(ui_screen_t *s) {
    info_t *in = (info_t *)s;
    uint32_t now = os_tick_ms();
    if (now - in->last_ms < 1000u) {
        return; /* values change slowly: refresh once a second */
    }
    in->last_ms = now;
    info_sample(in);
    ui_list_refresh(in->list);
}

static void info_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((info_t *)s)->list, steps); }

/* About: Select on the first row opens Licences (the third-party components and their licences, kept in step with LICENSE.md) */
static bool info_button(ui_screen_t *s, uint8_t btn) {
    info_t *in = (info_t *)s;
    if (btn != HAL_BTN_SELECT || in->kind != INFO_ABOUT) {
        return false;
    }
    if (ui_list_selected(in->list) == 0) {
        ui_frame_push(ui_screen_licences_create());
    }
    return true;
}
static void info_destroy(ui_screen_t *s) { ui_list_destroy(((info_t *)s)->list); }

static ui_screen_t *info_create(const char *title, info_kind_t kind) {
    info_t *in = (info_t *)ui_screen_new(sizeof *in, title);
    if (in == NULL) {
        return NULL;
    }
    in->kind = kind;
    in->base.on_scroll = info_scroll;
    in->base.on_tick = info_tick;
    in->base.on_button = info_button;
    in->base.destroy = info_destroy;
    info_sample(in);
    if (kind == INFO_STORAGE && caps_available(CAP_STORAGE)) {
        g_space_seq = os_tick_ms() | 1u;
        (void)storage_space_request(g_space_seq); /* free space is slow on a big volume: ask the storage task */
    }
    in->last_ms = os_tick_ms();
    in->list = ui_list_create(in->base.root, UI_W, &(ui_list_model_t){.count = info_count(in), .ctx = in, .get = info_get});
    return &in->base;
}

static ui_screen_t *make_about(void) { return info_create("About", INFO_ABOUT); }
static ui_screen_t *make_storage(void) { return info_create("Storage", INFO_STORAGE); }
static ui_screen_t *make_battery(void) { return info_create("Battery", INFO_BATTERY); }
static ui_screen_t *make_bluetooth(void) { return ui_screen_bluetooth_create(); }

/* ---- Display: backlight (a slider modal with live preview, docs/ui-input.md) ---- */

static void backlight_change(void *ctx, const wi_value_t *v) {
    (void)ctx;
    g_ui_state.backlight_pct = (uint8_t)v->number;
    ui_apply_backlight(); /* live; a cancel arrives here as the initial value */
}

static void backlight_done(void *ctx, bool ok, const wi_value_t *v) {
    (void)ctx;
    ui_idle_inhibit(false);
    if (ok) {
        g_ui_state.backlight_pct = (uint8_t)v->number;
        ui_apply_backlight();
        ui_state_persist();
    }
}

static void open_backlight_slider(void) {
    /* never fully dark: you could not find the way back */
    ui_idle_inhibit(true); /* the preview must stay at the live level */
    if (!ui_input_open(&(wi_request_t){.kind = WI_K_SLIDER, .title = "Backlight", .done = backlight_done, .change = backlight_change,
                                       .value = g_ui_state.backlight_pct, .min = 5, .max = 100, .step = 5, .unit = "%"})) {
        ui_idle_inhibit(false);
    }
}

/* Display: a two-level list like Haptics (Backlight opens the slider, the other two rows toggle in place) */

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
} display_t;

enum { D_BACKLIGHT, D_AUTODIM, D_DIM_AFTER, D_COUNT };

static void display_get(void *ctx, int32_t i, ui_list_item_t *o) {
    (void)ctx;
    if (i == D_BACKLIGHT) {
        put(o, "Backlight", "%u %%", (unsigned)g_ui_state.backlight_pct);
        o->chevron = true;
    } else if (i == D_AUTODIM) {
        put(o, "Auto-dim", "%s", g_ui_state.autodim ? "On" : "Off");
    } else {
        put(o, "Dim after", "%s", ui_autodim_name(g_ui_state.dim_idx));
        o->dim = !g_ui_state.autodim;
    }
}

static void display_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((display_t *)s)->list, steps); }

static bool display_button(ui_screen_t *b, uint8_t btn) {
    display_t *s = (display_t *)b;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    switch (ui_list_selected(s->list)) {
    case D_BACKLIGHT:
        open_backlight_slider();
        return true;
    case D_AUTODIM:
        g_ui_state.autodim = !g_ui_state.autodim;
        break;
    default:
        if (!g_ui_state.autodim) {
            return true; /* greyed: nothing to change */
        }
        g_ui_state.dim_idx = (uint8_t)((g_ui_state.dim_idx + 1u) % UI_AUTODIM_STEPS);
        break;
    }
    ui_state_persist();
    ui_list_refresh(s->list);
    return true;
}

static void display_destroy(ui_screen_t *b) { ui_list_destroy(((display_t *)b)->list); }
static void display_suspend(ui_screen_t *b) { ui_list_suspend(((display_t *)b)->list); }
static void display_resume(ui_screen_t *b) {
    ui_list_resume(((display_t *)b)->list);
    ui_list_refresh(((display_t *)b)->list); /* the slider may have changed the backlight value */
}

static ui_screen_t *make_display(void) {
    display_t *s = (display_t *)ui_screen_new(sizeof *s, "Display");
    if (s == NULL) {
        return NULL;
    }
    s->base.on_scroll = display_scroll;
    s->base.on_button = display_button;
    s->base.destroy = display_destroy;
    s->base.suspend = display_suspend;
    s->base.resume = display_resume;
    s->list = ui_list_create(s->base.root, UI_W, &(ui_list_model_t){.count = D_COUNT, .ctx = s, .get = display_get});
    return &s->base;
}

/* ---- Haptics: actuator and calibration (the work is done by the haptic task, never here) ---- */

static const char *cal_text(void) {
    if (!caps_available(CAP_HAPTICS)) {
        return "No driver";
    }
    switch (rt_haptic_cal_state()) {
    case HAPTIC_CAL_NOT_NEEDED: return "Not needed";
    case HAPTIC_CAL_RUNNING: return "Running...";
    case HAPTIC_CAL_OK: return "Done";
    case HAPTIC_CAL_FAILED: return "Failed";
    default: return "Not done";
    }
}

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    uint32_t last_ms;
} haptics_t;

static void haptics_get(void *ctx, int32_t i, ui_list_item_t *o) {
    (void)ctx;
    if (i == 0) {
        put(o, "Actuator", "%s", rt_haptic_actuator() == HAL_HAPTIC_LRA ? "LRA" : "ERM");
    } else {
        put(o, "Calibrate", "%s", cal_text());
    }
    o->dim = !caps_available(CAP_HAPTICS);
}

static void haptics_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((haptics_t *)s)->list, steps); }

static bool haptics_button(ui_screen_t *b, uint8_t btn) {
    haptics_t *s = (haptics_t *)b;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    if (caps_available(CAP_HAPTICS)) {
        if (ui_list_selected(s->list) == 0) {
            /* a changed type is calibrated automatically; the other type's calibration is dropped */
            rt_haptic_set_actuator(rt_haptic_actuator() == HAL_HAPTIC_LRA ? HAL_HAPTIC_ERM : HAL_HAPTIC_LRA);
        } else {
            rt_haptic_request_calibration();
        }
        ui_list_refresh(s->list);
    }
    return true;
}

static void haptics_tick(ui_screen_t *b) {
    haptics_t *s = (haptics_t *)b;
    uint32_t now = os_tick_ms();
    if (now - s->last_ms >= 500u) {
        s->last_ms = now;
        ui_list_refresh(s->list);
    }
}

static void haptics_destroy(ui_screen_t *b) { ui_list_destroy(((haptics_t *)b)->list); }

static ui_screen_t *make_haptics(void) {
    haptics_t *s = (haptics_t *)ui_screen_new(sizeof *s, "Haptics");
    if (s == NULL) {
        return NULL;
    }
    s->base.on_scroll = haptics_scroll;
    s->base.on_button = haptics_button;
    s->base.on_tick = haptics_tick;
    s->base.destroy = haptics_destroy;
    s->last_ms = os_tick_ms();
    s->list = ui_list_create(s->base.root, UI_W, &(ui_list_model_t){.count = 2, .ctx = s, .get = haptics_get});
    return &s->base;
}

/* ---- the Settings screen itself ---------------------------------------------------------------------- */

enum { G_ABOUT, G_DISPLAY, G_CLICK, G_HAPTICS, G_GAPLESS, G_SHUFFLE, G_REPEAT, G_RESUME, G_BLUETOOTH, G_WIFI, G_STORAGE, G_BATTERY, G_COUNT };

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    lv_obj_t *head, *body;
    int32_t shown_sel;
    uint32_t last_ms;
    char text[200];
} settings_t;

static const char *const k_group[G_COUNT] = {"About", "Display", "Click", "Haptics", "Gapless playback", "Shuffle", "Repeat", "Resume playing", "Bluetooth", "Wi-Fi", "Storage", "Battery"};

static const char *shuffle_text(void) { return player_shuffle() == PQ_SHUFFLE_SONGS ? "Songs" : "Off"; }
static const char *repeat_text(void) {
    pq_repeat_t r = player_repeat();
    return r == PQ_REPEAT_ONE ? "One" : r == PQ_REPEAT_ALL ? "All" : "Off";
}

static void settings_get(void *ctx, int32_t i, ui_list_item_t *o) {
    (void)ctx;
    snprintf(o->label, sizeof o->label, "%s", k_group[i]);
    if (i == G_CLICK) {
        snprintf(o->detail, sizeof o->detail, "%s", click_mode_name(g_ui_state.click_mode));
    } else if (i == G_HAPTICS) {
        o->chevron = true;
    } else if (i == G_GAPLESS) {
        snprintf(o->detail, sizeof o->detail, "%s", player_gapless_enabled() ? "On" : "Off");
    } else if (i == G_SHUFFLE) {
        snprintf(o->detail, sizeof o->detail, "%s", shuffle_text());
    } else if (i == G_REPEAT) {
        snprintf(o->detail, sizeof o->detail, "%s", repeat_text());
    } else if (i == G_RESUME) {
        snprintf(o->detail, sizeof o->detail, "%s", player_resume_autoplay() ? "On" : "Off");
    } else if (i == G_DISPLAY) {
        snprintf(o->detail, sizeof o->detail, "%u %%", (unsigned)g_ui_state.backlight_pct);
        o->chevron = true;
    } else {
        o->chevron = true;
    }
}

static void pane_text(int32_t g, char *out, size_t n) {
    char a[24], b[24];
    switch (g) {
    case G_ABOUT: {
        capabilities_t c;
        caps_get(&c);
        int ok = 0;
        for (int i = 0; i < CAP_COUNT; i++) {
            ok += c.state[i] == CAP_OK;
        }
        snprintf(out, n, "%s\n%s\n%s\n%d of %d checks ok", NEUPOD_VERSION, NEUPOD_GIT_HASH, hal_platform_name(), ok, (int)CAP_COUNT);
        break;
    }
    case G_DISPLAY:
        if (g_ui_state.autodim) {
            snprintf(out, n, "Backlight\n%u %%\n%s %u %% after %s", (unsigned)g_ui_state.backlight_pct,
                     g_ui_state.backlight_pct > UI_DIM_PCT ? "Dims to" : "Stays at", (unsigned)ui_backlight_effective(g_ui_state.backlight_pct, true),
                     ui_autodim_name(g_ui_state.dim_idx));
        } else {
            snprintf(out, n, "Backlight\n%u %%\nAuto-dim off", (unsigned)g_ui_state.backlight_pct);
        }
        break;
    case G_CLICK:
        snprintf(out, n, "Click\n%s\n%s", click_mode_name(g_ui_state.click_mode),
                 click_mode_uses_haptic(g_ui_state.click_mode) && !caps_available(CAP_HAPTICS) ? "No haptic driver"
                 : click_mode_uses_piezo(g_ui_state.click_mode) ? "Piezo not fitted yet"
                                                                 : "Select to change");
        break;
    case G_HAPTICS:
        snprintf(out, n, "Haptics\n%s\nCalibration: %s", rt_haptic_actuator() == HAL_HAPTIC_LRA ? "LRA" : "ERM", cal_text());
        break;
    case G_GAPLESS:
        snprintf(out, n, "Gapless\n%s\n%s", player_gapless_enabled() ? "On" : "Off",
                 player_gapless_enabled() ? "Tracks of one album play without a break" : "Short pause between tracks");
        break;
    case G_SHUFFLE:
        snprintf(out, n, "Shuffle\n%s\n%s", shuffle_text(), player_shuffle() == PQ_SHUFFLE_SONGS ? "Folder plays in random order" : "Folder plays in order");
        break;
    case G_REPEAT:
        snprintf(out, n, "Repeat\n%s\n%s", repeat_text(),
                 player_repeat() == PQ_REPEAT_ONE ? "Current track loops" : player_repeat() == PQ_REPEAT_ALL ? "Folder starts over at the end" : "Stops at the end of the folder");
        break;
    case G_RESUME:
        snprintf(out, n, "At power-up\n%s\n%s", player_resume_autoplay() ? "On" : "Off",
                 player_resume_autoplay() ? "Plays on power-up if it was playing" : "Power-up leaves the track paused");
        break;
    case G_BLUETOOTH: {
        hal_bt_state_t bs = hal_bt_state();
        uint8_t addr[6];
        if (bs == HAL_BT_ON || bs == HAL_BT_SCANNING) {
            if (hal_bt_local_address(addr)) {
                snprintf(a, sizeof a, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
            } else {
                snprintf(a, sizeof a, "-");
            }
            char hs[80], cs[96];
            ui_bt_headset_text(hs, sizeof hs);
            ui_bt_stream_pane_text(cs, sizeof cs);
            if (cs[0] != '\0') { /* with a stream open the codec lines (and the absolute-volume line) need the room: no address, no hint, no "On" */
                snprintf(out, n, "%s%s\n%s", bs == HAL_BT_SCANNING ? "Scanning...\n" : "", hs, cs); /* "On" is not said: the headset line says more */
            } else {
                snprintf(out, n, "%s\n%s\n%s%sSelect to %s", bs == HAL_BT_SCANNING ? "Scanning..." : "On", a, hs, hs[0] != '\0' ? "\n" : "",
                         hs[0] != '\0' ? "manage" : "scan");
            }
        } else if (bs == HAL_BT_STARTING) {
            snprintf(out, n, "Starting...");
        } else {
            capabilities_t c;
            caps_get(&c);
            snprintf(out, n, "Unavailable\n%s", c.brief[CAP_BLUETOOTH]);
        }
        break;
    }
    case G_WIFI: {
        wifi_info_t wi;
        wifi_get_info(&wi);
        if (wi.state == HAL_WIFI_UP) {
            snprintf(out, n, "%s\n%s\nSignal: %s", wi.ssid, wi.ip, ui_signal_word(ui_wifi_bars()));
        } else if (wi.saved == 0) {
            snprintf(out, n, "No network saved\nSelect to add one");
        } else {
            snprintf(out, n, "%s", wi.state == HAL_WIFI_DOWN ? "Radio idle" : wi.state == HAL_WIFI_JOINING ? "Connecting..." : "Not connected");
        }
        break;
    }
    case G_STORAGE: {
        hal_card_info_t ci;
        if (hal_storage_ready() && hal_storage_get_info(&ci) == HAL_STORAGE_OK) {
            ui_format_size(ci.capacity_bytes, a, sizeof a);
            snprintf(out, n, "%s\n%s\n%s", hal_storage_card_type_str(ci.type), a, hal_storage_get_write_enabled() ? "read-write" : "read-only");
        } else {
            snprintf(out, n, "No card");
        }
        break;
    }
    case G_BATTERY: {
        hal_battery_status_t bs;
        if (battery_store_get(&bs)) {
            snprintf(a, sizeof a, "%u %%", (unsigned)bs.soc_pct);
            snprintf(b, sizeof b, "%u mV", (unsigned)bs.mv);
            snprintf(out, n, "%s\n%s\n%s", a, b, bs.rate_mpct_per_h > 2000 ? "charging" : bs.rate_mpct_per_h < -2000 ? "discharging" : "idle");
        } else {
            snprintf(out, n, "No gauge");
        }
        break;
    }
    }
}

static void settings_update_pane(settings_t *s) {
    int32_t g = ui_list_selected(s->list);
    char text[sizeof s->text];
    pane_text(g, text, sizeof text);
    if (strcmp(text, s->text) != 0 || g != s->shown_sel) {
        snprintf(s->text, sizeof s->text, "%s", text);
        s->shown_sel = g;
        lv_label_set_text(s->head, k_group[g]);
        ui_label_set_text(s->body, s->text);
    }
}

static void settings_scroll(ui_screen_t *b, int32_t steps) {
    settings_t *s = (settings_t *)b;
    ui_list_move(s->list, steps);
    settings_update_pane(s);
}

static bool settings_button(ui_screen_t *b, uint8_t btn) {
    settings_t *s = (settings_t *)b;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    ui_screen_t *next = NULL;
    switch (ui_list_selected(s->list)) {
    case G_ABOUT: next = make_about(); break;
    case G_DISPLAY: next = make_display(); break;
    case G_CLICK:
        g_ui_state.click_mode = click_mode_next(g_ui_state.click_mode);
        ui_state_persist();
        ui_list_refresh(s->list);
        settings_update_pane(s);
        ui_feedback_step(); /* a click when switched on is the confirmation */
        return true;
    case G_HAPTICS: next = make_haptics(); break;
    case G_GAPLESS:
        player_set_gapless(!player_gapless_enabled()); /* saved with the other settings; read when the next track is prepared */
        ui_list_refresh(s->list);
        settings_update_pane(s);
        return true;
    case G_SHUFFLE:
        player_set_shuffle(player_shuffle() == PQ_SHUFFLE_SONGS ? PQ_SHUFFLE_OFF : PQ_SHUFFLE_SONGS);
        ui_list_refresh(s->list);
        settings_update_pane(s);
        return true;
    case G_REPEAT:
        player_set_repeat((pq_repeat_t)(((unsigned)player_repeat() + 1u) % 3u)); /* Off -> One -> All -> Off */
        ui_list_refresh(s->list);
        settings_update_pane(s);
        return true;
    case G_RESUME:
        player_set_resume_autoplay(!player_resume_autoplay());
        ui_list_refresh(s->list);
        settings_update_pane(s);
        return true;
    case G_BLUETOOTH: next = make_bluetooth(); break;
    case G_WIFI: next = ui_screen_wifi_create(); break;
    case G_STORAGE: next = make_storage(); break;
    case G_BATTERY: next = make_battery(); break;
    }
    ui_frame_push(next);
    return true;
}

static void settings_tick(ui_screen_t *b) {
    settings_t *s = (settings_t *)b;
    uint32_t now = os_tick_ms();
    if (now - s->last_ms >= 1000u) {
        s->last_ms = now;
        settings_update_pane(s);
        ui_list_refresh(s->list); /* values on the rows (backlight) may have changed in a detail screen */
    }
}

static void settings_destroy(ui_screen_t *b) { ui_list_destroy(((settings_t *)b)->list); }

static void list_suspend(ui_screen_t *x) { ui_list_suspend(((settings_t *)x)->list); }
static void list_resume(ui_screen_t *x) {
    settings_t *s = (settings_t *)x;
    ui_list_resume(s->list);
    ui_list_refresh(s->list); /* a detail screen may have changed a value */
    settings_update_pane(s);
}

ui_screen_t *ui_screen_settings_create(void) {
    settings_t *s = (settings_t *)ui_screen_new(sizeof *s, "Settings");
    if (s == NULL) {
        return NULL;
    }
    s->base.on_scroll = settings_scroll;
    s->base.on_button = settings_button;
    s->base.on_tick = settings_tick;
    s->base.destroy = settings_destroy;
    s->base.suspend = list_suspend;
    s->base.resume = list_resume;
    s->shown_sel = -1;
    s->list = ui_list_create(s->base.root, UIM->set_list_w, &(ui_list_model_t){.count = G_COUNT, .ctx = s, .get = settings_get});
    const int32_t pane_w = UI_W - UIM->set_list_w;
    const int32_t text_w = pane_w - 2 * UIM->set_pane_pad;

    lv_obj_t *pane = lv_obj_create(s->base.root);
    lv_obj_remove_style_all(pane);
    lv_obj_set_size(pane, pane_w, UI_CONTENT_H);
    lv_obj_set_pos(pane, UIM->set_list_w, 0);
    lv_obj_set_scrollable(pane, false);
    lv_obj_set_style_bg_color(pane, lv_color_hex(UI_L_PANE), 0);
    lv_obj_set_style_bg_opa(pane, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pane, lv_color_hex(0xD9D9D4), 0);
    lv_obj_set_style_border_width(pane, 1, 0);
    lv_obj_set_style_border_side(pane, LV_BORDER_SIDE_LEFT, 0);
    s->head = ui_label(pane, "", UI_L_TEXT);
    lv_obj_set_width(s->head, text_w);
    lv_obj_set_style_text_align(s->head, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s->head, UIM->set_pane_pad, UIM->set_head_y);
    s->body = ui_label(pane, "", UI_L_DIM);
    lv_obj_set_width(s->body, text_w);
    lv_obj_set_style_text_align(s->body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s->body, UIM->set_body_line_space, 0);
    lv_obj_set_pos(s->body, UIM->set_pane_pad, UIM->set_body_y);
    settings_update_pane(s);
    return &s->base;
}

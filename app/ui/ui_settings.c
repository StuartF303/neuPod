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
#include "audio_route.h"
#include "hal_audio.h"
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

typedef enum { INFO_ABOUT, INFO_DIAG, INFO_STORAGE, INFO_BATTERY } info_kind_t;

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

#define ABOUT_ROWS 6 /* Licences, Diagnostics, Version, Build, Platform, Uptime */
#define DIAG_FIXED 11 /* Heap free, Core 1 stack free, audio stats, Bluetooth link stats; the boot checks follow */

static int32_t info_count(const info_t *in) {
    switch (in->kind) {
    case INFO_ABOUT:
        return ABOUT_ROWS; /* rows 0 and 1 open Licences and Diagnostics */
    case INFO_DIAG:
        return CAP_COUNT + DIAG_FIXED;
    case INFO_STORAGE:
        return 5;
    case INFO_BATTERY:
        return 6;
    }
    return 0;
}

static void about_get(const info_t *in, int32_t i, ui_list_item_t *o) {
    (void)in;
    char b[24];
    switch (i) {
    case 0: snprintf(o->label, sizeof o->label, "Licences"); o->chevron = true; break;
    case 1: snprintf(o->label, sizeof o->label, "Diagnostics"); o->chevron = true; break;
    case 2: put(o, "Version", "%s", NEUPOD_VERSION); break;
    case 3: put(o, "Build", "%s", NEUPOD_GIT_HASH); break;
    case 4: put(o, "Platform", "%s", hal_platform_name()); break;
    default: fmt_uptime(b, sizeof b); put(o, "Uptime", "%s", b); break;
    }
}

/* Diagnostics: the boot checks, then memory and audio counters, then the Bluetooth link (what moved here from the Settings pane) */
static void diag_get(const info_t *in, int32_t i, ui_list_item_t *o) {
    char b[24];
    if (i < CAP_COUNT) {
        cap_id_t c = (cap_id_t)i;
        if (in->caps.state[c] == CAP_OK) {
            put(o, cap_name(c), "ok");
        } else if (in->caps.state[c] == CAP_PENDING) {
            put(o, cap_name(c), "checking");
        } else {
            put(o, cap_name(c), "%s", in->caps.brief[c]);
            o->dim = true;
        }
        return;
    }
    hal_bt_codec_t codec;
    hal_bt_stats_t bs;
    hal_bt_stats(&bs);
    hal_bt_stream_state_t ss = hal_bt_stream_state();
    const bool have_codec = (ss == HAL_BT_STREAM_OPEN || ss == HAL_BT_STREAM_STREAMING || ss == HAL_BT_STREAM_SUSPENDED) && hal_bt_stream_codec(&codec);
    switch (i - CAP_COUNT) {
    case 0: ui_format_size(os_heap_free(), b, sizeof b); put(o, "Heap free", "%s", b); break;
    case 1: put(o, "Core 1 stack free", "%u B", (unsigned)hal_core1_stack_free_min()); break;
    case 2: put(o, "Audio blocks", in->audio_ok ? "%u" : "-", (unsigned)in->audio.blocks); break;
    case 3: put(o, "Audio underruns", in->audio_ok ? "%u" : "-", (unsigned)in->audio.underruns); break;
    case 4: put(o, "Jitter max", in->audio_ok ? "%u us" : "-", (unsigned)in->audio.jitter_max_us); break;
    case 5: put(o, "BT codec", "%s", have_codec ? (codec.codec == HAL_BT_CODEC_SBC_XQ ? "SBC-XQ" : "SBC") : "-"); break;
    case 6: put(o, "BT bitpool", have_codec ? "%u" : "-", (unsigned)codec.bitpool); break;
    case 7: put(o, "BT bit rate", have_codec ? "%u kbit/s" : "-", (unsigned)codec.kbps); break;
    case 8: put(o, "BT underruns", have_codec ? "%u" : "-", (unsigned)bs.underruns); break;
    case 9: put(o, "BT frames", have_codec ? "%u" : "-", (unsigned)bs.frames_encoded); break;
    default: put(o, "BT drift trims", have_codec ? "%u" : "-", (unsigned)bs.drift_corrections); break;
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
    case INFO_DIAG: diag_get(in, i, o); break;
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

/* About: Select on the first row opens Licences (the third-party components and their licences, kept in step with LICENSE.md), on the second Diagnostics */
static ui_screen_t *info_create(const char *title, info_kind_t kind);
static bool info_button(ui_screen_t *s, uint8_t btn) {
    info_t *in = (info_t *)s;
    if (btn != HAL_BTN_SELECT || in->kind != INFO_ABOUT) {
        return false;
    }
    if (ui_list_selected(in->list) == 0) {
        ui_frame_push(ui_screen_licences_create());
    } else if (ui_list_selected(in->list) == 1) {
        ui_frame_push(info_create("Diagnostics", INFO_DIAG));
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
    if (!ui_input_open(&(wi_request_t){.kind = WI_K_SLIDER, .title = "Brightness", .done = backlight_done, .change = backlight_change,
                                       .value = g_ui_state.backlight_pct, .min = 5, .max = 100, .step = 5, .unit = "%"})) {
        ui_idle_inhibit(false);
    }
}

/* Display (ST-2, design review 3.5): Theme, Brightness (opens the slider), Dim after (10 s ... 5 min, then Off: that is Auto-dim), Off after, Auto-lock.
 * The other rows change in place with a tick. */

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
} display_t;

enum { D_THEME, D_BACKLIGHT, D_DIM_AFTER, D_SCREEN_OFF, D_AUTOLOCK, D_COUNT };

static void display_get(void *ctx, int32_t i, ui_list_item_t *o) {
    (void)ctx;
    if (i == D_THEME) {
        put(o, "Theme", "%s", ui_theme_mode_name(g_ui_state.theme)); /* Light, Dark, or Auto (dark once the backlight dims) */
    } else if (i == D_BACKLIGHT) {
        put(o, "Brightness", "%u %%", (unsigned)g_ui_state.backlight_pct);
        o->chevron = true;
    } else if (i == D_DIM_AFTER) {
        put(o, "Dim after", "%s", g_ui_state.autodim ? ui_autodim_name(g_ui_state.dim_idx) : "Off");
    } else if (i == D_SCREEN_OFF) {
        put(o, "Off after", "%s", ui_screenoff_name(g_ui_state.off_idx)); /* counted from the dim (from the last press when Dim after is Off) */
    } else {
        put(o, "Auto-lock", "%s", g_ui_state.autolock ? "On" : "Off"); /* Hold turns on when the screen goes off */
    }
}

static void display_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((display_t *)s)->list, steps); }

static bool display_button(ui_screen_t *b, uint8_t btn) {
    display_t *s = (display_t *)b;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    switch (ui_list_selected(s->list)) {
    case D_THEME:
        g_ui_state.theme = ui_theme_mode_next(g_ui_state.theme);
        ui_apply_theme(); /* Light and Dark switch at once; Auto follows the backlight */
        break;
    case D_BACKLIGHT:
        open_backlight_slider();
        return true;
    case D_DIM_AFTER: /* Off -> 10 s -> ... -> 5 min -> Off */
        if (!g_ui_state.autodim) {
            g_ui_state.autodim = true;
            g_ui_state.dim_idx = 0;
        } else if (g_ui_state.dim_idx + 1u < UI_AUTODIM_STEPS) {
            g_ui_state.dim_idx++;
        } else {
            g_ui_state.autodim = false;
        }
        break;
    case D_SCREEN_OFF:
        g_ui_state.off_idx = (uint8_t)((g_ui_state.off_idx + 1u) % UI_SCREENOFF_STEPS);
        break;
    default:
        g_ui_state.autolock = !g_ui_state.autolock;
        break;
    }
    ui_state_persist();
    ui_list_refresh(s->list);
    ui_feedback_step(); /* the tick says it changed */
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

enum { G_ABOUT, G_DISPLAY, G_CLICK, G_HAPTICS, G_GAPLESS, G_SHUFFLE, G_REPEAT, G_RESUME, G_SPEAKER, G_LINEOUT, G_BLUETOOTH, G_WIFI, G_STORAGE, G_BATTERY, G_COUNT };

#define PANE_ROWS 4
typedef struct {
    char kv[PANE_ROWS][2][28]; /* key, value */
    char help[64];
    int8_t n;
} pane_t;

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    lv_obj_t *head, *help;
    lv_obj_t *key[PANE_ROWS], *val[PANE_ROWS];
    int32_t shown_sel;
    uint32_t last_ms;
    pane_t shown; /* what the pane shows now: a change is written once */
    bool flip;    /* the next pane update flips the first value (a toggle changed it in place) */
} settings_t;

static const char *const k_group[G_COUNT] = {"About", "Display", "Click", "Haptics", "Gapless playback", "Shuffle", "Repeat", "Resume playing", "Speaker", "Line out", "Bluetooth", "Wi-Fi", "Storage", "Battery"};

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
    } else if (i == G_SPEAKER) {
        snprintf(o->detail, sizeof o->detail, "%s", !hal_audio_speaker_available() ? "None" : audio_route_speaker_enabled() ? "On" : "Off");
        o->dim = !hal_audio_speaker_available();
    } else if (i == G_LINEOUT) {
        snprintf(o->detail, sizeof o->detail, "%s", !hal_audio_line_out_available() ? "None" : audio_route_line_out_enabled() ? "On" : "Off");
        o->dim = !hal_audio_line_out_available() || (!audio_route_line_out_enabled() && !hal_audio_headphones_in()); /* nothing in the jack: it cannot be turned on */
    } else if (i == G_DISPLAY) {
        snprintf(o->detail, sizeof o->detail, "%u %%", (unsigned)g_ui_state.backlight_pct);
        o->chevron = true;
    } else {
        o->chevron = true;
    }
}

/* The right pane (ST-1): the group's name, up to four key / value lines (Small, keys ink2 at the left, values ink at the right) and a line of help (Small, ink3). */
static void kv(pane_t *p, const char *key, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void kv(pane_t *p, const char *key, const char *fmt, ...) {
    if (p->n >= PANE_ROWS) {
        return;
    }
    snprintf(p->kv[p->n][0], sizeof p->kv[0][0], "%s", key);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->kv[p->n][1], sizeof p->kv[0][1], fmt, ap);
    va_end(ap);
    p->n++;
}

static void pane_fill(int32_t g, pane_t *p) {
    char a[24];
    memset(p, 0, sizeof *p);
    switch (g) {
    case G_ABOUT: {
        capabilities_t c;
        caps_get(&c);
        int ok = 0;
        for (int i = 0; i < CAP_COUNT; i++) {
            ok += c.state[i] == CAP_OK;
        }
        kv(p, "Version", "%s", NEUPOD_VERSION);
        kv(p, "Build", "%s", NEUPOD_GIT_HASH);
        kv(p, "Platform", "%s", hal_platform_name());
        kv(p, "Checks", "%d of %d ok", ok, (int)CAP_COUNT);
        break;
    }
    case G_DISPLAY:
        kv(p, "Theme", "%s", ui_theme_mode_name(g_ui_state.theme));
        kv(p, "Brightness", "%u %%", (unsigned)g_ui_state.backlight_pct);
        kv(p, "Dim after", "%s", g_ui_state.autodim ? ui_autodim_name(g_ui_state.dim_idx) : "Off");
        kv(p, "Off after", "%s", ui_screenoff_name(g_ui_state.off_idx));
        if (g_ui_state.theme == UI_THEME_AUTO) {
            snprintf(p->help, sizeof p->help, "Dark when dimmed");
        }
        break;
    case G_CLICK:
        kv(p, "Click", "%s", click_mode_name(g_ui_state.click_mode));
        snprintf(p->help, sizeof p->help, "%s",
                 click_mode_uses_haptic(g_ui_state.click_mode) && !caps_available(CAP_HAPTICS) ? "No haptic driver"
                 : click_mode_uses_piezo(g_ui_state.click_mode) ? "Piezo not fitted yet"
                                                                 : "Select to change");
        break;
    case G_HAPTICS:
        kv(p, "Actuator", "%s", rt_haptic_actuator() == HAL_HAPTIC_LRA ? "LRA" : "ERM");
        kv(p, "Calibration", "%s", cal_text());
        break;
    case G_GAPLESS:
        kv(p, "Gapless", "%s", player_gapless_enabled() ? "On" : "Off");
        snprintf(p->help, sizeof p->help, "%s", player_gapless_enabled() ? "Tracks of one album play without a break" : "Short pause between tracks");
        break;
    case G_SHUFFLE:
        kv(p, "Shuffle", "%s", shuffle_text());
        snprintf(p->help, sizeof p->help, "%s", player_shuffle() == PQ_SHUFFLE_SONGS ? "Folder plays in random order" : "Folder plays in order");
        break;
    case G_REPEAT:
        kv(p, "Repeat", "%s", repeat_text());
        snprintf(p->help, sizeof p->help, "%s",
                 player_repeat() == PQ_REPEAT_ONE ? "Current track loops" : player_repeat() == PQ_REPEAT_ALL ? "Folder starts over at the end" : "Stops at the end of the folder");
        break;
    case G_RESUME:
        kv(p, "At power-up", "%s", player_resume_autoplay() ? "On" : "Off");
        snprintf(p->help, sizeof p->help, "%s", player_resume_autoplay() ? "Plays on power-up if it was playing" : "Power-up leaves the track paused");
        break;
    case G_SPEAKER:
        if (!hal_audio_speaker_available()) {
            kv(p, "Speaker", "Not fitted");
        } else {
            kv(p, "Speaker", "%s", audio_route_speaker_enabled() ? "On" : "Off");
            if (audio_route_pref() == AUDIO_ROUTE_SPEAKER) {
                kv(p, "Output", "%s", audio_route_speaker_enabled() ? "Speaker" : "Speaker (off)");
            }
            snprintf(p->help, sizeof p->help, "%s", audio_route_speaker_enabled() ? "Used when no headphones or headset" : "Never used, even as the output");
        }
        break;
    case G_LINEOUT:
        if (!hal_audio_line_out_available()) {
            kv(p, "Line out", "Not fitted");
        } else {
            kv(p, "Line out", "%s", audio_route_line_out_enabled() ? "On" : "Off");
            snprintf(p->help, sizeof p->help, "%s",
                     audio_route_line_out_enabled() ? "Full level, volume fixed" : hal_audio_headphones_in() ? "For an amplifier: full level" : "Plug into the jack first");
        }
        break;
    case G_BLUETOOTH: {
        hal_bt_state_t bs = hal_bt_state();
        if (bs == HAL_BT_ON || bs == HAL_BT_SCANNING) {
            char hs[80];
            ui_bt_headset_text(hs, sizeof hs); /* "Name\nState", or "" when no headset is remembered */
            char *nl = strchr(hs, '\n');
            if (nl != NULL) {
                *nl = '\0';
                kv(p, "Headset", "%s", hs);
                kv(p, "State", "%s", nl + 1);
            } else {
                kv(p, "Headset", "None saved");
            }
            snprintf(p->help, sizeof p->help, "Select to %s", hs[0] != '\0' ? "manage" : "pair one");
        } else if (bs == HAL_BT_STARTING) {
            kv(p, "State", "Starting...");
        } else {
            capabilities_t c;
            caps_get(&c);
            kv(p, "State", "Unavailable");
            snprintf(p->help, sizeof p->help, "%s", c.brief[CAP_BLUETOOTH]);
        }
        break;
    }
    case G_WIFI: {
        wifi_info_t wi;
        wifi_get_info(&wi);
        if (wi.state == HAL_WIFI_UP) {
            kv(p, "Network", "%s", wi.ssid);
            kv(p, "Address", "%s", wi.ip);
            kv(p, "Signal", "%s", ui_signal_word(ui_wifi_bars()));
        } else if (wi.saved == 0) {
            kv(p, "Network", "None saved");
            snprintf(p->help, sizeof p->help, "Select to add one");
        } else {
            kv(p, "State", "%s", wi.state == HAL_WIFI_DOWN ? "Radio idle" : wi.state == HAL_WIFI_JOINING ? "Connecting..." : "Not connected");
        }
        break;
    }
    case G_STORAGE: {
        hal_card_info_t ci;
        if (hal_storage_ready() && hal_storage_get_info(&ci) == HAL_STORAGE_OK) {
            ui_format_size(ci.capacity_bytes, a, sizeof a);
            kv(p, "Card", "%s", hal_storage_card_type_str(ci.type));
            kv(p, "Size", "%s", a);
            kv(p, "Mode", "%s", hal_storage_get_write_enabled() ? "read-write" : "read-only");
        } else {
            kv(p, "Card", "None");
        }
        break;
    }
    case G_BATTERY: {
        hal_battery_status_t bs;
        if (battery_store_get(&bs)) {
            kv(p, "Charge", "%u %%", (unsigned)bs.soc_pct);
            kv(p, "Voltage", "%u mV", (unsigned)bs.mv);
            kv(p, "State", "%s", bs.rate_mpct_per_h > 2000 ? "charging" : bs.rate_mpct_per_h < -2000 ? "discharging" : "idle");
        } else {
            kv(p, "Gauge", "None");
        }
        break;
    }
    }
}

static void value_flip(lv_obj_t *l);
static void settings_update_pane(settings_t *s) {
    int32_t g = ui_list_selected(s->list);
    pane_t p;
    pane_fill(g, &p);
    if (memcmp(&p, &s->shown, sizeof p) == 0 && g == s->shown_sel) {
        s->flip = false;
        return;
    }
    const bool same = g == s->shown_sel;
    s->shown = p;
    s->shown_sel = g;
    lv_label_set_text(s->head, k_group[g]);
    const int32_t pitch = lv_font_get_line_height(UIM->font_small) + UIM->set_body_line_space * 2 / 3;
    for (int i = 0; i < PANE_ROWS; i++) {
        const bool on = i < p.n;
        lv_obj_set_hidden(s->key[i], !on);
        lv_obj_set_hidden(s->val[i], !on);
        if (on) {
            ui_label_set_text(s->key[i], p.kv[i][0]);
            ui_label_set_text(s->val[i], p.kv[i][1]);
        }
    }
    const bool help = p.help[0] != '\0';
    lv_obj_set_hidden(s->help, !help);
    if (help) {
        ui_label_set_text(s->help, p.help);
        lv_obj_set_pos(s->help, UIM->set_pane_pad, UIM->set_body_y + p.n * pitch + UIM->set_body_line_space);
    }
    if (same && s->flip && p.n > 0) { /* a value changed in place flips; moving to another group does not */
        value_flip(s->val[0]);
    }
    s->flip = false;
}

/* ST-1: a toggle changed in place: its value in the pane flips in 60 ms (the haptic tick is the click feedback) */
static void flip_cb(void *o, int32_t v) { lv_obj_set_style_text_opa(o, (lv_opa_t)v, 0); }

static void value_flip(lv_obj_t *l) {
    lv_anim_delete(l, flip_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_exec_cb(&a, flip_cb);
    lv_anim_set_values(&a, 60, 255);
    lv_anim_set_duration(&a, 60);
    lv_anim_start(&a);
}

/* A toggle row changed: refresh the list and the pane, flip the value, tick. */
static void toggled(settings_t *s) {
    ui_list_refresh(s->list);
    s->flip = true;
    settings_update_pane(s);
    ui_feedback_step();
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
        toggled(s); /* the tick is the click mode's own confirmation */
        return true;
    case G_HAPTICS: next = make_haptics(); break;
    case G_GAPLESS:
        player_set_gapless(!player_gapless_enabled()); /* saved with the other settings; read when the next track is prepared */
        toggled(s);
        return true;
    case G_SHUFFLE:
        player_set_shuffle(player_shuffle() == PQ_SHUFFLE_SONGS ? PQ_SHUFFLE_OFF : PQ_SHUFFLE_SONGS);
        toggled(s);
        return true;
    case G_REPEAT:
        player_set_repeat((pq_repeat_t)(((unsigned)player_repeat() + 1u) % 3u)); /* Off -> One -> All -> Off */
        toggled(s);
        return true;
    case G_RESUME:
        player_set_resume_autoplay(!player_resume_autoplay());
        toggled(s);
        return true;
    case G_SPEAKER:
        if (!hal_audio_speaker_available()) {
            ui_toast_show("No speaker in this device", UI_TOAST_WARN, 0);
            return true;
        }
        audio_route_set_speaker_enabled(!audio_route_speaker_enabled());
        toggled(s);
        return true;
    case G_LINEOUT:
        if (!hal_audio_line_out_available()) {
            ui_toast_show("No line out in this device", UI_TOAST_WARN, 0);
            return true;
        }
        if (audio_route_line_out_enabled()) {
            (void)audio_route_set_line_out_enabled(false);
        } else if (!audio_route_set_line_out_enabled(true)) {
            ui_toast_show("Plug into the jack first", UI_TOAST_WARN, 0);
            return true;
        }
        toggled(s);
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
    const int32_t key_w = text_w * 54 / 100;

    lv_obj_t *pane = lv_obj_create(s->base.root);
    lv_obj_remove_style_all(pane);
    lv_obj_set_size(pane, pane_w, UI_CONTENT_H);
    lv_obj_set_pos(pane, UIM->set_list_w, 0);
    lv_obj_set_scrollable(pane, false);
    ui_obj_bg(pane, UI_C_SURFACE, 0);
    ui_obj_border(pane, UI_C_LINE, 1, LV_BORDER_SIDE_LEFT);
    s->head = ui_label(pane, "", UI_L_TEXT);
    lv_obj_set_width(s->head, text_w);
    lv_label_set_long_mode(s->head, LV_LABEL_LONG_DOT);
    lv_obj_set_height(s->head, UIM->line_h);
    lv_obj_set_pos(s->head, UIM->set_pane_pad, UIM->set_head_y);
    const int32_t pitch = lv_font_get_line_height(UIM->font_small) + UIM->set_body_line_space * 2 / 3;
    for (int i = 0; i < PANE_ROWS; i++) {
        s->key[i] = ui_label_small(pane, "", UI_L_DIM);
        lv_obj_set_size(s->key[i], key_w, lv_font_get_line_height(UIM->font_small));
        lv_label_set_long_mode(s->key[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(s->key[i], UIM->set_pane_pad, UIM->set_body_y + i * pitch);
        s->val[i] = ui_label_small(pane, "", UI_L_TEXT);
        lv_obj_set_size(s->val[i], text_w - key_w, lv_font_get_line_height(UIM->font_small));
        lv_label_set_long_mode(s->val[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s->val[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(s->val[i], UIM->set_pane_pad + key_w, UIM->set_body_y + i * pitch);
        lv_obj_set_hidden(s->key[i], true);
        lv_obj_set_hidden(s->val[i], true);
    }
    s->help = ui_label_small(pane, "", UI_L_FAINT);
    lv_obj_set_width(s->help, text_w);
    lv_label_set_long_mode(s->help, LV_LABEL_LONG_WRAP);
    lv_obj_set_hidden(s->help, true);
    settings_update_pane(s);
    return &s->base;
}

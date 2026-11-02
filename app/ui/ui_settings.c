// SPDX-License-Identifier: MPL-2.0
/*
 * Settings: groups on the left, a live summary pane on the right (docs/ui.md mock-up). Select opens a
 * detail screen (About, Storage, Battery: live read-only lists; Display: backlight; Bluetooth: status and device discovery)
 * or, for Click feedback, toggles On/Off. Backlight and click feedback are stored in the settings store (settings_store.h).
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
#include "neupod_version.h"
#include "nfs.h"
#include "os.h"
#include "storage_service.h"
#include "ui_input.h"
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

#define ABOUT_FIXED 9

static int32_t info_count(const info_t *in) {
    switch (in->kind) {
    case INFO_ABOUT:
        return ABOUT_FIXED + CAP_COUNT;
    case INFO_STORAGE:
        return 5;
    case INFO_BATTERY:
        return 6;
    }
    return 0;
}

static void about_get(const info_t *in, int32_t i, ui_list_item_t *o) {
    char b[24];
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
static void info_destroy(ui_screen_t *s) { ui_list_destroy(((info_t *)s)->list); }

static ui_screen_t *info_create(const char *title, info_kind_t kind) {
    info_t *in = (info_t *)ui_screen_new(sizeof *in, title);
    if (in == NULL) {
        return NULL;
    }
    in->kind = kind;
    in->base.on_scroll = info_scroll;
    in->base.on_tick = info_tick;
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
    if (ok) {
        g_ui_state.backlight_pct = (uint8_t)v->number;
        ui_apply_backlight();
        ui_state_persist();
    }
}

static ui_screen_t *make_display(void) {
    /* never fully dark: you could not find the way back */
    ui_input_open(&(wi_request_t){.kind = WI_K_SLIDER, .title = "Backlight", .done = backlight_done, .change = backlight_change,
                                  .value = g_ui_state.backlight_pct, .min = 5, .max = 100, .step = 5, .unit = "%"});
    return NULL; /* the modal pushed itself */
}

/* ---- the Settings screen itself ---------------------------------------------------------------------- */

enum { G_ABOUT, G_DISPLAY, G_CLICK, G_BLUETOOTH, G_STORAGE, G_BATTERY, G_COUNT };

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    lv_obj_t *head, *body;
    int32_t shown_sel;
    uint32_t last_ms;
    char text[200];
} settings_t;

static const char *const k_group[G_COUNT] = {"About", "Display", "Click feedback", "Bluetooth", "Storage", "Battery"};

static void settings_get(void *ctx, int32_t i, ui_list_item_t *o) {
    (void)ctx;
    snprintf(o->label, sizeof o->label, "%s", k_group[i]);
    if (i == G_CLICK) {
        snprintf(o->detail, sizeof o->detail, "%s", g_ui_state.click_feedback ? "On" : "Off");
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
        snprintf(out, n, "Backlight\n%u %%", (unsigned)g_ui_state.backlight_pct);
        break;
    case G_CLICK:
        snprintf(out, n, "Haptic click\n%s\n%s", g_ui_state.click_feedback ? "On" : "Off",
                 caps_available(CAP_HAPTICS) ? "Motor ready" : "No haptic driver");
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
            snprintf(out, n, "%s\n%s\nSelect to scan", bs == HAL_BT_SCANNING ? "Scanning..." : "On", a);
        } else if (bs == HAL_BT_STARTING) {
            snprintf(out, n, "Starting...");
        } else {
            capabilities_t c;
            caps_get(&c);
            snprintf(out, n, "Unavailable\n%s", c.brief[CAP_BLUETOOTH]);
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
        lv_label_set_text(s->body, s->text);
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
        g_ui_state.click_feedback = !g_ui_state.click_feedback;
        ui_state_persist();
        ui_list_refresh(s->list);
        settings_update_pane(s);
        ui_feedback_step(); /* a click when switched on is the confirmation */
        return true;
    case G_BLUETOOTH: next = make_bluetooth(); break;
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
static void list_resume(ui_screen_t *x) { ui_list_resume(((settings_t *)x)->list); }

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
    s->list = ui_list_create(s->base.root, 170, &(ui_list_model_t){.count = G_COUNT, .ctx = s, .get = settings_get});

    lv_obj_t *pane = lv_obj_create(s->base.root);
    lv_obj_remove_style_all(pane);
    lv_obj_set_size(pane, 150, UI_CONTENT_H);
    lv_obj_set_pos(pane, 170, 0);
    lv_obj_set_scrollable(pane, false);
    lv_obj_set_style_bg_color(pane, lv_color_hex(UI_L_PANE), 0);
    lv_obj_set_style_bg_opa(pane, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pane, lv_color_hex(0xD9D9D4), 0);
    lv_obj_set_style_border_width(pane, 1, 0);
    lv_obj_set_style_border_side(pane, LV_BORDER_SIDE_LEFT, 0);
    s->head = ui_label(pane, "", UI_L_TEXT);
    lv_obj_set_width(s->head, 138);
    lv_obj_set_style_text_align(s->head, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s->head, 6, 22);
    s->body = ui_label(pane, "", UI_L_DIM);
    lv_obj_set_width(s->body, 138);
    lv_obj_set_style_text_align(s->body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s->body, 6, 0);
    lv_obj_set_pos(s->body, 6, 52);
    settings_update_pane(s);
    return &s->base;
}

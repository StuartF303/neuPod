// SPDX-License-Identifier: MPL-2.0
/*
 * Extras -> Test tone (docs/ui.md, docs/audio.md). Now Playing is in ui_nowplaying.c.
 *
 * Reads the player (player.h) every 200 ms and only touches LVGL when a value changed. Volume is software attenuation: Left / Right set it here. On the
 * Bluetooth output with a headset that has absolute volume the volume is the headset's: the label reads "Headset 62%" (bt_control.h).
 */
#include <stdio.h>
#include <string.h>

#include "art_service.h"
#include "audio_route.h"
#include "bt_control.h"
#include "hal_radio.h"
#include "os.h"
#include "player.h"
#include "ui_internal.h"

#define POLL_MS 200u

static void set_text(lv_obj_t *l, const char *t) { ui_label_set_text(l, t); } /* filters title / artist / album; skips an unchanged label */

static void vol_text(char *out, size_t n, uint32_t att) {
    if (att >= AUDIO_VOL_OFF_DB) {
        snprintf(out, n, "Volume off");
    } else if (att == 0) {
        snprintf(out, n, "Volume 0 dB");
    } else {
        snprintf(out, n, "Volume -%u dB", (unsigned)att);
    }
}

/* The volume line: the headset's (hv >= 0, bt_volume_get()) or the software one. */
static void vol_label(char *out, size_t n, uint32_t att, int hv) {
    if (hv >= 0) {
        snprintf(out, n, "Headset %u%%", bt_vol_percent((uint8_t)hv));
    } else {
        vol_text(out, n, att);
    }
}

/* ---- Test tone -------------------------------------------------------------------------------------- */

static const tone_mode_t k_modes[3] = {TONE_1K, TONE_IDENT, TONE_SCALE};
static const char *const k_mode_label[3] = {"1 kHz tone", "L/R ident (low left, high right)", "Scale (C4 up to C5 and back)"};

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    lv_obj_t *vol, *level;
    player_info_t info;
    int hv;               /* the headset's volume shown (-1: the software volume) */
    uint32_t last_ms;
} tone_t;

static bool mode_on(const tone_t *t, int32_t i) {
    return t->info.src == PLAYER_SRC_TONE && t->info.tone == k_modes[i] && t->info.state == PLAYER_PLAYING;
}

static void tone_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    const tone_t *t = ctx;
    snprintf(out->label, sizeof out->label, "%s", k_mode_label[idx]);
    snprintf(out->detail, sizeof out->detail, "%s", mode_on(t, idx) ? "On" : "Off");
}

static void tone_refresh(tone_t *t, bool force) {
    player_info_t n;
    player_get(&n);
    bool changed = force || n.src != t->info.src || n.tone != t->info.tone || n.state != t->info.state;
    int hv = bt_volume_get();
    bool vol_changed = n.vol_att_db != t->info.vol_att_db || hv != t->hv || force;
    t->info = n;
    t->hv = hv;
    if (changed) {
        ui_list_refresh(t->list);
    }
    if (vol_changed) {
        char b[64];
        vol_label(b, sizeof b, n.vol_att_db, hv);
        set_text(t->vol, b);
        if (n.vol_att_db >= AUDIO_VOL_OFF_DB) {
            snprintf(b, sizeof b, "Output: silent");
        } else {
            snprintf(b, sizeof b, "Tone peak at the DAC: -%u dBFS", (unsigned)(20u + n.vol_att_db));
        }
        set_text(t->level, b);
    }
}

static void tone_tick(ui_screen_t *s) {
    tone_t *t = (tone_t *)s;
    uint32_t now = os_tick_ms();
    if (now - t->last_ms < POLL_MS) {
        return;
    }
    t->last_ms = now;
    tone_refresh(t, false);
}

static void tone_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((tone_t *)s)->list, steps); }

static bool tone_button(ui_screen_t *s, uint8_t btn) {
    tone_t *t = (tone_t *)s;
    if (btn == HAL_BTN_SELECT) {
        int32_t i = ui_list_selected(t->list);
        if (i >= 0 && i < 3) {
            player_tone_toggle(k_modes[i]);
            tone_refresh(t, true);
        }
        return true;
    }
    if (btn == HAL_BTN_LEFT || btn == HAL_BTN_RIGHT) {
        bt_volume_step(btn == HAL_BTN_RIGHT ? 1 : -1);
        tone_refresh(t, false);
        return true;
    }
    return false;
}

static void tone_destroy(ui_screen_t *s) {
    tone_t *t = (tone_t *)s;
    ui_list_destroy(t->list);
    player_info_t n;
    player_get(&n);
    if (n.src == PLAYER_SRC_TONE) {
        player_tone_toggle(TONE_OFF); // leaving the screen ends the test signal: nothing keeps sounding unseen
    }
}

ui_screen_t *ui_screen_testtone_create(void) {
    tone_t *t = (tone_t *)ui_screen_new(sizeof *t, "Test tone");
    if (t == NULL) {
        return NULL;
    }
    t->base.shows_volume = true;
    t->base.on_scroll = tone_scroll;
    t->base.on_button = tone_button;
    t->base.on_tick = tone_tick;
    t->base.destroy = tone_destroy;
    t->list = ui_list_create(t->base.root, UI_W, &(ui_list_model_t){.count = 3, .ctx = t, .get = tone_get});
    lv_obj_t *warn = ui_label(t->base.root, "Select: start / stop.  Left / Right: volume.", UI_L_DIM);
    lv_obj_set_pos(warn, UIM->pad, 3 * UI_ROW_H + UIM->tt_y[0]);
    t->vol = ui_label(t->base.root, "", UI_L_TEXT);
    lv_obj_set_pos(t->vol, UIM->pad, 3 * UI_ROW_H + UIM->tt_y[1]);
    t->level = ui_label(t->base.root, "", UI_L_DIM);
    lv_obj_set_pos(t->level, UIM->pad, 3 * UI_ROW_H + UIM->tt_y[2]);
    lv_obj_t *safe = ui_label(t->base.root, "Starts quiet (-30 dB). Leaving this screen stops the sound.", UI_L_FAINT);
    lv_obj_set_width(safe, UI_W - 2 * UIM->pad);
    lv_obj_set_pos(safe, UIM->pad, 3 * UI_ROW_H + UIM->tt_y[3]);
    tone_refresh(t, true);
    return &t->base;
}

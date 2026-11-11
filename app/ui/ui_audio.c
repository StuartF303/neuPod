// SPDX-License-Identifier: MPL-2.0
/*
 * Extras -> Test tone, and the Now Playing screen (docs/ui.md, docs/audio.md).
 *
 * Both read the player (player.h) every 200 ms and only touch LVGL when a value changed. Volume is
 * software attenuation: on Now Playing the wheel sets it (Select hands the wheel to seek for a few seconds: ui_logic.h, ui_npc_*);
 * on Test tone use Left / Right.
 */
#include <stdio.h>
#include <string.h>

#include "art_service.h"
#include "os.h"
#include "player.h"
#include "ui_internal.h"

#define POLL_MS 200u

static void set_text(lv_obj_t *l, const char *t) {
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void vol_text(char *out, size_t n, uint32_t att) {
    if (att >= AUDIO_VOL_OFF_DB) {
        snprintf(out, n, "Volume off");
    } else if (att == 0) {
        snprintf(out, n, "Volume 0 dB");
    } else {
        snprintf(out, n, "Volume -%u dB", (unsigned)att);
    }
}

static lv_obj_t *bar_new(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    return b;
}

/* ---- Test tone -------------------------------------------------------------------------------------- */

static const tone_mode_t k_modes[3] = {TONE_1K, TONE_IDENT, TONE_SCALE};
static const char *const k_mode_label[3] = {"1 kHz tone", "L/R ident (low left, high right)", "Scale (C4 up to C5 and back)"};

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    lv_obj_t *vol, *level;
    player_info_t info;
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
    bool vol_changed = n.vol_att_db != t->info.vol_att_db || force;
    t->info = n;
    if (changed) {
        ui_list_refresh(t->list);
    }
    if (vol_changed) {
        char b[64];
        vol_text(b, sizeof b, n.vol_att_db);
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
        (void)player_volume_step(btn == HAL_BTN_RIGHT ? 1 : -1);
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

/* ---- Now Playing ------------------------------------------------------------------------------------ */

/* Now Playing geometry is all in the metrics (np_*); the bars span the margins, the volume bar starts under the text column. */
#define NP_BAR_W (UI_W - 2 * UIM->np_margin)
#define NP_VOL_X (UIM->np_text_x)
#define NP_VOL_W (UI_W - UIM->np_margin - NP_VOL_X)
#define NP_TEXT_W NP_VOL_W /* the text column and the volume bar share their right edge */

typedef struct {
    ui_screen_t base;
    lv_obj_t *title, *artist, *album, *badge, *state, *elapsed, *remain, *icon, *vol, *prog_fill, *prog_track, *vol_fill, *modes;
    lv_obj_t *art_img, *art_note; /* album art (hidden unless the decoder has a picture) and the note shown otherwise */
    lv_image_dsc_t art_dsc;       /* points at the art service's thumbnail; nothing is copied */
    uint32_t art_gen;
    bool art_shown;
    player_info_t info;
    uint32_t last_ms;
    bool have;
    int32_t prog_w, vol_w;
    ui_npc_t ctl;          /* wheel mode (volume / seek), scrub target, Left / Right hold */
    bool seek_shown;       /* the bars are drawn in seek colours */
    const char *msg;       /* a short notice shown on the status line until msg_until ("Seek not available") */
    uint32_t msg_until;
} np_t;

#define NP_NOTICE_MS 2000u
#define NP_COL_SEEK UI_L_WARN       /* progress bar while the wheel seeks */
#define NP_COL_SEEK_TRACK 0xFFE2B8  /* its unfilled part */
#define NP_COL_TRACK 0xE6E6E2

static ui_npc_track_t np_track(const player_info_t *n) {
    return (ui_npc_track_t){.seekable = n->seekable, .pos_frames = n->pos_frames, .total_frames = n->total_frames, .rate = n->rate};
}

static void np_notice(np_t *p, const char *text) {
    p->msg = text;
    p->msg_until = os_tick_ms() + NP_NOTICE_MS;
}

static void mmss(char *out, size_t n, const char *prefix, uint32_t s) {
    snprintf(out, n, "%s%u:%02u", prefix, (unsigned)(s / 60u), (unsigned)(s % 60u));
}

/* Show the thumbnail while the art service has one for the playing track, the note otherwise. The 64 px thumbnail is scaled up by LVGL to
 * fit the art box (112 px at 320x240; aspect kept, the rest of the box stays the dark background). The ack tells the decoder that this screen no longer
 * points at the old pixels, so it may overwrite them. */
static void np_art(np_t *p, const player_info_t *n) {
    art_view_t v;
    art_view_get(&v);
    bool live = n->src == PLAYER_SRC_FILE && (n->state == PLAYER_PLAYING || n->state == PLAYER_PAUSED || n->state == PLAYER_ENDED);
    if (live && v.state == ART_VIEW_READY && v.w != 0 && v.h != 0) {
        if (!p->art_shown || p->art_gen != v.gen) {
            memset(&p->art_dsc, 0, sizeof p->art_dsc);
            p->art_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            p->art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            p->art_dsc.header.w = v.w;
            p->art_dsc.header.h = v.h;
            p->art_dsc.header.stride = (uint32_t)v.w * 2u;
            p->art_dsc.data_size = (uint32_t)v.w * v.h * 2u;
            p->art_dsc.data = (const uint8_t *)v.px;
            lv_image_set_src(p->art_img, &p->art_dsc);
            lv_obj_set_size(p->art_img, UIM->np_art, UIM->np_art);
            lv_obj_invalidate(p->art_img);
            lv_obj_set_hidden(p->art_img, false);
            lv_obj_set_hidden(p->art_note, true);
            p->art_gen = v.gen;
            p->art_shown = true;
        }
    } else if (p->art_shown) {
        lv_obj_set_hidden(p->art_img, true);
        lv_obj_set_hidden(p->art_note, false);
        p->art_shown = false;
    }
    art_view_ack(v.gen);
}

static void np_update(np_t *p) {
    player_info_t n;
    player_get(&n);
    p->info = n;
    char b[96];
    const char *title = n.src == PLAYER_SRC_NONE ? "Nothing playing" : n.src == PLAYER_SRC_FILE ? n.tags.title : n.name;
    if (n.src == PLAYER_SRC_FILE && n.tags.title[0] == '\0') {
        title = n.name;
    }
    set_text(p->title, title);
    set_text(p->artist, n.src == PLAYER_SRC_FILE ? n.tags.artist : "");
    if (n.src == PLAYER_SRC_FILE && n.tags.album[0] != '\0') {
        if (n.tags.track != 0) {
            snprintf(b, sizeof b, "%s  #%u", n.tags.album, (unsigned)n.tags.track);
        } else {
            snprintf(b, sizeof b, "%s", n.tags.album);
        }
        set_text(p->album, b);
    } else if (n.src == PLAYER_SRC_FILE && n.tags.track != 0) {
        snprintf(b, sizeof b, "Track %u", (unsigned)n.tags.track);
        set_text(p->album, b);
    } else {
        set_text(p->album, "");
    }
    set_text(p->badge, n.src == PLAYER_SRC_FILE ? n.format : n.src == PLAYER_SRC_TONE ? "TEST SIGNAL" : "");
    const char *st = "Choose a file in Extras > Files, or a test tone";
    static const char *const verdict[] = {"", "Verifying", "MD5 OK: decoded audio matches the file", "MD5 MISMATCH: decoded audio differs",
                                          "No MD5 stored in this file"};
    switch (n.state) {
    case PLAYER_IDLE: break;
    case PLAYER_OPENING: st = "Opening..."; break;
    case PLAYER_PLAYING: st = "Playing"; break;
    case PLAYER_PAUSED: st = "Paused"; break;
    case PLAYER_ENDED: st = "Finished"; break;
    case PLAYER_ERROR: st = n.msg; break;
    case PLAYER_VERIFYING: st = "Verifying MD5 (no sound)..."; break;
    case PLAYER_VERIFIED: st = n.verify < 5 ? verdict[n.verify] : ""; break;
    }
    np_art(p, &n);
    char stq[128];
    if (n.queue_len > 1 && n.src == PLAYER_SRC_FILE && (n.state == PLAYER_PLAYING || n.state == PLAYER_PAUSED || n.state == PLAYER_OPENING)) {
        snprintf(stq, sizeof stq, "%s - %u of %u", st, (unsigned)n.queue_pos, (unsigned)n.queue_len); /* queue position on the status line */
        st = stq;
    }
    if (p->msg != NULL && (int32_t)(p->msg_until - os_tick_ms()) > 0) {
        st = p->msg; /* a notice replaces the status line for a moment */
    } else {
        p->msg = NULL;
    }
    set_text(p->state, st);
    /* shuffle / repeat indicators: only while a queue is playing; repeat-one carries a 1 */
    snprintf(b, sizeof b, "%s%s%s%s", n.shuffle ? LV_SYMBOL_SHUFFLE : "", n.shuffle && n.repeat ? " " : "",
             n.repeat ? LV_SYMBOL_LOOP : "", n.repeat == PQ_REPEAT_ONE ? "1" : "");
    set_text(p->modes, b);
    lv_obj_set_style_text_color(p->state, lv_color_hex(n.state == PLAYER_ERROR || (n.state == PLAYER_VERIFIED && n.verify == AUDIO_VERIFY_MISMATCH) ? UI_L_ERR : n.state == PLAYER_VERIFIED && n.verify == AUDIO_VERIFY_OK ? UI_L_OK : UI_L_DIM), 0);
    set_text(p->icon, n.state == PLAYER_PLAYING ? LV_SYMBOL_PLAY : n.state == PLAYER_PAUSED ? LV_SYMBOL_PAUSE : "");
    uint32_t el = n.state == PLAYER_ENDED && n.rate != 0 ? (uint32_t)(n.total_frames / n.rate) : n.elapsed_s;
    ui_npc_track_t trk = np_track(&n);
    bool seek_mode = p->ctl.mode == UI_WHEEL_SEEK;
    if (p->ctl.pending && n.rate != 0) {
        el = (uint32_t)(ui_npc_position(&p->ctl, &trk) / n.rate); /* a scrub in progress: show where it will land */
    }
    if (seek_mode != p->seek_shown) { /* the progress bar is the thing the wheel moves: highlighted; the volume bar greys out */
        p->seek_shown = seek_mode;
        lv_obj_set_style_bg_color(p->prog_fill, lv_color_hex(seek_mode ? NP_COL_SEEK : UI_L_SEL_BOT), 0);
        lv_obj_set_style_bg_color(p->prog_track, lv_color_hex(seek_mode ? NP_COL_SEEK_TRACK : NP_COL_TRACK), 0);
        lv_obj_set_style_bg_color(p->vol_fill, lv_color_hex(seek_mode ? UI_L_FAINT : UI_L_SEL_BOT), 0);
    }
    int32_t pw = 0;
    if (n.src == PLAYER_SRC_FILE && n.rate != 0 && n.total_frames != 0 &&
        (n.state == PLAYER_PLAYING || n.state == PLAYER_PAUSED || n.state == PLAYER_ENDED || n.state == PLAYER_VERIFYING || n.state == PLAYER_VERIFIED)) {
        uint32_t total = (uint32_t)(n.total_frames / n.rate);
        mmss(b, sizeof b, "", el);
        set_text(p->elapsed, b);
        mmss(b, sizeof b, n.estimated ? "-~" : "-", total > el ? total - el : 0);
        set_text(p->remain, b);
        pw = (int32_t)((uint64_t)NP_BAR_W * (n.state == PLAYER_ENDED || n.state == PLAYER_VERIFIED ? n.total_frames : (uint64_t)el * n.rate) / n.total_frames);
        if (p->ctl.pending && n.state != PLAYER_ENDED) {
            pw = (int32_t)((uint64_t)NP_BAR_W * ui_npc_position(&p->ctl, &trk) / n.total_frames); /* frame-accurate target */
        }
        if (pw > NP_BAR_W) {
            pw = NP_BAR_W;
        }
    } else {
        set_text(p->elapsed, n.src == PLAYER_SRC_NONE ? "0:00" : "--:--");
        set_text(p->remain, "-:--");
    }
    if (pw != p->prog_w) {
        p->prog_w = pw;
        lv_obj_set_width(p->prog_fill, pw > 0 ? pw : 1);
        if (pw == 0) {
            lv_obj_set_hidden(p->prog_fill, true);
        } else {
            lv_obj_set_hidden(p->prog_fill, false);
        }
    }
    if (seek_mode) {
        mmss(b, sizeof b, "Seek ", el);
    } else {
        vol_text(b, sizeof b, n.vol_att_db);
    }
    set_text(p->vol, b);
    uint32_t span = AUDIO_VOL_OFF_DB;
    int32_t vw = n.vol_att_db >= span ? 0 : (int32_t)((span - n.vol_att_db) * NP_VOL_W / span);
    if (vw != p->vol_w) {
        p->vol_w = vw;
        lv_obj_set_width(p->vol_fill, vw > 0 ? vw : 1);
        if (vw == 0) {
            lv_obj_set_hidden(p->vol_fill, true);
        } else {
            lv_obj_set_hidden(p->vol_fill, false);
        }
    }
}

/* Apply the scrub target: one restart of the track at that frame (player_seek). */
static void np_commit(np_t *p) {
    uint64_t target = p->ctl.target;
    if (!player_seek(target)) {
        np_notice(p, "Seek not available"); /* the track changed under the scrub */
    }
    ui_npc_committed(&p->ctl);
    np_update(p);
}

static void np_events(np_t *p, uint32_t ev) {
    if ((ev & UI_NPC_UNAVAILABLE) != 0) {
        np_notice(p, "Seek not available");
    }
    if ((ev & UI_NPC_COMMIT) != 0) {
        np_commit(p);
    } else if (ev != UI_NPC_NONE) {
        np_update(p);
    }
}

static void np_tick(ui_screen_t *s) {
    np_t *p = (np_t *)s;
    uint32_t now = os_tick_ms();
    if (p->have) { /* the wheel-mode clock, scrub commit and hold run on every pass; the poll below is rate-limited */
        ui_npc_track_t trk = np_track(&p->info);
        np_events(p, ui_npc_tick(&p->ctl, &trk, now));
    }
    if (now - p->last_ms < POLL_MS && p->have) {
        return;
    }
    p->last_ms = now;
    p->have = true;
    np_update(p);
}

static void np_scroll(ui_screen_t *s, int32_t steps) {
    np_t *p = (np_t *)s;
    player_get(&p->info); /* a scrub starts from the position as it is now */
    ui_npc_track_t trk = np_track(&p->info);
    if (!ui_npc_scroll(&p->ctl, &trk, steps, os_tick_ms())) {
        (void)player_volume_step(steps); // clockwise = louder
    }
    np_update(p);
}

static bool np_button(ui_screen_t *s, uint8_t btn) {
    np_t *p = (np_t *)s;
    if (btn == HAL_BTN_SELECT) { /* wheel mode: volume <-> seek (pause is the Play button, which works on every screen) */
        player_get(&p->info);
        ui_npc_track_t trk = np_track(&p->info);
        if (!ui_npc_select(&p->ctl, &trk, os_tick_ms()) && p->info.src == PLAYER_SRC_FILE) {
            np_notice(p, "Seek not available"); /* MP3 / ALAC (no frame index yet), FLAC without a seek table, MD5 check */
        }
        np_update(p);
        return true;
    }
    if (btn == HAL_BTN_PLAY) {
        player_pause_toggle();
        np_update(p);
        return true;
    }
    if (btn == HAL_BTN_RIGHT || btn == HAL_BTN_LEFT) {
        /* A short press skips (on release, so that a hold can be a fast-forward / rewind instead); a hold of UI_HOLD_MS scrubs. */
        player_get(&p->info);
        ui_npc_key_down(&p->ctl, btn == HAL_BTN_RIGHT ? 1 : -1, os_tick_ms());
        return true;
    }
    return false; // Menu = back; playback continues
}

static void np_button_up(ui_screen_t *s, uint8_t btn) {
    np_t *p = (np_t *)s;
    if (btn != HAL_BTN_RIGHT && btn != HAL_BTN_LEFT) {
        return;
    }
    int dir = btn == HAL_BTN_RIGHT ? 1 : -1;
    uint32_t ev = ui_npc_key_up(&p->ctl, dir, os_tick_ms());
    if ((ev & UI_NPC_SKIP) != 0) { /* next / previous track of the queue (Previous restarts after 3 s) */
        if (dir > 0) {
            player_next();
        } else {
            player_prev();
        }
        np_update(p);
    }
    np_events(p, ev & ~UI_NPC_SKIP);
}

ui_screen_t *ui_screen_nowplaying_create(void) {
    np_t *p = (np_t *)ui_screen_new(sizeof *p, "Now Playing");
    if (p == NULL) {
        return NULL;
    }
    lv_obj_t *r = p->base.root;
    lv_obj_t *art = ui_artwork(r, UIM->np_margin, UIM->np_margin, UIM->np_art);
    lv_obj_clean(art); /* the ring of the shared placeholder is replaced by a note */
    p->art_img = lv_image_create(art);
    lv_obj_set_size(p->art_img, UIM->np_art, UIM->np_art);
    lv_image_set_inner_align(p->art_img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(p->art_img);
    lv_obj_set_hidden(p->art_img, true);
    p->art_note = ui_label(art, LV_SYMBOL_AUDIO, 0xFFFFFF);
    lv_obj_set_style_text_font(p->art_note, UIM->font_huge, 0);
    lv_obj_set_style_text_opa(p->art_note, LV_OPA_50, 0);
    lv_obj_center(p->art_note);
    p->title = ui_label(r, "", UI_L_TEXT);
    lv_obj_set_style_text_font(p->title, UIM->font_large, 0);
    lv_obj_set_size(p->title, NP_TEXT_W, UIM->np_title_h);
    lv_label_set_long_mode(p->title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(p->title, UIM->np_text_x, UIM->np_title_y);
    p->artist = ui_label(r, "", UI_L_TEXT);
    lv_obj_set_size(p->artist, NP_TEXT_W, UIM->line_h);
    lv_label_set_long_mode(p->artist, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(p->artist, UIM->np_text_x, UIM->np_artist_y);
    p->album = ui_label(r, "", UI_L_DIM);
    lv_obj_set_size(p->album, NP_TEXT_W, UIM->line_h);
    lv_label_set_long_mode(p->album, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(p->album, UIM->np_text_x, UIM->np_album_y);
    p->badge = ui_label(r, "", UI_L_SEL_BOT);
    lv_obj_set_pos(p->badge, UIM->np_text_x, UIM->np_badge_y);
    p->state = ui_label(r, "", UI_L_DIM);
    lv_obj_set_size(p->state, NP_TEXT_W, UIM->np_state_h);
    lv_label_set_long_mode(p->state, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(p->state, UIM->np_text_x, UIM->np_state_y);

    p->prog_track = bar_new(r, UIM->np_margin, UIM->np_bar_y, NP_BAR_W, UIM->np_bar_h, NP_COL_TRACK);
    p->prog_fill = bar_new(r, UIM->np_margin, UIM->np_bar_y, 1, UIM->np_bar_h, UI_L_SEL_BOT);
    lv_obj_set_hidden(p->prog_fill, true);
    p->elapsed = ui_label(r, "0:00", UI_L_DIM);
    lv_obj_set_pos(p->elapsed, UIM->np_margin, UIM->np_time_y);
    p->remain = ui_label(r, "-:--", UI_L_DIM);
    lv_obj_align(p->remain, LV_ALIGN_TOP_RIGHT, -UIM->np_margin, UIM->np_time_y);
    p->icon = ui_label(r, "", UI_L_FAINT);
    lv_obj_align(p->icon, LV_ALIGN_TOP_MID, 0, UIM->np_time_y);

    p->modes = ui_label(r, "", UI_L_FAINT);
    lv_obj_align(p->modes, LV_ALIGN_TOP_MID, UIM->np_modes_dx, UIM->np_time_y);
    p->vol = ui_label(r, "", UI_L_TEXT);
    lv_obj_set_pos(p->vol, UIM->np_margin, UIM->np_vol_y);
    bar_new(r, NP_VOL_X, UIM->np_vol_bar_y, NP_VOL_W, UIM->np_vol_bar_h, 0xE6E6E2);
    p->vol_fill = bar_new(r, NP_VOL_X, UIM->np_vol_bar_y, 1, UIM->np_vol_bar_h, UI_L_SEL_BOT);
    p->prog_w = -1;
    p->vol_w = -1;
    p->base.on_tick = np_tick;
    p->base.on_scroll = np_scroll;
    p->base.on_button = np_button;
    p->base.on_button_up = np_button_up;
    ui_npc_init(&p->ctl);
    np_tick(&p->base);
    return &p->base;
}

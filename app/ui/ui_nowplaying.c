// SPDX-License-Identifier: MPL-2.0
/*
 * Now Playing (design review 3.2, docs/ui.md "Now Playing"): page 1 is the track (art, Title / Artist / Album . Year, the format and output pills, the
 * progress bar with elapsed and remaining time, the volume strip); Select opens page 2 (rating, Shuffle / Repeat / Gapless, Go to album / artist); HOLD Select
 * is seek mode (the wheel moves the target, Select applies it, Menu drops it). Text that does not fit scrolls (the ticker rule).
 *
 * Page 1 reads the player (player.h) every 200 ms and only touches LVGL when a value changed. Volume is software attenuation: the wheel sets it; on a
 * Bluetooth output with a headset that has absolute volume the volume is the headset's ("Headset 62%", the same 0..127 scale, bt_control.h). Page 1
 * draws the volume itself, so the volume HUD (ui_frame.c) stays away while it is on top.
 */
#include <stdio.h>
#include <string.h>

#include "art_service.h"
#include "audio_route.h"
#include "bt_control.h"
#include "bt_saved.h"
#include "hal_radio.h"
#include "library_service.h"
#include "os.h"
#include "player.h"
#include "playlist_service.h"
#include "plays_store.h"
#include "ratings_store.h"
#include "ui_internal.h"
#include "ui_library.h"

#define POLL_MS 200u
#define NOTICE_MS 2000u

static void set_text(lv_obj_t *l, const char *t) { ui_label_set_text(l, t); } /* filters title / artist / album; skips an unchanged label */
static void show(lv_obj_t *o, bool on) {
    if (lv_obj_is_hidden(o) == on) {
        lv_obj_set_hidden(o, !on);
    }
}

static void mmss(char *out, size_t n, const char *prefix, uint32_t s) { snprintf(out, n, "%s%u:%02u", prefix, (unsigned)(s / 60u), (unsigned)(s % 60u)); }

static void vol_label(char *out, size_t n, uint32_t att, int hv) {
    if (hv >= 0) {
        snprintf(out, n, "Headset %u%%", bt_vol_percent((uint8_t)hv));
    } else if (att >= AUDIO_VOL_OFF_DB) {
        snprintf(out, n, "Volume off");
    } else if (att == 0) {
        snprintf(out, n, "Volume 0 dB");
    } else {
        snprintf(out, n, "Volume -%u dB", (unsigned)att);
    }
}

/* Width of the volume bar's fill out of @p w pixels. */
static int32_t vol_fill_w(uint32_t att, int hv, int32_t w) {
    if (hv >= 0) {
        return (int32_t)((uint32_t)hv * (uint32_t)w / HAL_BT_VOLUME_MAX);
    }
    return att >= AUDIO_VOL_OFF_DB ? 0 : (int32_t)((AUDIO_VOL_OFF_DB - att) * (uint32_t)w / AUDIO_VOL_OFF_DB);
}

static lv_obj_t *bar_new(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, h / 2, 0);
    ui_obj_bg(b, col, 0);
    return b;
}

static ui_npc_track_t np_track(const player_info_t *n) {
    return (ui_npc_track_t){.seekable = n->seekable, .pos_frames = n->pos_frames, .total_frames = n->total_frames, .rate = n->rate, .id = n->track_id};
}

/* The folder a file lives in ("/Music/Aurora Lane/First Light/01.flac" -> "First Light"): the artist line of a track that has no tags. */
static void folder_of(const char *path, char *out, size_t n) {
    out[0] = '\0';
    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash == path) {
        return;
    }
    const char *end = slash;
    const char *start = end;
    while (start > path && start[-1] != '/') {
        start--;
    }
    size_t len = (size_t)(end - start);
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
}

/* ---- the ratings ------------------------------------------------------------------------------------------------------------------ */
/* Kept on the card by path + size (ratings_store.h); on a write-protected card they stay in RAM until it can be written. */

static uint32_t track_key(const player_info_t *n) {
    char path[160];
    if (!player_now_path(path, sizeof path)) {
        snprintf(path, sizeof path, "%s", n->name);
    }
    return ratings_key(path, n->file_size);
}
static uint8_t rating_get(uint32_t key) { return ratings_get(key); }
static void rating_set(uint32_t key, uint8_t stars) { ratings_set(key, stars); }

/* ---- page 1 ---------------------------------------------------------------------------------------------------------------------------- */

/* Now Playing geometry is all in the metrics (np_*); the text column ends at the right margin, the bars span the margins. */
#define NP_BAR_W (UI_W - 2 * UIM->np_margin)
#define NP_TEXT_W (UI_W - UIM->np_text_x - UIM->np_margin)
#define NP_VOL_W (UI_W - UIM->np_margin - UIM->np_vol_x)

typedef struct {
    ui_screen_t base;
    ui_artbox_t *art;
    lv_obj_t *title, *artist, *album;
    lv_obj_t *pill_fmt, *pill_fmt_l, *pill_out, *pill_out_l;
    lv_obj_t *seek_time, *hint;
    lv_obj_t *icon, *elapsed, *remain, *modes;
    lv_obj_t *prog_track, *prog_fill, *knob;
    lv_obj_t *vol, *vol_track, *vol_fill;
    player_info_t info;
    uint32_t last_ms;
    bool have;
    int32_t prog_w, vol_w;
    ui_npc_t ctl;          /* wheel mode (volume / seek), the seek target, Select and Left / Right holds */
    bool seek_shown;       /* the screen is drawn in seek mode */
    uint32_t fmt_col, vol_col;
    uint32_t track_seen;   /* player_info_t.track_id the folder name below belongs to */
    char folder[48];       /* the folder of the playing file (the artist line when there are no tags) */
} np_t;

static void np_notice(const char *text) { ui_toast_show(text, UI_TOAST_WARN, NOTICE_MS); }

/* Show the art service's thumbnail while it has one for the playing track, the placeholder otherwise. The ack tells the decoder that this screen no longer
 * points at the old pixels, so it may overwrite them (a cache of one thumbnail only). */
static void np_art(np_t *p, const player_info_t *n) {
    art_view_t v;
    art_view_get(&v);
    bool live = n->src == PLAYER_SRC_FILE && (n->state == PLAYER_PLAYING || n->state == PLAYER_PAUSED || n->state == PLAYER_ENDED);
    if (live && v.state == ART_VIEW_READY && v.w != 0 && v.h != 0) {
        ui_artbox_show(p->art, v.px, v.w, v.h, v.gen);
    } else {
        ui_artbox_show(p->art, NULL, 0, 0, v.gen);
    }
    art_view_ack(v.gen);
}

/* A pill: its box is as wide as its text plus the padding; @p max_text_w cuts a long name with dots. Returns the pill's width (0 when hidden). */
static int32_t pill_set(lv_obj_t *box, lv_obj_t *label, const char *text, int32_t x, int32_t max_w) {
    if (text[0] == '\0' || max_w < 2 * UIM->np_pill_pad + 8) {
        show(box, false);
        return 0;
    }
    set_text(label, text);
    lv_obj_set_width(label, LV_SIZE_CONTENT); /* measure the text at its natural width */
    lv_obj_update_layout(label);
    int32_t tw = lv_obj_get_width(label);
    int32_t w = tw + 2 * UIM->np_pill_pad;
    if (w > max_w) {
        w = max_w;
        tw = w - 2 * UIM->np_pill_pad;
    }
    lv_obj_set_width(label, tw);
    lv_obj_set_width(box, w);
    lv_obj_set_x(box, x);
    show(box, true);
    return w;
}

static void np_update(np_t *p) {
    player_info_t n;
    player_get(&n);
    p->info = n;
    char b[128];
    bool file = n.src == PLAYER_SRC_FILE;
    if (n.track_id != p->track_seen) { /* another track: its folder (for the artist line of an untagged file) */
        p->track_seen = n.track_id;
        char path[160];
        p->folder[0] = '\0';
        if (file && player_now_path(path, sizeof path)) {
            folder_of(path, p->folder, sizeof p->folder);
        }
    }
    const bool untagged = file && n.tags.title[0] == '\0';
    const char *title = n.src == PLAYER_SRC_NONE ? "Nothing playing" : file ? (untagged ? n.name : n.tags.title) : n.name;
    set_text(p->title, title);
    set_text(p->artist, file ? (n.tags.artist[0] != '\0' ? n.tags.artist : untagged ? p->folder : "") : "");

    /* the third line: "Album . Year", or what is happening when that is not just playing */
    static const char *const verdict[] = {"", "Verifying", "MD5 OK: decoded audio matches the file", "MD5 MISMATCH: decoded audio differs", "No MD5 stored in this file"};
    uint32_t line_col = UI_C_INK3;
    b[0] = '\0';
    switch (n.state) {
    case PLAYER_IDLE: snprintf(b, sizeof b, "Choose a song in Music or Files"); break;
    case PLAYER_OPENING: snprintf(b, sizeof b, "Opening..."); break;
    case PLAYER_ERROR:
        snprintf(b, sizeof b, "%s", n.msg);
        line_col = UI_C_RED;
        break;
    case PLAYER_VERIFYING: snprintf(b, sizeof b, "Verifying MD5 (no sound)..."); break;
    case PLAYER_VERIFIED:
        snprintf(b, sizeof b, "%s", n.verify < 5 ? verdict[n.verify] : "");
        line_col = n.verify == AUDIO_VERIFY_OK ? UI_C_GREEN : n.verify == AUDIO_VERIFY_MISMATCH ? UI_C_RED : UI_C_INK3;
        break;
    case PLAYER_ENDED: snprintf(b, sizeof b, "Finished"); break;
    default:
        if (file && n.tags.album[0] != '\0') {
            if (n.tags.year != 0) {
                snprintf(b, sizeof b, "%s \xC2\xB7 %u", n.tags.album, (unsigned)n.tags.year);
            } else {
                snprintf(b, sizeof b, "%s", n.tags.album);
            }
        } else if (file && n.tags.year != 0) {
            snprintf(b, sizeof b, "%u", (unsigned)n.tags.year);
        }
        break;
    }
    set_text(p->album, b);
    ui_label_recolour(p->album, p->vol_col == 0 ? UI_C_INK3 : p->vol_col, line_col); /* vol_col holds the colour the line has now (0: the initial ink3) */
    p->vol_col = line_col;

    /* the pills: the format (what leaves the box: the Bluetooth codec on a Bluetooth output) and the output */
    char fmt[32];
    fmt[0] = '\0';
    if (file) {
        snprintf(fmt, sizeof fmt, "%s", n.format);
    } else if (n.src == PLAYER_SRC_TONE) {
        snprintf(fmt, sizeof fmt, "TEST SIGNAL");
    }
    const bool bt_out = audio_route_output() == AUDIO_OUT_BT;
    hal_bt_codec_t codec;
    if (n.src != PLAYER_SRC_NONE && bt_out && hal_bt_stream_codec(&codec)) {
        hal_bt_codec_label(&codec, fmt, sizeof fmt);
    }
    char outn[HAL_BT_NAME_MAX + 8];
    snprintf(outn, sizeof outn, "Wired");
    if (bt_out) {
        hal_bt_device_t sv;
        char raw[HAL_BT_NAME_MAX + 8] = "Headset";
        if (bt_saved_get(&sv) && sv.name[0] != '\0') {
            snprintf(raw, sizeof raw, "%s", sv.name);
        }
        ui_text_display(outn, sizeof outn, raw);
    }
    const bool seek_mode = p->ctl.mode == UI_WHEEL_SEEK;
    const bool pills = !seek_mode && n.src != PLAYER_SRC_NONE;
    int32_t fw = pills ? pill_set(p->pill_fmt, p->pill_fmt_l, fmt, UIM->np_text_x, NP_TEXT_W) : 0;
    if (!pills) {
        show(p->pill_fmt, false);
    }
    if (pills) {
        int32_t ox = UIM->np_text_x + (fw > 0 ? fw + UIM->np_pill_gap : 0);
        (void)pill_set(p->pill_out, p->pill_out_l, outn, ox, UIM->np_text_x + NP_TEXT_W - ox);
    } else {
        show(p->pill_out, false);
    }

    /* the play state: a pause glyph before the elapsed time while paused */
    const bool paused = n.state == PLAYER_PAUSED;
    show(p->icon, paused);
    int32_t ex = UIM->np_margin + (paused ? lv_font_get_line_height(UIM->font_icon_sm) + UIM->np_pill_gap / 2 : 0);
    if (lv_obj_get_x(p->elapsed) != ex) {
        lv_obj_set_x(p->elapsed, ex);
    }

    /* the word in the middle of the time row: shuffle and repeat, only when either is on */
    b[0] = '\0';
    if (n.queue_len > 0 && file) {
        const char *rep = n.repeat == PQ_REPEAT_ONE ? "repeat 1" : n.repeat == PQ_REPEAT_ALL ? "repeat" : "";
        snprintf(b, sizeof b, "%s%s%s", n.shuffle ? "shuffle" : "", n.shuffle && rep[0] != '\0' ? " \xC2\xB7 " : "", rep);
    }
    set_text(p->modes, b);

    /* the position */
    uint32_t el = n.state == PLAYER_ENDED && n.rate != 0 ? (uint32_t)(n.total_frames / n.rate) : n.elapsed_s;
    ui_npc_track_t trk = np_track(&n);
    if (p->ctl.pending && n.rate != 0) {
        el = (uint32_t)(ui_npc_position(&p->ctl, &trk) / n.rate); /* a scrub in progress: show where it will land */
    }
    if (seek_mode != p->seek_shown) { /* seek mode: the Big amber time and the hints replace the pills and the volume strip; the bar grows a knob */
        p->seek_shown = seek_mode;
        int32_t h = seek_mode ? UIM->np_bar_seek_h : UIM->np_bar_h;
        int32_t y = UIM->np_bar_y + UIM->np_bar_h / 2 - h / 2;
        lv_obj_set_height(p->prog_track, h);
        lv_obj_set_y(p->prog_track, y);
        lv_obj_set_height(p->prog_fill, h);
        lv_obj_set_y(p->prog_fill, y);
        lv_obj_set_style_radius(p->prog_track, h / 2, 0);
        lv_obj_set_style_radius(p->prog_fill, h / 2, 0);
        ui_obj_bg_swap(p->prog_fill, seek_mode ? UI_C_ACCENT : UI_C_AMBER, seek_mode ? UI_C_AMBER : UI_C_ACCENT, 0);
        ui_obj_bg_swap(p->prog_track, seek_mode ? UI_C_LINE : UI_C_AMBER_SOFT, seek_mode ? UI_C_AMBER_SOFT : UI_C_LINE, 0);
        ui_label_recolour(p->elapsed, seek_mode ? UI_C_INK2 : UI_C_AMBER, seek_mode ? UI_C_AMBER : UI_C_INK2);
        show(p->seek_time, seek_mode);
        show(p->hint, seek_mode);
        show(p->album, !seek_mode);
        show(p->vol, !seek_mode);
        show(p->vol_track, !seek_mode);
        show(p->vol_fill, !seek_mode && p->vol_w != 0);
        show(p->knob, seek_mode);
        p->base.shows_volume = !seek_mode; /* the strip is gone: a volume change shows the HUD instead */
        p->prog_w = -1;
    }
    int32_t pw = 0;
    if (file && n.rate != 0 && n.total_frames != 0 &&
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
        if (seek_mode) {
            mmss(b, sizeof b, "", el);
            set_text(p->seek_time, b);
        }
    } else {
        set_text(p->elapsed, n.src == PLAYER_SRC_NONE ? "0:00" : "--:--");
        set_text(p->remain, "-:--");
        if (seek_mode) {
            set_text(p->seek_time, "0:00");
        }
    }
    if (pw != p->prog_w) {
        p->prog_w = pw;
        lv_obj_set_width(p->prog_fill, pw > 0 ? pw : 1);
        show(p->prog_fill, pw > 0);
        if (seek_mode) { /* the knob sits on the end of the fill */
            lv_obj_set_pos(p->knob, UIM->np_margin + pw - UIM->np_knob / 2, UIM->np_bar_y + UIM->np_bar_h / 2 - UIM->np_knob / 2);
        }
    }
    const int hv = bt_volume_get();
    if (!seek_mode) {
        vol_label(b, sizeof b, n.vol_att_db, hv);
        uint32_t vc = UI_C_INK2;
        if (hv == 0 && n.state == PLAYER_PLAYING) { /* the headset is at 0 %: it plays, but silently (an AirPods Pro left at 0 % by a phone): say so */
            snprintf(b, sizeof b, "Headset 0%% \xC2\xB7 silent");
            vc = UI_C_AMBER;
        }
        set_text(p->vol, b);
        if (vc != p->fmt_col && (p->fmt_col != 0 || vc != UI_C_INK2)) {
            ui_label_recolour(p->vol, p->fmt_col == 0 ? UI_C_INK2 : p->fmt_col, vc);
            p->fmt_col = vc;
        }
        int32_t vw = vol_fill_w(n.vol_att_db, hv, NP_VOL_W);
        if (vw != p->vol_w) {
            p->vol_w = vw;
            lv_obj_set_width(p->vol_fill, vw > 0 ? vw : 1);
            show(p->vol_fill, vw > 0);
        }
    }
    np_art(p, &n);
    if (n.queue_len > 1 && file) { /* the status bar's left slot (design review 3.10): where this track sits in its queue */
        snprintf(b, sizeof b, "%u of %u", (unsigned)n.queue_pos, (unsigned)n.queue_len);
        ui_screen_set_left(&p->base, b);
    } else {
        ui_screen_set_left(&p->base, "");
    }
}

/* Apply the scrub target: one restart of the track at that frame (player_seek). */
static void np_commit(np_t *p) {
    player_get(&p->info);
    ui_npc_track_t trk = np_track(&p->info);
    if (!ui_npc_commit_valid(&p->ctl, &trk)) { /* the scrub was made on another track (a handoff or a skip since): drop it */
        ui_npc_committed(&p->ctl);
        np_update(p);
        return;
    }
    if (!player_seek(p->ctl.target)) {
        np_notice("Seek not available"); /* the track changed under the scrub */
    }
    ui_npc_committed(&p->ctl);
    np_update(p);
}

static void np_events(np_t *p, uint32_t ev) {
    if ((ev & UI_NPC_UNAVAILABLE) != 0) {
        np_notice("Seek not available"); /* MP3 / ALAC (no frame index yet), FLAC without a seek table, MD5 check */
    }
    if ((ev & UI_NPC_SEEK_ENTER) != 0) {
        ui_feedback_step(); /* the haptic tick of the hold */
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
    if (p->have) { /* the wheel-mode clock, the Select hold, the scrub commit and the Left / Right hold run on every pass; the poll below is rate-limited */
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
    ui_npc_cancel_hold(&p->ctl); /* the wheel moved while a key was down: that key's release may never be seen */
    player_get(&p->info); /* a scrub starts from the position as it is now */
    ui_npc_track_t trk = np_track(&p->info);
    if (!ui_npc_scroll(&p->ctl, &trk, steps, os_tick_ms())) {
        bt_volume_step(steps); // clockwise = louder
    }
    np_update(p);
}

static bool np_button(ui_screen_t *s, uint8_t btn) {
    np_t *p = (np_t *)s;
    if (btn != HAL_BTN_RIGHT && btn != HAL_BTN_LEFT) {
        ui_npc_cancel_hold(&p->ctl); /* another key while Left / Right is down */
    }
    if (btn == HAL_BTN_SELECT) { /* short: page 2 (on release); held: seek mode; in seek mode: go */
        np_events(p, ui_npc_select_down(&p->ctl, os_tick_ms()));
        return true;
    }
    if (btn == HAL_BTN_MENU) {
        if (ui_npc_menu(&p->ctl)) { /* "Menu: cancel": leave seek mode, nothing applied */
            np_update(p);
            return true;
        }
        return false; // Menu = back; playback continues
    }
    if (btn == HAL_BTN_PLAY) {
        player_pause_toggle();
        np_update(p);
        return true;
    }
    if (btn == HAL_BTN_RIGHT || btn == HAL_BTN_LEFT) {
        /* A short press skips (on release, so that a hold can be a fast-forward / rewind instead); a hold of UI_HOLD_MS scrubs. */
        player_get(&p->info);
        ui_npc_track_t trk = np_track(&p->info);
        ui_npc_key_down(&p->ctl, btn == HAL_BTN_RIGHT ? 1 : -1, &trk, os_tick_ms());
        return true;
    }
    return false;
}

/* Another screen was pushed over this one: its key releases will not arrive here, so a hold or a scrub in progress is dropped. */
static void np_suspend(ui_screen_t *s) {
    np_t *p = (np_t *)s;
    ui_npc_cancel_hold(&p->ctl);
    p->ctl.pending = false;
    p->ctl.mode = UI_WHEEL_VOLUME;
    p->ctl.sel_down = false;
    p->ctl.sel_used = false;
}

static void np_resume(ui_screen_t *s) {
    np_t *p = (np_t *)s;
    p->last_ms = 0;
    p->have = false; /* look again at once: the track may have changed while page 2 or a list was on top */
    p->track_seen = UINT32_MAX;
    np_tick(s);
}

static void np_button_up(ui_screen_t *s, uint8_t btn) {
    np_t *p = (np_t *)s;
    if (btn == HAL_BTN_SELECT) {
        if ((ui_npc_select_up(&p->ctl) & UI_NPC_PAGE2) != 0) {
            player_get(&p->info);
            if (p->info.src == PLAYER_SRC_FILE) {
                ui_frame_push(ui_screen_nowplaying_page2_create());
            } else {
                np_notice("Nothing playing from a file");
            }
        }
        return;
    }
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

static void np_destroy(ui_screen_t *s) {
    np_t *p = (np_t *)s;
    ui_artbox_destroy(p->art);
    for (lv_obj_t *l = p->title; l != NULL; l = (l == p->title ? p->artist : l == p->artist ? p->album : NULL)) {
        ui_label_ticker(l, false); /* the registry must not hold labels that are about to go */
    }
}

static lv_obj_t *pill_new(lv_obj_t *parent, uint32_t bg, uint32_t line, uint32_t ink, lv_obj_t **label) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 2 * UIM->np_pill_pad + 8, UIM->np_pill_h);
    lv_obj_set_pos(box, UIM->np_text_x, UIM->np_pill_y);
    lv_obj_set_style_radius(box, UIM->np_pill_h / 2, 0);
    lv_obj_set_scrollable(box, false);
    if (bg != 0) {
        ui_obj_bg(box, bg, 0);
    }
    if (line != 0) {
        ui_obj_border(box, line, 1, LV_BORDER_SIDE_FULL);
    }
    *label = ui_label_small(box, "", ink);
    lv_label_set_long_mode(*label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(*label, lv_font_get_line_height(UIM->font_small));
    lv_obj_align(*label, LV_ALIGN_LEFT_MID, UIM->np_pill_pad, 0);
    lv_obj_set_hidden(box, true);
    return box;
}

static lv_obj_t *line_label(lv_obj_t *parent, const lv_font_t *font, int32_t y, int32_t h, uint32_t colour, bool small) {
    lv_obj_t *l = small ? ui_label_small(parent, "", colour) : ui_label(parent, "", colour);
    if (font != NULL) {
        lv_obj_set_style_text_font(l, font, 0);
    }
    lv_obj_set_size(l, NP_TEXT_W, h);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    ui_label_ticker(l, true); /* the owner's rule: Now Playing's title, artist and album scroll when they do not fit */
    lv_obj_set_pos(l, UIM->np_text_x, y);
    return l;
}

ui_screen_t *ui_screen_nowplaying_create(void) {
    np_t *p = (np_t *)ui_screen_new(sizeof *p, "Now Playing");
    if (p == NULL) {
        return NULL;
    }
    lv_obj_t *r = p->base.root;
    p->art = ui_artbox_create(r, UIM->np_margin, UIM->np_art_y, UIM->np_art);
    p->title = line_label(r, UIM->font_large, UIM->np_title_y, UIM->np_title_h, UI_C_INK, false);
    p->artist = line_label(r, NULL, UIM->np_artist_y, UIM->line_h, UI_C_INK2, false);
    p->album = line_label(r, NULL, UIM->np_album_y, UIM->np_album_h, UI_C_INK3, true);
    p->pill_fmt = pill_new(r, UI_C_ACCENT_SOFT, 0, UI_C_ACCENT, &p->pill_fmt_l);
    p->pill_out = pill_new(r, 0, UI_C_LINE, UI_C_INK2, &p->pill_out_l);
    p->seek_time = ui_label(r, "0:00", UI_C_AMBER);
    lv_obj_set_style_text_font(p->seek_time, UIM->font_huge, 0);
    lv_obj_set_size(p->seek_time, NP_TEXT_W, lv_font_get_line_height(UIM->font_huge));
    lv_label_set_long_mode(p->seek_time, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(p->seek_time, UIM->np_text_x, UIM->np_seek_y);
    lv_obj_set_hidden(p->seek_time, true);

    int32_t bar_y = UIM->np_bar_y;
    p->prog_track = bar_new(r, UIM->np_margin, bar_y, NP_BAR_W, UIM->np_bar_h, UI_C_LINE);
    p->prog_fill = bar_new(r, UIM->np_margin, bar_y, 1, UIM->np_bar_h, UI_C_ACCENT);
    lv_obj_set_hidden(p->prog_fill, true);
    p->knob = bar_new(r, UIM->np_margin, bar_y, UIM->np_knob, UIM->np_knob, UI_C_AMBER);
    lv_obj_set_style_radius(p->knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_hidden(p->knob, true);
    p->icon = ui_icon(r, UI_ICON_PAUSE, false, UI_C_INK2); /* shown while paused */
    lv_obj_set_pos(p->icon, UIM->np_margin, UIM->np_time_y);
    lv_obj_set_hidden(p->icon, true);
    p->elapsed = ui_label_small(r, "0:00", UI_C_INK2);
    lv_obj_set_pos(p->elapsed, UIM->np_margin, UIM->np_time_y);
    p->remain = ui_label_small(r, "-:--", UI_C_INK2);
    lv_obj_align(p->remain, LV_ALIGN_TOP_RIGHT, -UIM->np_margin, UIM->np_time_y);
    p->modes = ui_label_small(r, "", UI_C_INK3);
    lv_obj_set_width(p->modes, UI_W / 3);
    lv_obj_set_style_text_align(p->modes, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(p->modes, LV_LABEL_LONG_DOT);
    lv_obj_set_height(p->modes, lv_font_get_line_height(UIM->font_small));
    lv_obj_align(p->modes, LV_ALIGN_TOP_MID, 0, UIM->np_time_y);

    p->vol = ui_label_small(r, "", UI_C_INK2);
    lv_obj_set_size(p->vol, UIM->np_vol_x - UIM->np_margin - UIM->np_pill_gap, lv_font_get_line_height(UIM->font_small));
    lv_label_set_long_mode(p->vol, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(p->vol, UIM->np_margin, UIM->np_vol_y);
    p->vol_track = bar_new(r, UIM->np_vol_x, UIM->np_vol_bar_y, NP_VOL_W, UIM->np_vol_bar_h, UI_C_LINE);
    p->vol_fill = bar_new(r, UIM->np_vol_x, UIM->np_vol_bar_y, 1, UIM->np_vol_bar_h, UI_C_INK2);
    p->hint = ui_label_small(r, "Scroll: seek \xC2\xB7 Select: go \xC2\xB7 Menu: cancel", UI_C_INK3);
    lv_obj_set_size(p->hint, UI_W - 2 * UIM->np_margin, lv_font_get_line_height(UIM->font_small));
    lv_obj_set_style_text_align(p->hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(p->hint, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(p->hint, UIM->np_margin, UIM->np_hint_y);
    lv_obj_set_hidden(p->hint, true);

    p->prog_w = -1;
    p->vol_w = -1;
    p->track_seen = UINT32_MAX;
    p->base.shows_volume = true;
    p->base.on_tick = np_tick;
    p->base.on_scroll = np_scroll;
    p->base.on_button = np_button;
    p->base.on_button_up = np_button_up;
    p->base.suspend = np_suspend;
    p->base.resume = np_resume;
    p->base.destroy = np_destroy;
    ui_npc_init(&p->ctl);
    np_tick(&p->base);
    return &p->base;
}

/* ---- page 2 ---------------------------------------------------------------------------------------------------------------------------- */

static uint32_t g_otg_seq = 0x9000u;

typedef enum { P2_SHUFFLE, P2_REPEAT, P2_GAPLESS, P2_PLAYLIST, P2_ALBUM, P2_ARTIST, P2_ROWS } p2_row_t;

typedef struct {
    ui_screen_t base;
    ui_artbox_t *art;
    lv_obj_t *title, *line2, *stars_on, *stars_off, *stars_box;
    ui_list_t *list;
    player_info_t info;
    uint32_t key;          /* the rating key of the track shown */
    uint8_t stars;
    bool focus_stars;      /* the wheel adjusts the rating (Select or a Menu back to the rows) */
    uint32_t last_ms, track_seen;
    uint32_t album_id, artist_id; /* library ids of the playing track (LIB_NONE: not from the library / a compilation) */
    bool ids_known;
    uint32_t otg_seq;      /* an Add to On-The-Go is out (playlist_service.h) */
    bool otg_pending;
} p2_t;

static const char *rep_text(uint8_t r) { return r == PQ_REPEAT_ONE ? "One" : r == PQ_REPEAT_ALL ? "All" : "Off"; }

static void p2_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    const p2_t *p = ctx;
    switch ((p2_row_t)idx) {
    case P2_SHUFFLE:
        snprintf(out->label, sizeof out->label, "Shuffle");
        snprintf(out->detail, sizeof out->detail, "%s", player_shuffle() == PQ_SHUFFLE_SONGS ? "Songs" : "Off");
        break;
    case P2_REPEAT:
        snprintf(out->label, sizeof out->label, "Repeat");
        snprintf(out->detail, sizeof out->detail, "%s", rep_text((uint8_t)player_repeat()));
        break;
    case P2_GAPLESS:
        snprintf(out->label, sizeof out->label, "Gapless");
        snprintf(out->detail, sizeof out->detail, "%s", player_gapless_enabled() ? "On" : "Off");
        break;
    case P2_PLAYLIST:
        snprintf(out->label, sizeof out->label, "Add to On-The-Go");
        out->dim = !PLS_AVAILABLE || p->otg_pending; /* the frozen Pico 2 W has no playlists; one add at a time */
        break;
    case P2_ALBUM:
        snprintf(out->label, sizeof out->label, "Go to album");
        out->chevron = true;
        out->dim = !p->ids_known || p->album_id == LIB_NONE;
        break;
    case P2_ARTIST:
        snprintf(out->label, sizeof out->label, "Go to artist");
        out->chevron = true;
        out->dim = !p->ids_known || p->artist_id == LIB_NONE;
        break;
    case P2_ROWS: break;
    }
}

/* Note the playing track in the plays store (zero plays): a rated or added track is then known to the smart lists, which have to open its file again
 * without the library. False when it has no path (not a file the player resolved yet). */
static bool p2_note_track(const p2_t *p, char *path, size_t n) {
    if (!player_now_path(path, n)) {
        return false;
    }
    (void)plays_record(path, p->info.file_size, p->info.title_is_name ? "" : p->info.tags.title, p->info.tags.artist, false);
    return true;
}

static void p2_add_otg(p2_t *p) {
    if (!PLS_AVAILABLE || p->otg_pending) {
        ui_toast_show(PLS_AVAILABLE ? "Adding..." : "Playlists need the RP2350B board", UI_TOAST_INFO, 0);
        return;
    }
    static char path[PLAYER_PATH_MAX]; /* UI task only */
    if (!player_now_path(path, sizeof path)) {
        ui_toast_show("Can't add this track", UI_TOAST_WARN, 0);
        return;
    }
    uint32_t secs = p->info.rate != 0 ? (uint32_t)(p->info.total_frames / p->info.rate) : 0;
    p->otg_seq = ++g_otg_seq;
    p->otg_pending = pls_otg_add(path, p->info.title_is_name ? "" : p->info.tags.title, p->info.tags.artist, secs, p->otg_seq);
    if (!p->otg_pending) {
        ui_toast_show(pls_status_text(PLS_BUSY), UI_TOAST_WARN, 0);
        return;
    }
    (void)plays_record(path, p->info.file_size, p->info.title_is_name ? "" : p->info.tags.title, p->info.tags.artist, false);
    ui_list_refresh(p->list);
}

static void p2_otg_poll(p2_t *p) {
    pls_status_t st;
    if (!p->otg_pending || !pls_otg_poll(p->otg_seq, &st)) {
        return;
    }
    p->otg_pending = false;
    if (st == PLS_OK) {
        ui_toast_show("Added to On-The-Go", UI_TOAST_INFO, 0);
    } else {
        ui_toast_show(pls_status_text(st), st == PLS_DUPLICATE ? UI_TOAST_INFO : UI_TOAST_WARN, 0);
    }
    ui_list_refresh(p->list);
}

static void stars_set(p2_t *p, uint8_t stars) {
    p->stars = stars;
    char on[24] = "", off[24] = "";
    for (uint8_t i = 0; i < 5; i++) {
        strcat(i < stars ? on : off, UI_SYMBOL_STAR);
    }
    lv_label_set_text(p->stars_on, on);
    lv_label_set_text(p->stars_off, off);
    lv_obj_align_to(p->stars_off, p->stars_on, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
}

static void stars_focus(p2_t *p, bool on) {
    p->focus_stars = on;
    show(p->stars_box, on);
    ui_list_set_focus(p->list, !on);
}

static void p2_update(p2_t *p, bool force) {
    player_info_t n;
    player_get(&n);
    p->info = n;
    if (n.src != PLAYER_SRC_FILE) { /* the player stopped or went to a test tone: nothing to show a page for */
        ui_frame_pop();
        return;
    }
    bool changed = force || n.track_id != p->track_seen;
    if (changed) {
        p->track_seen = n.track_id;
        const bool untagged = n.tags.title[0] == '\0';
        set_text(p->title, untagged ? n.name : n.tags.title);
        char b[160];
        char folder[48] = "";
        if (untagged) {
            char path[160];
            if (player_now_path(path, sizeof path)) {
                folder_of(path, folder, sizeof folder);
            }
        }
        const char *artist = n.tags.artist[0] != '\0' ? n.tags.artist : folder;
        snprintf(b, sizeof b, "%s%s%s", artist, artist[0] != '\0' && n.tags.album[0] != '\0' ? " \xC2\xB7 " : "", n.tags.album);
        set_text(p->line2, b);
        p->key = track_key(&n);
        stars_set(p, rating_get(p->key));
        /* the library ids of the track (its queue handle was resolved when it started) */
        char handle[40];
        p->ids_known = player_now_handle(handle, sizeof handle) && library_track_ids(handle, &p->album_id, &p->artist_id);
        ui_list_refresh(p->list);
    }
    art_view_t v;
    art_view_get(&v);
    if (v.state == ART_VIEW_READY && v.w != 0 && (n.state == PLAYER_PLAYING || n.state == PLAYER_PAUSED || n.state == PLAYER_ENDED)) {
        ui_artbox_show(p->art, v.px, v.w, v.h, v.gen);
    } else {
        ui_artbox_show(p->art, NULL, 0, 0, v.gen);
    }
    art_view_ack(v.gen);
    if (n.queue_len > 1) {
        char b[24];
        snprintf(b, sizeof b, "%u of %u", (unsigned)n.queue_pos, (unsigned)n.queue_len);
        ui_screen_set_left(&p->base, b);
    }
}

static void p2_tick(ui_screen_t *s) {
    p2_t *p = (p2_t *)s;
    uint32_t now = os_tick_ms();
    if (now - p->last_ms < POLL_MS && p->last_ms != 0) {
        return;
    }
    p->last_ms = now ? now : 1u;
    p2_update(p, false);
    p2_otg_poll(p);
}

static void p2_scroll(ui_screen_t *s, int32_t steps) {
    p2_t *p = (p2_t *)s;
    if (p->focus_stars) { /* the wheel sets the rating: clockwise = more stars */
        int32_t v = (int32_t)p->stars + (steps > 0 ? 1 : -1);
        v = v < 0 ? 0 : v > 5 ? 5 : v;
        if ((uint8_t)v != p->stars) {
            rating_set(p->key, (uint8_t)v);
            if (v >= 4) { /* a well rated track is noted with its path, so Top rated can open it later */
                char path[PLAYER_PATH_MAX];
                (void)p2_note_track(p, path, sizeof path);
            }
            stars_set(p, (uint8_t)v);
            ui_feedback_step();
        }
        return;
    }
    if (steps < 0 && ui_list_selected(p->list) == 0) { /* up from the first row: onto the stars */
        stars_focus(p, true);
        return;
    }
    (void)ui_list_move(p->list, steps);
}

static bool p2_button(ui_screen_t *s, uint8_t btn) {
    p2_t *p = (p2_t *)s;
    if (btn == HAL_BTN_PLAY) {
        player_pause_toggle();
        return true;
    }
    if (btn == HAL_BTN_MENU && p->focus_stars) { /* Menu leaves the stars first, then the page */
        stars_focus(p, false);
        return true;
    }
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    if (p->focus_stars) {
        stars_focus(p, false);
        return true;
    }
    switch ((p2_row_t)ui_list_selected(p->list)) {
    case P2_SHUFFLE: player_set_shuffle(player_shuffle() == PQ_SHUFFLE_SONGS ? PQ_SHUFFLE_OFF : PQ_SHUFFLE_SONGS); break;
    case P2_REPEAT: player_set_repeat(player_repeat() == PQ_REPEAT_OFF ? PQ_REPEAT_ONE : player_repeat() == PQ_REPEAT_ONE ? PQ_REPEAT_ALL : PQ_REPEAT_OFF); break;
    case P2_GAPLESS: player_set_gapless(!player_gapless_enabled()); break;
    case P2_PLAYLIST: p2_add_otg(p); return true;
    case P2_ALBUM:
        if (p->ids_known && p->album_id != LIB_NONE) {
            if (!ui_frame_push(ui_screen_library_view_create(LVW_ALBUM, p->album_id, p->info.tags.album))) {
                ui_toast_show("Can't open: too many screens", UI_TOAST_WARN, 0); /* the stack is full (phase-2 review) */
            }
        } else {
            ui_toast_show("Not in the library", UI_TOAST_WARN, 0);
        }
        return true;
    case P2_ARTIST:
        if (p->ids_known && p->artist_id != LIB_NONE) {
            if (!ui_frame_push(ui_screen_library_view_create(LVW_ARTIST, p->artist_id, p->info.tags.artist))) {
                ui_toast_show("Can't open: too many screens", UI_TOAST_WARN, 0);
            }
        } else {
            ui_toast_show("Not in the library", UI_TOAST_WARN, 0);
        }
        return true;
    case P2_ROWS: break;
    }
    ui_list_refresh(p->list);
    return true;
}

static void p2_suspend(ui_screen_t *s) { ui_list_suspend(((p2_t *)s)->list); }

static void p2_resume(ui_screen_t *s) {
    p2_t *p = (p2_t *)s;
    ui_list_resume(p->list);
    p->last_ms = 0;
    p2_update(p, true);
}

static void p2_destroy(ui_screen_t *s) {
    p2_t *p = (p2_t *)s;
    ui_label_ticker(p->title, false);
    ui_label_ticker(p->line2, false);
    ui_artbox_destroy(p->art);
    ui_list_destroy(p->list);
}

ui_screen_t *ui_screen_nowplaying_page2_create(void) {
    p2_t *p = (p2_t *)ui_screen_new(sizeof *p, "Now Playing");
    if (p == NULL) {
        return NULL;
    }
    lv_obj_t *r = p->base.root;
    p->album_id = p->artist_id = LIB_NONE;
    p->art = ui_artbox_create(r, UIM->np_margin, UIM->hd_art_y, UIM->hd_art);
    int32_t w = UI_W - UIM->hd_text_x - UIM->np_margin;
    p->title = ui_label(r, "", UI_C_INK);
    lv_obj_set_size(p->title, w, UIM->line_h);
    lv_label_set_long_mode(p->title, LV_LABEL_LONG_DOT);
    ui_label_ticker(p->title, true);
    lv_obj_set_pos(p->title, UIM->hd_text_x, UIM->hd_art_y + 2);
    p->line2 = ui_label_small(r, "", UI_C_INK2);
    lv_obj_set_size(p->line2, w, UIM->np_album_h);
    lv_label_set_long_mode(p->line2, LV_LABEL_LONG_DOT);
    ui_label_ticker(p->line2, true);
    lv_obj_set_pos(p->line2, UIM->hd_text_x, UIM->hd_art_y + UIM->line_h + 4);
    /* the stars: lit ones in the accent colour, the rest in a hairline grey; a soft box behind them while the wheel adjusts them */
    int32_t sy = UIM->hd_art_y + UIM->line_h + UIM->np_album_h + 8;
    p->stars_box = lv_obj_create(r);
    lv_obj_remove_style_all(p->stars_box);
    lv_obj_set_size(p->stars_box, w, lv_font_get_line_height(UIM->font_body) + 4);
    lv_obj_set_pos(p->stars_box, UIM->hd_text_x - 4, sy - 2);
    lv_obj_set_style_radius(p->stars_box, UIM->sel_radius, 0);
    ui_obj_bg(p->stars_box, UI_C_ACCENT_SOFT, 0);
    lv_obj_set_hidden(p->stars_box, true);
    p->stars_on = ui_label(r, "", UI_C_ACCENT);
    lv_obj_set_pos(p->stars_on, UIM->hd_text_x, sy);
    p->stars_off = ui_label(r, "", UI_C_LINE);
    lv_obj_set_pos(p->stars_off, UIM->hd_text_x, sy);
    int32_t top = UIM->hd_art_y + UIM->hd_art + 10;
    p->list = ui_list_create(r, UI_W, &(ui_list_model_t){.count = P2_ROWS, .ctx = p, .get = p2_get, .top = top});
    p->base.on_tick = p2_tick;
    p->base.on_scroll = p2_scroll;
    p->base.on_button = p2_button;
    p->base.suspend = p2_suspend;
    p->base.resume = p2_resume;
    p->base.destroy = p2_destroy;
    p->track_seen = UINT32_MAX;
    stars_set(p, 0);
    p2_update(p, true);
    return &p->base;
}

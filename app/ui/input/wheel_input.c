// SPDX-License-Identifier: MPL-2.0
// Wheel input widgets, model layer. See app/include/wheel_input.h and docs/ui-input.md.
#include <string.h>

#include "wheel_input.h"

static int32_t wrap(int32_t v, int32_t n) {
    int32_t r = v % n;
    return r < 0 ? r + n : r;
}
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- UTF-8 helpers ---- */
static bool is_cont(uint8_t c) { return (c & 0xC0u) == 0x80u; }
static uint32_t cp_len(uint8_t lead) { return lead < 0x80u ? 1u : lead >= 0xF0u ? 4u : lead >= 0xE0u ? 3u : 2u; }
static uint32_t encode(uint32_t cp, char out[4]) {
    if (cp < 0x80u) { out[0] = (char)cp; return 1; }
    if (cp < 0x800u) { out[0] = (char)(0xC0u | (cp >> 6)); out[1] = (char)(0x80u | (cp & 0x3Fu)); return 2; }
    if (cp < 0x10000u) { out[0] = (char)(0xE0u | (cp >> 12)); out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu)); out[2] = (char)(0x80u | (cp & 0x3Fu)); return 3; }
    out[0] = (char)(0xF0u | (cp >> 18)); out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu)); out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

/* ---- Text ---- */

static const char *const k_pages[WI_PAGE_COUNT] = {
    "abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789", ".,-_@!?#$%&*+=/:;'\"()[]{}<>~^\\|`"};

static bool allowed(wi_charset_t cs, char c) {
    switch (cs) {
    case WI_CS_DIGITS: return c >= '0' && c <= '9';
    case WI_CS_HEX: return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    default: return true;
    }
}

static uint32_t page_count_chars(wi_charset_t cs, wi_page_t p) {
    uint32_t n = 0;
    for (const char *s = k_pages[p]; *s; s++) {
        n += allowed(cs, *s);
    }
    return n;
}
static uint32_t pages_usable(const wi_text_t *t) {
    uint32_t n = 0;
    for (int p = 0; p < WI_PAGE_COUNT; p++) {
        n += page_count_chars(t->charset, (wi_page_t)p) != 0;
    }
    return n;
}

uint32_t wi_text_chars(const wi_text_t *t) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < t->len; i++) {
        n += !is_cont((uint8_t)t->text[i]);
    }
    return n;
}

void wi_text_init(wi_text_t *t, const char *initial, uint32_t max_chars, wi_charset_t cs, bool masked, bool search) {
    memset(t, 0, sizeof *t);
    t->max_bytes = WI_TEXT_MAX;
    t->max_chars = max_chars == 0 || max_chars > WI_TEXT_MAX ? WI_TEXT_MAX : max_chars;
    t->charset = cs;
    t->masked = masked;
    t->search = search;
    t->page = cs == WI_CS_DIGITS || cs == WI_CS_HEX ? WI_PAGE_DIGITS : WI_PAGE_LOWER;
    if (initial != NULL) { /* keep the valid, printable prefix: never an invalid sequence, a control byte or half a character */
        size_t i = 0, n = 0;
        while (initial[i] != '\0' && n < WI_TEXT_MAX) {
            uint8_t c = (uint8_t)initial[i];
            uint32_t l = c < 0x80u ? 1u : (c >= 0xC2u && c < 0xE0u) ? 2u : (c >= 0xE0u && c < 0xF0u) ? 3u : (c >= 0xF0u && c < 0xF5u) ? 4u : 0u;
            if (l == 0 || (l == 1 && (c < 0x20u || c == 0x7Fu)) || n + l > WI_TEXT_MAX || n + l > t->max_bytes) {
                break;
            }
            bool ok = true;
            for (uint32_t k = 1; k < l; k++) {
                ok = ok && is_cont((uint8_t)initial[i + k]);
            }
            if (!ok) {
                break;
            }
            for (uint32_t k = 0; k < l; k++) {
                t->text[n + k] = initial[i + k];
            }
            n += l;
            i += l;
        }
        t->text[n] = '\0';
        t->len = t->cursor = (uint32_t)n;
        while (wi_text_chars(t) > t->max_chars) { /* too many characters: drop from the end, a whole character at a time */
            uint32_t e = t->len;
            do {
                e--;
            } while (e > 0 && is_cont((uint8_t)t->text[e]));
            t->text[e] = '\0';
            t->len = t->cursor = e;
        }
    }
}

void wi_text_set_byte_limit(wi_text_t *t, uint32_t max_bytes) {
    t->max_bytes = max_bytes == 0 || max_bytes > WI_TEXT_MAX ? WI_TEXT_MAX : max_bytes;
    while (t->len > t->max_bytes) {
        uint32_t e = t->len;
        do {
            e--;
        } while (e > 0 && is_cont((uint8_t)t->text[e]));
        t->text[e] = '\0';
        t->len = e;
    }
    if (t->cursor > t->len) {
        t->cursor = t->len;
    }
}

uint32_t wi_text_strip_len(const wi_text_t *t) {
    uint32_t n = page_count_chars(t->charset, t->page);
    n += pages_usable(t) > 1;
    n += t->charset == WI_CS_ANY; /* space */
    n += t->masked;
    return n + 1; /* done */
}

wi_key_kind_t wi_text_strip_key(const wi_text_t *t, uint32_t i, char out[8]) {
    out[0] = '\0';
    uint32_t nch = page_count_chars(t->charset, t->page);
    if (i < nch) {
        for (const char *s = k_pages[t->page]; *s; s++) {
            if (allowed(t->charset, *s) && i-- == 0) {
                out[0] = *s;
                out[1] = '\0';
                break;
            }
        }
        return WI_KEY_CHAR;
    }
    i -= nch;
    if (pages_usable(t) > 1) {
        if (i-- == 0) {
            static const char *const nm[WI_PAGE_COUNT] = {"ABC", "123", "#+=", "abc"};
            strcpy(out, nm[t->page]); /* names the page a Select goes to */
            return WI_KEY_PAGE;
        }
    }
    if (t->charset == WI_CS_ANY && i-- == 0) {
        strcpy(out, "space");
        return WI_KEY_SPACE;
    }
    if (t->masked && i-- == 0) {
        strcpy(out, t->show ? "hide" : "show");
        return WI_KEY_SHOW;
    }
    strcpy(out, "done");
    return WI_KEY_DONE;
}

bool wi_text_insert(wi_text_t *t, uint32_t cp) {
    char b[4];
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        return false; /* not a Unicode scalar value */
    }
    uint32_t n = encode(cp, b);
    if (cp < 0x80u && (cp < 0x20u || cp == 0x7Fu || !allowed(t->charset, (char)cp))) {
        return false;
    }
    if (cp >= 0x80u && t->charset != WI_CS_ANY) {
        return false;
    }
    if (wi_text_chars(t) >= t->max_chars || t->len + n > t->max_bytes) {
        return false;
    }
    memmove(&t->text[t->cursor + n], &t->text[t->cursor], t->len - t->cursor + 1);
    memcpy(&t->text[t->cursor], b, n);
    t->cursor += n;
    t->len += n;
    t->changed = true;
    return true;
}

static void backspace(wi_text_t *t) {
    uint32_t s = t->cursor;
    do {
        s--;
    } while (s > 0 && is_cont((uint8_t)t->text[s]));
    memmove(&t->text[s], &t->text[t->cursor], t->len - t->cursor + 1);
    t->len -= t->cursor - s;
    t->cursor = s;
    t->changed = true;
}

static void next_page(wi_text_t *t) {
    for (int k = 1; k <= WI_PAGE_COUNT; k++) {
        wi_page_t p = (wi_page_t)((t->page + (uint32_t)k) % WI_PAGE_COUNT);
        if (page_count_chars(t->charset, p) != 0) {
            t->page = p;
            break;
        }
    }
    t->sel = 0;
}

wi_result_t wi_text_event(wi_text_t *t, wi_ev_t ev, int32_t steps) {
    uint32_t n = wi_text_strip_len(t);
    switch (ev) {
    case WI_EV_SCROLL:
        t->sel = (uint32_t)wrap((int32_t)t->sel + steps, (int32_t)n);
        break;
    case WI_EV_SELECT: {
        char k[8];
        switch (wi_text_strip_key(t, t->sel, k)) {
        case WI_KEY_CHAR: (void)wi_text_insert(t, (uint8_t)k[0]); break;
        case WI_KEY_SPACE: (void)wi_text_insert(t, ' '); break;
        case WI_KEY_PAGE: next_page(t); break;
        case WI_KEY_SHOW: t->show = !t->show; break;
        case WI_KEY_DONE: return WI_OK;
        }
        break;
    }
    case WI_EV_MENU:
        if (t->cursor > 0) {
            backspace(t);
        } else if (t->len == 0) {
            return WI_CANCELLED;
        } /* cursor at the start of non-empty text: nothing before it to delete */
        break;
    case WI_EV_PLAY: return WI_OK;
    case WI_EV_LEFT:
        if (t->cursor > 0) {
            do {
                t->cursor--;
            } while (t->cursor > 0 && is_cont((uint8_t)t->text[t->cursor]));
        }
        break;
    case WI_EV_RIGHT:
        if (t->cursor < t->len) {
            t->cursor += cp_len((uint8_t)t->text[t->cursor]);
            if (t->cursor > t->len) {
                t->cursor = t->len;
            }
        }
        break;
    }
    if (t->sel >= wi_text_strip_len(t)) {
        t->sel = 0;
    }
    return WI_RUNNING;
}

/* ---- Number and slider ---- */

void wi_number_init(wi_number_t *n, int32_t value, int32_t min, int32_t max, int32_t step, uint8_t decimals, const char *unit) {
    n->min = min;
    n->max = max;
    n->step = step > 0 ? step : 1;
    n->decimals = decimals > 4 ? 4 : decimals;
    n->unit = unit != NULL ? unit : "";
    n->value = n->initial = clampi(value, min, max);
    n->changed = false;
}

static wi_result_t number_event(wi_number_t *n, wi_ev_t ev, int32_t steps) {
    int32_t d = 0;
    switch (ev) {
    case WI_EV_SCROLL: d = steps; break;
    case WI_EV_LEFT: d = -1; break;
    case WI_EV_RIGHT: d = 1; break;
    case WI_EV_SELECT:
    case WI_EV_PLAY: return WI_OK;
    case WI_EV_MENU:
        if (n->value != n->initial) {
            n->value = n->initial; /* Menu throws the edit away (and, for a slider, the preview is reverted) */
            n->changed = true;
        }
        return WI_CANCELLED;
    }
    int64_t v = (int64_t)n->value + (int64_t)d * n->step;
    int32_t nv = (int32_t)(v < n->min ? n->min : v > n->max ? n->max : v);
    if (nv != n->value) {
        n->value = nv;
        n->changed = true;
    }
    return WI_RUNNING;
}
wi_result_t wi_number_event(wi_number_t *n, wi_ev_t ev, int32_t steps) { return number_event(n, ev, steps); }

void wi_number_format(const wi_number_t *n, char *out, size_t cap) {
    int32_t scale = 1;
    for (uint8_t i = 0; i < n->decimals; i++) {
        scale *= 10;
    }
    int32_t v = n->value;
    uint32_t a = (uint32_t)(v < 0 ? -(int64_t)v : v);
    char b[24];
    size_t p = 0;
    uint32_t ip = a / (uint32_t)scale, fp = a % (uint32_t)scale;
    char tmp[12];
    int k = 0;
    do {
        tmp[k++] = (char)('0' + ip % 10u);
        ip /= 10u;
    } while (ip != 0);
    if (v < 0) {
        b[p++] = '-';
    }
    while (k > 0) {
        b[p++] = tmp[--k];
    }
    if (n->decimals > 0) {
        b[p++] = '.';
        for (int i = n->decimals - 1; i >= 0; i--) {
            uint32_t div = 1;
            for (int j = 0; j < i; j++) {
                div *= 10u;
            }
            b[p++] = (char)('0' + (fp / div) % 10u);
        }
    }
    b[p] = '\0';
    size_t un = strlen(n->unit);
    size_t need = p + (un ? un + 1 : 0) + 1;
    if (cap == 0) {
        return;
    }
    if (need > cap) {
        out[0] = '\0';
        return;
    }
    memcpy(out, b, p);
    if (un) {
        out[p] = ' ';
        memcpy(out + p + 1, n->unit, un);
    }
    out[need - 1] = '\0';
}

void wi_slider_init(wi_slider_t *s, int32_t value, int32_t min, int32_t max, int32_t step, const char *unit) {
    wi_number_init(s, value, min, max, step, 0, unit);
}
wi_result_t wi_slider_event(wi_slider_t *s, wi_ev_t ev, int32_t steps) { return number_event(s, ev, steps); }

/* ---- Picker ---- */

void wi_picker_init(wi_picker_t *p, uint32_t count, uint32_t sel, bool multi, bool wrap_) {
    p->count = count > WI_PICK_MAX ? WI_PICK_MAX : count;
    p->sel = p->count == 0 ? 0 : (sel < p->count ? sel : p->count - 1);
    p->mask = 0;
    p->multi = multi;
    p->wrap = wrap_;
}

wi_result_t wi_picker_event(wi_picker_t *p, wi_ev_t ev, int32_t steps) {
    switch (ev) {
    case WI_EV_SCROLL:
    case WI_EV_LEFT:
    case WI_EV_RIGHT: {
        if (p->count == 0) {
            break;
        }
        int32_t d = ev == WI_EV_SCROLL ? steps : ev == WI_EV_LEFT ? -1 : 1;
        int32_t v = (int32_t)p->sel + d;
        p->sel = (uint32_t)(p->wrap ? wrap(v, (int32_t)p->count) : clampi(v, 0, (int32_t)p->count - 1));
        break;
    }
    case WI_EV_SELECT:
        if (p->multi) {
            if (p->count != 0) {
                p->mask ^= 1u << p->sel;
            }
            break;
        }
        return p->count == 0 ? WI_RUNNING : WI_OK;
    case WI_EV_PLAY: return WI_OK;
    case WI_EV_MENU: return WI_CANCELLED;
    }
    return WI_RUNNING;
}

/* ---- Date and time ---- */

bool wi_is_leap(int32_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int32_t wi_days_in_month(int32_t y, int32_t mo) {
    static const int8_t d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mo < 1 || mo > 12) {
        return 31;
    }
    return mo == 2 && wi_is_leap(y) ? 29 : d[mo - 1];
}

void wi_datetime_init(wi_datetime_t *t, bool has_date, bool has_time, int32_t y, int32_t mo, int32_t d, int32_t h, int32_t mi) {
    t->has_date = has_date;
    t->has_time = has_time;
    t->y = clampi(y, 2000, 2099);
    t->mo = clampi(mo, 1, 12);
    t->d = clampi(d, 1, wi_days_in_month(t->y, t->mo));
    t->h = clampi(h, 0, 23);
    t->mi = clampi(mi, 0, 59);
    t->field = has_date ? 0 : 3;
}

static bool field_present(const wi_datetime_t *t, uint8_t f) { return f < 3 ? t->has_date : t->has_time; }

static void dt_step(wi_datetime_t *t, int32_t d) {
    switch (t->field) {
    case 0: t->d = 1 + wrap(t->d - 1 + d, wi_days_in_month(t->y, t->mo)); break;
    case 1: t->mo = 1 + wrap(t->mo - 1 + d, 12); break;
    case 2: t->y = clampi(t->y + d, 2000, 2099); break;
    case 3: t->h = wrap(t->h + d, 24); break;
    default: t->mi = wrap(t->mi + d, 60); break;
    }
    t->d = clampi(t->d, 1, wi_days_in_month(t->y, t->mo)); /* month or year moved under the day (31 -> 30, 29 Feb) */
}

static bool dt_move(wi_datetime_t *t, int dir) {
    for (int k = 1; k <= 5; k++) {
        int f = (int)t->field + dir * k;
        if (f < 0 || f > 4) {
            return false;
        }
        if (field_present(t, (uint8_t)f)) {
            t->field = (uint8_t)f;
            return true;
        }
    }
    return false;
}

wi_result_t wi_datetime_event(wi_datetime_t *t, wi_ev_t ev, int32_t steps) {
    switch (ev) {
    case WI_EV_SCROLL: dt_step(t, steps); break;
    case WI_EV_LEFT: (void)dt_move(t, -1); break;
    case WI_EV_RIGHT: (void)dt_move(t, 1); break;
    case WI_EV_SELECT: return dt_move(t, 1) ? WI_RUNNING : WI_OK; /* next field; on the last one it confirms */
    case WI_EV_PLAY: return WI_OK;
    case WI_EV_MENU: return WI_CANCELLED;
    }
    return WI_RUNNING;
}

/* ---- Letter jump ---- */

static char norm(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : (c >= 'A' && c <= 'Z') ? c : '#'; }

void wi_letterjump_init(wi_letterjump_t *j) { memset(j, 0, sizeof *j); j->letter = '#'; }

bool wi_letterjump_scroll(wi_letterjump_t *j, int32_t steps, uint32_t now_ms, char initial) {
    if (now_ms - j->last_ms > WI_LJ_WINDOW_MS) {
        j->energy = 0;
    }
    j->last_ms = now_ms;
    j->energy += (uint32_t)(steps < 0 ? -steps : steps);
    j->letter = norm(initial);
    if (j->energy >= WI_LJ_FAST_STEPS) {
        j->active = true;
    }
    return j->active;
}

void wi_letterjump_tick(wi_letterjump_t *j, uint32_t now_ms) {
    if (j->active && now_ms - j->last_ms > WI_LJ_HIDE_MS) {
        j->active = false;
        j->energy = 0;
    }
}

int32_t wi_letter_first_index(int32_t count, char (*initial_of)(void *, int32_t), void *ctx, char letter) {
    char want = norm(letter);
    int32_t lo = 0, hi = count;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        char c = norm(initial_of(ctx, mid));
        bool less = c == '#' ? want != '#' : (want != '#' && c < want);
        if (less) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < count ? lo : (count > 0 ? count - 1 : 0);
}

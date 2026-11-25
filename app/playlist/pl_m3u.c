// SPDX-License-Identifier: MPL-2.0
#include "pl_m3u.h"

#include <stdio.h>
#include <string.h>

const char pl_m3u_header[] = "#EXTM3U\n";

static bool valid_utf8(const char *s) {
    const uint8_t *p = (const uint8_t *)s;
    while (*p != 0) {
        uint8_t c = *p++;
        int more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 && c >= 0xC2 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 && c <= 0xF4 ? 3 : -1;
        if (more < 0) {
            return false;
        }
        while (more-- > 0) {
            if ((*p & 0xC0) != 0x80) {
                return false;
            }
            p++;
        }
    }
    return true;
}

// Latin-1 -> UTF-8 into @p out; false when it does not fit.
static bool latin1_to_utf8(const char *in, char *out, size_t cap) {
    size_t w = 0;
    for (const uint8_t *p = (const uint8_t *)in; *p != 0; p++) {
        size_t need = *p < 0x80 ? 1u : 2u;
        if (w + need + 1u > cap) {
            return false;
        }
        if (*p < 0x80) {
            out[w++] = (char)*p;
        } else {
            out[w++] = (char)(0xC0 | (*p >> 6));
            out[w++] = (char)(0x80 | (*p & 0x3F));
        }
    }
    out[w] = '\0';
    return true;
}

bool pl_is_playlist_name(const char *name) {
    size_t n = strlen(name);
    if (n >= 5 && name[n - 1] == '8') {
        n--; // .m3u8
    }
    return n >= 5 && name[n - 4] == '.' && (name[n - 3] | 32) == 'm' && name[n - 2] == '3' && (name[n - 1] | 32) == 'u';
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

// Not reentrant (static scratch): the parser runs on the storage task, one list at a time.
bool pl_resolve(const char *dir, const char *raw, char *out, size_t cap) {
    static char tmp[PL_LINE_MAX]; /* static: the buffers are big and the storage task's stack is not; one parse at a time (storage task) */
    size_t w = 0;
    // file:// URL: drop the scheme (and an empty / "localhost" host), decode %XX
    bool url = false;
    if (strncmp(raw, "file://", 7) == 0) {
        raw += 7;
        if (strncmp(raw, "localhost/", 10) == 0) {
            raw += 9;
        }
        url = true;
    } else if (strstr(raw, "://") != NULL) {
        return false; // http:, rtsp: ... not a file on the card
    }
    for (const char *s = raw; *s != '\0'; s++) {
        char c = *s;
        if (url && c == '%' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) {
            c = (char)(hexv(s[1]) * 16 + hexv(s[2]));
            s += 2;
        }
        if (c == '\\') {
            c = '/';
        }
        if ((unsigned char)c < 0x20) {
            return false;
        }
        if (w + 1u >= sizeof tmp) {
            return false;
        }
        tmp[w++] = c;
    }
    tmp[w] = '\0';
    const char *rel = tmp;
    // a drive letter ("C:/Music/a.flac", also "C:a.flac" is not a path we can read): the rest is read from the root
    if (((tmp[0] | 32) >= 'a' && (tmp[0] | 32) <= 'z') && tmp[1] == ':') {
        rel = tmp + 2;
        if (rel[0] != '/') {
            return false;
        }
    }
    if (url && rel[0] != '/') {
        return false;
    }
    static char full[PL_LINE_MAX + PL_PATH_MAX];
    if (rel[0] == '/') {
        snprintf(full, sizeof full, "%s", rel);
    } else {
        snprintf(full, sizeof full, "%s/%s", dir, rel);
    }
    // normalise: collapse separators, drop ".", resolve ".."
    char *comp[64];
    static char buf[sizeof full];
    memcpy(buf, full, strlen(full) + 1u);
    size_t depth = 0;
    for (char *p = buf; *p != '\0';) {
        while (*p == '/') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        char *start = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }
        if (*p == '/') {
            *p++ = '\0';
        }
        if (strcmp(start, ".") == 0) {
            continue;
        }
        if (strcmp(start, "..") == 0) {
            if (depth == 0) {
                return false; // climbs above the root
            }
            depth--;
            continue;
        }
        if (depth >= 64) {
            return false;
        }
        comp[depth++] = start;
    }
    if (depth == 0) {
        return false; // the root is not a file
    }
    size_t o = 0;
    for (size_t i = 0; i < depth; i++) {
        size_t l = strlen(comp[i]);
        if (o + 1u + l + 1u > cap) {
            return false;
        }
        out[o++] = '/';
        memcpy(out + o, comp[i], l);
        o += l;
    }
    out[o] = '\0';
    return true;
}

static void copy_cut(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void pl_m3u_init(pl_m3u_t *p, const char *dir) {
    memset(p, 0, sizeof *p);
    copy_cut(p->dir, sizeof p->dir, dir != NULL && dir[0] != '\0' ? dir : "/");
}

// "#EXTINF:123,Artist - Title"
static void take_extinf(pl_m3u_t *p, const char *rest) {
    memset(&p->inf, 0, sizeof p->inf);
    uint32_t secs = 0;
    const char *s = rest;
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        if (secs < 10000000u) {
            secs = secs * 10u + (uint32_t)(*s - '0');
        }
        s++;
    }
    while (*s != '\0' && *s != ',') {
        s++; // attributes of the extended form ("123 tvg-id=...") are not read
    }
    p->inf.secs = neg ? 0 : secs;
    if (*s == ',') {
        s++;
    }
    while (*s == ' ') {
        s++;
    }
    const char *sep = strstr(s, " - ");
    if (sep != NULL && sep != s && sep[3] != '\0') {
        static char a[PL_LINE_MAX];
        size_t al = (size_t)(sep - s);
        if (al >= sizeof a) {
            al = sizeof a - 1u;
        }
        memcpy(a, s, al);
        a[al] = '\0';
        copy_cut(p->inf.artist, sizeof p->inf.artist, a);
        copy_cut(p->inf.title, sizeof p->inf.title, sep + 3);
    } else {
        copy_cut(p->inf.title, sizeof p->inf.title, s);
    }
    p->have_inf = true;
}

static void line_done(pl_m3u_t *p, pl_entry_fn fn, void *user) {
    char *l = p->line;
    l[p->n] = '\0';
    bool over = p->over;
    p->n = 0;
    p->over = false;
    while (*l == ' ' || *l == '\t') {
        l++;
    }
    size_t len = strlen(l);
    while (len > 0 && (l[len - 1] == ' ' || l[len - 1] == '\t')) {
        l[--len] = '\0';
    }
    if (len == 0 && !over) {
        return;
    }
    static char u[PL_LINE_MAX * 2];
    if (!valid_utf8(l)) {
        if (!latin1_to_utf8(l, u, sizeof u)) {
            u[0] = '\0';
        }
        l = u;
    }
    if (l[0] == '#') {
        if (!over && strncmp(l, "#EXTINF:", 8) == 0) {
            take_extinf(p, l + 8);
        }
        return; // #EXTM3U, #EXTALB, #PLAYLIST ... are comments here
    }
    pl_entry_t e;
    memset(&e, 0, sizeof e);
    if (p->have_inf) {
        e = p->inf;
        e.path[0] = '\0';
    }
    p->have_inf = false;
    memset(&p->inf, 0, sizeof p->inf);
    e.resolvable = !over && pl_resolve(p->dir, l, e.path, sizeof e.path);
    if (!e.resolvable) {
        e.path[0] = '\0';
        if (e.title[0] == '\0') {
            copy_cut(e.title, sizeof e.title, l); // what the line said: the greyed row shows it
        }
    }
    p->entries++;
    fn(user, &e);
}

void pl_m3u_feed(pl_m3u_t *p, const uint8_t *data, size_t n, pl_entry_fn fn, void *user) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = data[i];
        if (!p->started) {
            p->started = true;
            if (c == 0xEF && n - i >= 3 && data[i + 1] == 0xBB && data[i + 2] == 0xBF) {
                i += 2; // UTF-8 byte-order mark
                continue;
            }
        }
        if (c == '\n' || c == '\r') {
            line_done(p, fn, user); // CRLF: the LF ends an empty line, which is skipped
            continue;
        }
        if (c == 0) {
            continue;
        }
        if (p->n + 1u < sizeof p->line) {
            p->line[p->n++] = (char)c;
        } else {
            p->over = true;
        }
    }
}

void pl_m3u_end(pl_m3u_t *p, pl_entry_fn fn, void *user) { line_done(p, fn, user); }

size_t pl_format_entry(char *out, size_t n, const char *path, const char *title, const char *artist, uint32_t secs) {
    // a title or artist is one line: line ends inside it would split the entry
    char t[PL_TITLE_MAX], a[PL_ARTIST_MAX];
    copy_cut(t, sizeof t, title != NULL ? title : "");
    copy_cut(a, sizeof a, artist != NULL ? artist : "");
    for (char *s = t; *s; s++) {
        if (*s == '\n' || *s == '\r') {
            *s = ' ';
        }
    }
    for (char *s = a; *s; s++) {
        if (*s == '\n' || *s == '\r') {
            *s = ' ';
        }
    }
    int w;
    if (a[0] != '\0' && t[0] != '\0') {
        w = snprintf(out, n, "#EXTINF:%u,%s - %s\n%s\n", (unsigned)secs, a, t, path);
    } else {
        w = snprintf(out, n, "#EXTINF:%u,%s\n%s\n", (unsigned)secs, t[0] != '\0' ? t : a, path);
    }
    return w > 0 && (size_t)w < n ? (size_t)w : 0;
}

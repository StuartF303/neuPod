// SPDX-License-Identifier: MPL-2.0
#include "gfx.h"

void gfx_canvas_init(gfx_canvas_t *c, hal_pixel_t *buf, int16_t x0, int16_t y0, uint16_t w,
                     uint16_t h) {
    c->buf = buf;
    c->x0 = x0;
    c->y0 = y0;
    c->w = w;
    c->h = h;
}

void gfx_clear(gfx_canvas_t *c, hal_pixel_t colour) {
    size_t n = (size_t)c->w * c->h;
    for (size_t i = 0; i < n; i++) {
        c->buf[i] = colour;
    }
}

void gfx_pixel(gfx_canvas_t *c, int x, int y, hal_pixel_t colour) {
    int lx = x - c->x0;
    int ly = y - c->y0;
    if (lx < 0 || ly < 0 || lx >= (int)c->w || ly >= (int)c->h) {
        return;
    }
    c->buf[(size_t)ly * c->w + (size_t)lx] = colour;
}

void gfx_hline(gfx_canvas_t *c, int x, int y, int len, hal_pixel_t colour) {
    int ly = y - c->y0;
    if (len <= 0 || ly < 0 || ly >= (int)c->h) {
        return;
    }
    int a = x - c->x0;
    int b = a + len; // exclusive
    if (a < 0) {
        a = 0;
    }
    if (b > (int)c->w) {
        b = (int)c->w;
    }
    hal_pixel_t *row = c->buf + (size_t)ly * c->w;
    for (int i = a; i < b; i++) {
        row[i] = colour;
    }
}

void gfx_vline(gfx_canvas_t *c, int x, int y, int len, hal_pixel_t colour) {
    int lx = x - c->x0;
    if (len <= 0 || lx < 0 || lx >= (int)c->w) {
        return;
    }
    int a = y - c->y0;
    int b = a + len;
    if (a < 0) {
        a = 0;
    }
    if (b > (int)c->h) {
        b = (int)c->h;
    }
    for (int i = a; i < b; i++) {
        c->buf[(size_t)i * c->w + (size_t)lx] = colour;
    }
}

static int iabs(int v) { return v < 0 ? -v : v; }

void gfx_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1, hal_pixel_t colour) {
    int dx = iabs(x1 - x0);
    int dy = -iabs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(c, x0, y0, colour);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_fill_rect(gfx_canvas_t *c, int x, int y, int w, int h, hal_pixel_t colour) {
    if (w <= 0 || h <= 0) {
        return;
    }
    int a = y - c->y0;
    int b = a + h;
    if (a < 0) {
        a = 0;
    }
    if (b > (int)c->h) {
        b = (int)c->h;
    }
    for (int row = a; row < b; row++) {
        gfx_hline(c, x, row + c->y0, w, colour);
    }
}

void gfx_rect(gfx_canvas_t *c, int x, int y, int w, int h, hal_pixel_t colour) {
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_hline(c, x, y, w, colour);
    gfx_hline(c, x, y + h - 1, w, colour);
    gfx_vline(c, x, y, h, colour);
    gfx_vline(c, x + w - 1, y, h, colour);
}

void gfx_circle(gfx_canvas_t *c, int cx, int cy, int r, hal_pixel_t colour) {
    if (r < 0) {
        return;
    }
    int x = r;
    int y = 0;
    int err = 1 - r;
    while (x >= y) {
        gfx_pixel(c, cx + x, cy + y, colour);
        gfx_pixel(c, cx - x, cy + y, colour);
        gfx_pixel(c, cx + x, cy - y, colour);
        gfx_pixel(c, cx - x, cy - y, colour);
        gfx_pixel(c, cx + y, cy + x, colour);
        gfx_pixel(c, cx - y, cy + x, colour);
        gfx_pixel(c, cx + y, cy - x, colour);
        gfx_pixel(c, cx - y, cy - x, colour);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_fill_circle(gfx_canvas_t *c, int cx, int cy, int r, hal_pixel_t colour) {
    if (r < 0) {
        return;
    }
    // Only visit rows that intersect the canvas (strip rendering calls this per strip).
    int ya = cy - r > c->y0 ? cy - r : c->y0;
    int yb = cy + r < c->y0 + (int)c->h - 1 ? cy + r : c->y0 + (int)c->h - 1;
    for (int y = ya; y <= yb; y++) {
        int dy = y - cy;
        int half = 0;
        while ((half + 1) * (half + 1) + dy * dy <= r * r + r) { // r^2 + r ~ rounds the edge
            half++;
        }
        gfx_hline(c, cx - half, y, 2 * half + 1, colour);
    }
}

void gfx_image(gfx_canvas_t *c, int x, int y, const hal_pixel_t *src, int w, int h) {
    if (src == NULL || w <= 0 || h <= 0) {
        return;
    }
    int ra = y - c->y0 < 0 ? -(y - c->y0) : 0; // first source row that lands in the canvas
    int rb = h;
    if (y + h > c->y0 + (int)c->h) {
        rb = c->y0 + (int)c->h - y;
    }
    int ca = x - c->x0 < 0 ? -(x - c->x0) : 0;
    int cb = w;
    if (x + w > c->x0 + (int)c->w) {
        cb = c->x0 + (int)c->w - x;
    }
    if (ca >= cb) {
        return;
    }
    for (int r = ra; r < rb; r++) {
        hal_pixel_t *dst = c->buf + (size_t)(y + r - c->y0) * c->w + (size_t)(x + ca - c->x0);
        const hal_pixel_t *s = src + (size_t)r * (size_t)w + (size_t)ca;
        for (int i = 0; i < cb - ca; i++) {
            dst[i] = s[i];
        }
    }
}

// --- text ----------------------------------------------------------------------------------

static unsigned glyph_index(const gfx_font_t *f, char ch) {
    unsigned u = (unsigned char)ch;
    if (u < f->first || u > f->last) {
        u = '?';
    }
    return u - f->first;
}

static int glyph_advance(const gfx_font_t *f, unsigned idx) {
    return f->widths != NULL ? f->widths[idx] : f->fixed_w;
}

int gfx_text_width(const gfx_font_t *f, const char *s) {
    int w = 0;
    for (; *s != '\0'; s++) {
        w += glyph_advance(f, glyph_index(f, *s));
    }
    return w;
}

static void draw_text(gfx_canvas_t *c, const gfx_font_t *f, int x, int y, const char *s,
                      hal_pixel_t fg, bool opaque, hal_pixel_t bg) {
    if (y + f->height <= c->y0 || y >= c->y0 + (int)c->h) {
        return; // whole line outside this canvas
    }
    for (; *s != '\0'; s++) {
        unsigned idx = glyph_index(f, *s);
        int adv = glyph_advance(f, idx);
        if (x < c->x0 + (int)c->w && x + adv > c->x0) {
            const uint16_t *rows = f->rows + (size_t)idx * f->height;
            for (int r = 0; r < f->height; r++) {
                int py = y + r;
                if (py < c->y0 || py >= c->y0 + (int)c->h) {
                    continue;
                }
                for (int b = 0; b < adv; b++) {
                    if (rows[r] & (0x8000u >> b)) {
                        gfx_pixel(c, x + b, py, fg);
                    } else if (opaque) {
                        gfx_pixel(c, x + b, py, bg);
                    }
                }
            }
        }
        x += adv;
    }
}

void gfx_text(gfx_canvas_t *c, const gfx_font_t *f, int x, int y, const char *s, hal_pixel_t fg) {
    draw_text(c, f, x, y, s, fg, false, 0);
}

void gfx_text_bg(gfx_canvas_t *c, const gfx_font_t *f, int x, int y, const char *s, hal_pixel_t fg,
                 hal_pixel_t bg) {
    draw_text(c, f, x, y, s, fg, true, bg);
}

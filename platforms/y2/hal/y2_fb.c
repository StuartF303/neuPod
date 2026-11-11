// SPDX-License-Identifier: MPL-2.0
#include "y2_fb.h"

uint32_t y2_fb_expand565(uint16_t p) {
    uint32_t r = (p >> 11) & 0x1Fu, g = (p >> 5) & 0x3Fu, b = p & 0x1Fu;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void y2_fb_convert_rgb565(const uint16_t *src, uint32_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        dst[i] = y2_fb_expand565(src[i]);
    }
}

static bool field_is(y2_fb_field_t f, uint32_t offset, uint32_t length) { return f.offset == offset && f.length == length; }

bool y2_fb_mode_is_xrgb8888(uint32_t bits_per_pixel, y2_fb_field_t red, y2_fb_field_t green, y2_fb_field_t blue,
                            y2_fb_field_t transp) {
    return bits_per_pixel == 32 && field_is(red, 16, 8) && field_is(green, 8, 8) && field_is(blue, 0, 8) && field_is(transp, 24, 8);
}

unsigned y2_fb_page_count(size_t map_len, uint32_t yres_virtual, uint32_t line_length, uint32_t yres) {
    uint64_t page_bytes = (uint64_t)line_length * yres;
    if (page_bytes == 0) {
        return 1;
    }
    uint64_t usable = (uint64_t)yres_virtual * line_length;
    if (map_len < usable) {
        usable = map_len;
    }
    uint64_t n = usable / page_bytes;
    if (n < 1) {
        n = 1;
    }
    return n > Y2_FB_MAX_PAGES ? Y2_FB_MAX_PAGES : (unsigned)n;
}

unsigned y2_fb_next_page(unsigned cur, unsigned pages) { return pages <= 1 ? 0 : (cur + 1) % pages; }

static void rows_union(y2_rows_t *r, uint16_t y0, uint16_t y1) {
    if (y0 >= y1) {
        return;
    }
    if (r->lo >= r->hi) {
        r->lo = y0;
        r->hi = y1;
        return;
    }
    if (y0 < r->lo) {
        r->lo = y0;
    }
    if (y1 > r->hi) {
        r->hi = y1;
    }
}

void y2_pager_init(y2_pager_t *p, unsigned pages, unsigned visible, uint16_t height) {
    if (pages < 1) {
        pages = 1;
    }
    if (pages > Y2_FB_MAX_PAGES) {
        pages = Y2_FB_MAX_PAGES;
    }
    p->pages = pages;
    p->visible = visible < pages ? visible : 0;
    p->height = height;
    for (unsigned i = 0; i < Y2_FB_MAX_PAGES; i++) {
        p->stale[i] = (y2_rows_t){0, height};
    }
}

void y2_pager_mark(y2_pager_t *p, uint16_t y0, uint16_t y1) {
    if (y1 > p->height) {
        y1 = p->height;
    }
    for (unsigned i = 0; i < p->pages; i++) {
        rows_union(&p->stale[i], y0, y1);
    }
}

void y2_pager_mark_all(y2_pager_t *p) { y2_pager_mark(p, 0, p->height); }

bool y2_pager_next(y2_pager_t *p, unsigned *page, y2_rows_t *rows) {
    unsigned n = y2_fb_next_page(p->visible, p->pages);
    if (p->stale[n].lo >= p->stale[n].hi) {
        return false;
    }
    *page = n;
    *rows = p->stale[n];
    p->stale[n] = (y2_rows_t){0, 0};
    p->visible = n;
    return true;
}

/* fbtool: minimal framebuffer test tool (no fonts).
 *   fbtool info | bars | fill RRGGBB | status N | image FILE | check FILE [OUT.ppm]
 * Handles any bpp >= 16 via the reported channel offsets/lengths and the
 * line stride. Draws into every virtual page, then pans to page 0.
 *
 * image: FILE is a Y2I1 splash (see tools/y2splash.py): "Y2I1", u16le width, u16le height,
 * u16le palette size N (1..256), N RGB triples, then (run u8 1..255, index u8 < N) pairs that
 * cover exactly width*height pixels in raster order, and nothing after. The whole file is
 * validated and decoded BEFORE the framebuffer is opened: a bad file draws nothing (exit 3).
 * An image larger than the panel is clipped, a smaller one is centred on black.
 * check: validate only (no framebuffer); with OUT.ppm also writes the decoded image.
 *
 * Test hook: FBTOOL_FAKE_FB=PATH uses a regular file as a 32 bpp XRGB8888 framebuffer of
 * FBTOOL_FAKE_SIZE=WxH (default 480x360) with three virtual pages, so the drawing code runs on
 * a host without a panel. Unset on the device. */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static struct fb_var_screeninfo v;
static struct fb_fix_screeninfo f;
static uint8_t *fb;
static unsigned bpp_bytes;

static uint32_t chan(uint8_t c, const struct fb_bitfield *b)
{
	return b->length ? (uint32_t)(c >> (8 - (b->length > 8 ? 8 : b->length))) << b->offset : 0;
}

static uint32_t rgb(uint32_t c)
{
	uint32_t p = chan(c >> 16, &v.red) | chan(c >> 8, &v.green) | chan(c, &v.blue);
	if (v.transp.length)
		p |= (((1u << v.transp.length) - 1) << v.transp.offset);
	return p;
}

static void put(uint8_t *row, unsigned x, uint32_t px)
{
	memcpy(row + (size_t)x * bpp_bytes, &px, bpp_bytes); /* little-endian ARM */
}

/* Fill rectangle [x0,x1) x [y0,y1) in every page. */
static void rect(unsigned x0, unsigned y0, unsigned x1, unsigned y1, uint32_t c)
{
	uint32_t px = rgb(c);
	unsigned pages = v.yres_virtual / v.yres ? v.yres_virtual / v.yres : 1;
	if (x1 > v.xres) x1 = v.xres;
	if (y1 > v.yres) y1 = v.yres;
	for (unsigned p = 0; p < pages; p++)
		for (unsigned y = y0; y < y1; y++) {
			uint8_t *row = fb + (size_t)(p * v.yres + y) * f.line_length;
			for (unsigned x = x0; x < x1; x++)
				put(row, x, px);
		}
}

static int fd = -1;

static int no_put;

static int fake;

static void flip(void)
{
	if (fake)
		return;
	v.xoffset = v.yoffset = 0;
	ioctl(fd, FBIOPAN_DISPLAY, &v);
	if (!no_put) { /* mtkfb: some builds only refresh on PUT_VSCREENINFO */
		v.activate = FB_ACTIVATE_NOW;
		ioctl(fd, FBIOPUT_VSCREENINFO, &v);
	}
}

/* ---- Y2I1 splash image ---------------------------------------------------------------- */
#define IMG_MAGIC "Y2I1"
#define IMG_MAX_FILE (1u << 20)
#define IMG_MAX_DIM 4096u

struct img {
	unsigned w, h, npal;
	uint32_t pal[256]; /* 0xRRGGBB */
	uint8_t *idx;      /* w*h palette indices */
};

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

/* Returns NULL on success or a static message. Nothing is drawn here. */
static const char *img_decode(const uint8_t *b, size_t n, struct img *im)
{
	size_t pos = 10, px = 0, total;
	if (n < 10 || memcmp(b, IMG_MAGIC, 4))
		return "bad magic";
	im->w = le16(b + 4);
	im->h = le16(b + 6);
	im->npal = le16(b + 8);
	if (!im->w || !im->h || im->w > IMG_MAX_DIM || im->h > IMG_MAX_DIM)
		return "bad dimensions";
	if (!im->npal || im->npal > 256)
		return "bad palette size";
	if (n < pos + (size_t)im->npal * 3)
		return "truncated palette";
	for (unsigned i = 0; i < im->npal; i++, pos += 3)
		im->pal[i] = (uint32_t)b[pos] << 16 | (uint32_t)b[pos + 1] << 8 | b[pos + 2];
	total = (size_t)im->w * im->h;
	im->idx = malloc(total);
	if (!im->idx)
		return "out of memory";
	while (px < total) {
		if (n - pos < 2) { free(im->idx); im->idx = NULL; return "truncated pixel data"; }
		unsigned run = b[pos], ix = b[pos + 1];
		pos += 2;
		if (!run || ix >= im->npal || run > total - px) {
			free(im->idx);
			im->idx = NULL;
			return !run ? "zero-length run" : ix >= im->npal ? "palette index out of range" : "pixel data overruns image";
		}
		memset(im->idx + px, (int)ix, run);
		px += run;
	}
	if (pos != n) { free(im->idx); im->idx = NULL; return "trailing bytes"; }
	return NULL;
}

static const char *img_load(const char *path, struct img *im)
{
	FILE *fp = fopen(path, "rb");
	uint8_t *buf;
	size_t n;
	const char *err;
	if (!fp)
		return "cannot open image";
	buf = malloc(IMG_MAX_FILE + 1);
	if (!buf) { fclose(fp); return "out of memory"; }
	n = fread(buf, 1, IMG_MAX_FILE + 1, fp);
	fclose(fp);
	err = n > IMG_MAX_FILE ? "file too large" : img_decode(buf, n, im);
	free(buf);
	return err;
}

/* Draw into every page: black backdrop, image centred (smaller) or centre-clipped (larger). */
static void draw_image(const struct img *im)
{
	uint32_t pxpal[256];
	unsigned pages = v.yres_virtual / v.yres ? v.yres_virtual / v.yres : 1;
	unsigned cw = im->w < v.xres ? im->w : v.xres, ch = im->h < v.yres ? im->h : v.yres;
	unsigned sx = im->w > v.xres ? (im->w - v.xres) / 2 : 0, sy = im->h > v.yres ? (im->h - v.yres) / 2 : 0;
	unsigned dx = v.xres > im->w ? (v.xres - im->w) / 2 : 0, dy = v.yres > im->h ? (v.yres - im->h) / 2 : 0;
	for (unsigned i = 0; i < im->npal; i++)
		pxpal[i] = rgb(im->pal[i]);
	if (cw != v.xres || ch != v.yres)
		rect(0, 0, v.xres, v.yres, 0x000000);
	for (unsigned p = 0; p < pages; p++)
		for (unsigned y = 0; y < ch; y++) {
			uint8_t *row = fb + (size_t)(p * v.yres + dy + y) * f.line_length;
			const uint8_t *src = im->idx + (size_t)(sy + y) * im->w + sx;
			for (unsigned x = 0; x < cw; x++)
				put(row, dx + x, pxpal[src[x]]);
		}
}

static int write_ppm(const char *path, const struct img *im)
{
	FILE *fp = fopen(path, "wb");
	if (!fp)
		return -1;
	fprintf(fp, "P6\n%u %u\n255\n", im->w, im->h);
	for (size_t i = 0; i < (size_t)im->w * im->h; i++) {
		uint32_t c = im->pal[im->idx[i]];
		fputc((int)(c >> 16 & 0xff), fp);
		fputc((int)(c >> 8 & 0xff), fp);
		fputc((int)(c & 0xff), fp);
	}
	return fclose(fp);
}

/* ---- boot progress dots ---------------------------------------------------------------- */
#define DOTS 5 /* stages 1..5 of the rescue init */
#define DOT_R 5
#define DOT_PITCH 22
#define DOT_BOTTOM 16 /* centre of the dots, from the bottom edge */

static void dot(unsigned cx, unsigned cy, unsigned r, uint32_t c)
{
	for (int dy = -(int)r; dy <= (int)r; dy++) {
		int hw = 0;
		while ((hw + 1) * (hw + 1) + dy * dy <= (int)(r * r + r))
			hw++;
		rect(cx - hw, cy + dy, cx + hw + 1, cy + dy + 1, c);
	}
}

/* N lit green dots out of DOTS (stage reached), the rest dim, each with a dark ring so they
 * read over any splash colour. Only the dot pixels change: the splash behind stays. */
static void status_dots(unsigned n)
{
	unsigned span = (DOTS - 1) * DOT_PITCH, x0 = v.xres > span ? (v.xres - span) / 2 : DOT_R + 1;
	unsigned cy = v.yres > DOT_BOTTOM + DOT_R + 1 ? v.yres - DOT_BOTTOM : v.yres / 2;
	for (unsigned i = 0; i < DOTS; i++) {
		dot(x0 + i * DOT_PITCH, cy, DOT_R + 1, 0x05080f);
		dot(x0 + i * DOT_PITCH, cy, DOT_R - 1, i < n ? 0x00ff00 : 0x39475e);
	}
}

static void info(void)
{
	printf("id=%s smem_len=%u line_length=%u type=%u visual=%u\n", f.id, f.smem_len,
	       f.line_length, f.type, f.visual);
	printf("res=%ux%u virt=%ux%u off=%u,%u bpp=%u\n", v.xres, v.yres, v.xres_virtual,
	       v.yres_virtual, v.xoffset, v.yoffset, v.bits_per_pixel);
	printf("red=%u/%u green=%u/%u blue=%u/%u transp=%u/%u (offset/length)\n", v.red.offset,
	       v.red.length, v.green.offset, v.green.length, v.blue.offset, v.blue.length,
	       v.transp.offset, v.transp.length);
}

int main(int argc, char **argv)
{
	static const char *const paths[] = { "/dev/fb0", "/dev/graphics/fb0" };
	if (argc > 1 && !strcmp(argv[1], "--no-put")) { /* skip FBIOPUT_VSCREENINFO */
		no_put = 1;
		argc--;
		argv++;
	}
	if (getenv("FBTOOL_NO_PUT"))
		no_put = 1;
	const char *cmd = argc > 1 ? argv[1] : "";
	struct img im = { 0 };
	const char *fakepath = getenv("FBTOOL_FAKE_FB");

	if ((!strcmp(cmd, "image") || !strcmp(cmd, "check")) && argc > 2) { /* validate before touching the panel */
		const char *err = img_load(argv[2], &im);
		if (err) { fprintf(stderr, "fbtool: %s: %s\n", argv[2], err); return 3; }
		if (!strcmp(cmd, "check")) {
			if (argc > 3 && write_ppm(argv[3], &im)) { perror("write ppm"); return 1; }
			printf("ok %ux%u %u colours\n", im.w, im.h, im.npal);
			return 0;
		}
	}

	if (fakepath) {
		unsigned w = 480, h = 360;
		const char *sz = getenv("FBTOOL_FAKE_SIZE");
		if (sz && sscanf(sz, "%ux%u", &w, &h) != 2) { fprintf(stderr, "bad FBTOOL_FAKE_SIZE\n"); return 1; }
		fd = open(fakepath, O_RDWR);
		if (fd < 0) { perror("open fake fb"); return 1; }
		fake = 1;
		v.xres = v.xres_virtual = w;
		v.yres = h;
		v.yres_virtual = 3 * h;
		v.bits_per_pixel = 32;
		v.red.offset = 16; v.red.length = 8;
		v.green.offset = 8; v.green.length = 8;
		v.blue.offset = 0; v.blue.length = 8;
		f.line_length = 4 * w;
		f.smem_len = f.line_length * v.yres_virtual;
		strcpy((char *)f.id, "fake");
	} else {
		for (unsigned i = 0; i < 2 && fd < 0; i++)
			fd = open(paths[i], O_RDWR);
		if (fd < 0) { perror("open fb0"); return 1; }
		if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) {
			perror("FBIOGET_*SCREENINFO");
			return 1;
		}
	}
	if (!strcmp(cmd, "info")) { info(); return 0; }
	if (v.bits_per_pixel < 16 || v.bits_per_pixel > 32 || !v.xres || !v.yres || !f.line_length) {
		fprintf(stderr, "unsupported mode\n");
		return 1;
	}
	bpp_bytes = v.bits_per_pixel / 8;
	fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) { perror("mmap"); return 1; }
	if (v.yres_virtual * (size_t)f.line_length > f.smem_len)
		v.yres_virtual = f.smem_len / f.line_length; /* never draw past the mapping */

	if (!strcmp(cmd, "bars")) {
		static const uint32_t col[8] = { 0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
						 0xff00ff, 0xff0000, 0x0000ff, 0x000000 };
		for (unsigned i = 0; i < 8; i++)
			rect(v.xres * i / 8, 0, v.xres * (i + 1) / 8, v.yres, col[i]);
	} else if (!strcmp(cmd, "fill") && argc > 2) {
		rect(0, 0, v.xres, v.yres, (uint32_t)strtoul(argv[2], NULL, 16));
	} else if (!strcmp(cmd, "status") && argc > 2) {
		status_dots((unsigned)atoi(argv[2]));
	} else if (!strcmp(cmd, "image") && argc > 2) {
		draw_image(&im);
	} else {
		fprintf(stderr, "usage: fbtool info | bars | fill RRGGBB | status N | image FILE | check FILE [OUT.ppm]  (prefix --no-put to skip FBIOPUT_VSCREENINFO)\n");
		return 2;
	}
	flip();
	return 0;
}

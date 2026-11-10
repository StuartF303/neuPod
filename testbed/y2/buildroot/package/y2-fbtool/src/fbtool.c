/* fbtool: minimal framebuffer test tool (no fonts).
 *   fbtool info | bars | fill RRGGBB | status N
 * Handles any bpp >= 16 via the reported channel offsets/lengths and the
 * line stride. Draws into every virtual page, then pans to page 0. */
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

static void flip(void)
{
	v.xoffset = v.yoffset = 0;
	ioctl(fd, FBIOPAN_DISPLAY, &v);
	if (!no_put) { /* mtkfb: some builds only refresh on PUT_VSCREENINFO */
		v.activate = FB_ACTIVATE_NOW;
		ioctl(fd, FBIOPUT_VSCREENINFO, &v);
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

	for (unsigned i = 0; i < 2 && fd < 0; i++)
		fd = open(paths[i], O_RDWR);
	if (fd < 0) { perror("open fb0"); return 1; }
	if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) {
		perror("FBIOGET_*SCREENINFO");
		return 1;
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
		unsigned n = (unsigned)atoi(argv[2]);
		unsigned bw = v.xres / 16 > 4 ? v.xres / 16 : 4;
		unsigned bh = bw > 28 ? 28 : bw;
		for (unsigned i = 0; i < 16; i++) /* 16 slots: lit = green, unlit = dark grey */
			rect(i * bw + 1, 0, (i + 1) * bw - 1, bh, i < n ? 0x00ff00 : 0x303030);
	} else {
		fprintf(stderr, "usage: fbtool info | bars | fill RRGGBB | status N  (prefix --no-put to skip FBIOPUT_VSCREENINFO)\n");
		return 2;
	}
	flip();
	return 0;
}

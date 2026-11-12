/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --no-compress --no-prefilter --bpp 1 --size 16 --lv-font-name neupod_icons_16 --font lucide-static@1.72.0/font/lucide.ttf -r 0xe06f,0xe13c,0xe12e,0xe15f,0xe160,0xe15e,0xe146,0xe1fd,0xe1ab,0xe05c,0xe1ae,0xe10b,0xe0d7,0xe0c0,0xe10c --format lvgl -o app/ui/fonts/neupod_icons_16.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef NEUPOD_ICONS_16
#define NEUPOD_ICONS_16 1
#endif

#if NEUPOD_ICONS_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+E05C "" */
    0x18, 0x38, 0x54, 0x9d, 0x6f, 0x8e, 0x1c, 0x7d,
    0xae, 0x48, 0xa1, 0x82, 0x0,

    /* U+E06F "" */
    0xc1, 0x83, 0x6, 0xc, 0x63, 0x18, 0xc0,

    /* U+E0C0 "" */
    0x7f, 0x88, 0x1c, 0x81, 0x68, 0x13, 0x81, 0xf8,
    0x1, 0x80, 0x18, 0x1, 0x80, 0x18, 0x1, 0x80,
    0x18, 0x1, 0x80, 0x17, 0xfe,

    /* U+E0D7 "" */
    0x7c, 0x2, 0x10, 0x8, 0x3f, 0xa0, 0x1, 0x80,
    0x6, 0x0, 0x18, 0x0, 0x60, 0x1, 0x80, 0x6,
    0x0, 0x18, 0x0, 0x5f, 0xff,

    /* U+E10B "" */
    0x7, 0x0, 0x44, 0x4, 0x10, 0x20, 0x81, 0x4,
    0x8, 0x21, 0xff, 0xd0, 0x1, 0x80, 0xc, 0x0,
    0x60, 0x3, 0x0, 0x18, 0x0, 0xbf, 0xf8,

    /* U+E10C "" */
    0x7, 0x80, 0x46, 0x4, 0x10, 0x20, 0x81, 0x0,
    0x8, 0x1, 0xff, 0xd0, 0x1, 0x80, 0xc, 0x0,
    0x60, 0x3, 0x0, 0x18, 0x0, 0xbf, 0xf8,

    /* U+E12E "" */
    0xf3, 0xe4, 0x99, 0x26, 0x49, 0x92, 0x64, 0x99,
    0x26, 0x49, 0x92, 0x64, 0x99, 0x26, 0x49, 0xf3,
    0xc0,

    /* U+E13C "" */
    0x60, 0xf, 0x80, 0x9c, 0x8, 0x70, 0x81, 0xc8,
    0x6, 0x80, 0x18, 0x1, 0x80, 0x68, 0x1c, 0x87,
    0x9, 0xc0, 0xf0, 0x6, 0x0,

    /* U+E146 "" */
    0x0, 0x0, 0x1, 0x0, 0x4, 0xff, 0xfc, 0x1,
    0x40, 0x12, 0x0, 0x0, 0x1, 0x20, 0xa, 0x0,
    0xff, 0xfc, 0x80, 0x2, 0x0, 0x0, 0x0,

    /* U+E15E "" */
    0x0, 0x10, 0x0, 0x30, 0x0, 0x37, 0x87, 0xf1,
    0x98, 0xc0, 0x63, 0x0, 0x84, 0x3, 0x8, 0xc,
    0x18, 0x33, 0x1b, 0xc3, 0xf8, 0x0, 0x60, 0x1,
    0x80, 0x2, 0x0,

    /* U+E15F "" */
    0x80, 0x34, 0x3, 0xe0, 0x73, 0xe, 0x18, 0xc0,
    0xcc, 0x6, 0xc0, 0x33, 0x1, 0x8e, 0xc, 0x18,
    0x60, 0x33, 0x0, 0xf0,

    /* U+E160 "" */
    0x60, 0xd, 0xc0, 0x63, 0x83, 0x6, 0x18, 0x1c,
    0xc0, 0x36, 0x1, 0xb0, 0x39, 0x83, 0xc, 0x70,
    0x6e, 0x2, 0xc0, 0x10,

    /* U+E1AB "" */
    0x6, 0x0, 0x1c, 0x30, 0x68, 0x27, 0x92, 0x68,
    0x26, 0x50, 0x44, 0xa0, 0x89, 0x41, 0x32, 0xf2,
    0x4c, 0x34, 0x10, 0x38, 0x60, 0x30, 0x0,

    /* U+E1AE "" */
    0x1f, 0xf0, 0xe0, 0x3b, 0x0, 0x18, 0x3e, 0x1,
    0xc7, 0x6, 0x3, 0x0, 0xf0, 0x2, 0x10, 0x0,
    0x0, 0x3, 0x0,

    /* U+E1FD "" */
    0x0, 0x0, 0x3, 0x0, 0xc, 0xff, 0xfc, 0x3,
    0x43, 0x32, 0x8, 0x2, 0x41, 0x32, 0xb, 0x0,
    0xff, 0xfc, 0xc0, 0x3, 0x0, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 256, .box_w = 7, .box_h = 14, .ofs_x = 5, .ofs_y = 1},
    {.bitmap_index = 13, .adv_w = 256, .box_w = 6, .box_h = 9, .ofs_x = 5, .ofs_y = 3},
    {.bitmap_index = 20, .adv_w = 256, .box_w = 12, .box_h = 14, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 41, .adv_w = 256, .box_w = 14, .box_h = 12, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 62, .adv_w = 256, .box_w = 13, .box_h = 14, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 85, .adv_w = 256, .box_w = 13, .box_h = 14, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 108, .adv_w = 256, .box_w = 10, .box_h = 13, .ofs_x = 3, .ofs_y = 2},
    {.bitmap_index = 125, .adv_w = 256, .box_w = 12, .box_h = 14, .ofs_x = 3, .ofs_y = 1},
    {.bitmap_index = 146, .adv_w = 256, .box_w = 13, .box_h = 14, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 169, .adv_w = 256, .box_w = 15, .box_h = 14, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 196, .adv_w = 256, .box_w = 13, .box_h = 12, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 216, .adv_w = 256, .box_w = 13, .box_h = 12, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 236, .adv_w = 256, .box_w = 15, .box_h = 12, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 259, .adv_w = 256, .box_w = 15, .box_h = 10, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 278, .adv_w = 256, .box_w = 13, .box_h = 14, .ofs_x = 2, .ofs_y = 1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x13, 0x64, 0x7b, 0xaf, 0xb0, 0xd2, 0xe0,
    0xea, 0x102, 0x103, 0x104, 0x14f, 0x152, 0x1a1
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 57436, .range_length = 418, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 15, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t neupod_icons_16 = {
#else
lv_font_t neupod_icons_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,          /*The maximum line height required by the font*/
    .base_line = -1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if NEUPOD_ICONS_16*/


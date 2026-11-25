// SPDX-License-Identifier: MPL-2.0
/**
 * @file art.h
 * @brief Album art: pick the picture (embedded, then cover.jpg / folder.jpg / front.jpg) and decode a baseline JPEG, streamed
 *        from the card, into a small RGB565 thumbnail (at most 64x64) without ever holding the whole image. Portable, no OS
 *        calls; the storage task drives it on core 0 (runtime/art_service.c). Host-tested. Never used from core 1.
 *
 * The decoder (TJpgDec, third_party/tjpgd) delivers the image one MCU row at a time, already shrunk by 1/1, 1/2, 1/4 or
 * 1/8. Each MCU row is collected in a strip, then box-averaged into the thumbnail. Working memory (about 4 KB for the
 * decoder tables, a strip of 1 to 4 KB and 0.5 KB of accumulators) is taken from the heap for the length of one decode.
 */
#ifndef NEUPOD_ART_H
#define NEUPOD_ART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "meta.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thumbnail size and cache (NP-5, docs/ui.md "Album art"): hosted builds (the simulator, the Y2) and the RP2350B with PSRAM decode to a
 * 160 x 160 RGB565 thumbnail (50 KB) and keep the last 24 albums (1.2 MB); nothing is ever scaled up, the UI draws it smaller. The Pico 2 W
 * (ADR-0001 amendment: frozen) keeps one 64 x 64 thumbnail (8 KB) and no cache. A build with PSRAM defines NEUPOD_ART_HIRES.
 */
#ifdef NEUPOD_ART_HIRES
#define ART_MAX_DIM 160u                         /**< the thumbnail fits a 160 x 160 box */
#define ART_CACHE_N 24u                          /**< thumbnails kept, least recently used out first */
#else
#define ART_MAX_DIM 64u                          /**< the thumbnail fits a 64 x 64 box */
#define ART_CACHE_N 1u                           /**< one thumbnail: the track playing (no cache, no browse art) */
#endif
#define ART_PIXELS (ART_MAX_DIM * ART_MAX_DIM)   /**< RGB565 pixels in a thumbnail buffer (8 KB at 64, 50 KB at 160) */
#ifndef ART_POOL_BYTES
#define ART_POOL_BYTES 4096u /**< TJpgDec pool: 4:2:0 with two quantisation tables needs about 3.5 KB (measured in tests: see docs/audio.md) */
#endif
#define ART_MAX_SRC_DIM 5120u                    /**< larger images are refused (the strip must stay small) */

typedef enum {
    ART_OK = 0,
    ART_E_READ,        /**< the card or file read failed */
    ART_E_FORMAT,      /**< not a JPEG, or damaged / truncated */
    ART_E_UNSUPPORTED, /**< progressive or another non-baseline JPEG, or tables bigger than the pool */
    ART_E_TOO_BIG,     /**< wider or taller than ART_MAX_SRC_DIM */
    ART_E_MEMORY,      /**< no heap for the working buffers */
    ART_E_ABORTED,     /**< the yield callback asked to stop */
} art_err_t;

const char *art_err_str(art_err_t e);

/** Called often during a decode (before each card read and each MCU). Return false to abort. May sleep. */
typedef bool (*art_yield_fn)(void *ctx);

typedef struct {
    uint32_t src_w, src_h; /**< the JPEG's size */
    uint16_t w, h;         /**< the thumbnail's size (row stride = w) */
    uint8_t scale;         /**< decoder scale used: 0 = 1/1, 1 = 1/2, 2 = 1/4, 3 = 1/8 */
} art_info_t;

/**
 * Decode the JPEG of @p length bytes at @p offset (read through @p rd) into @p px (ART_PIXELS entries, row-major, stride
 * info->w, RGB565 in the CPU's native order: the same as LVGL's RGB565 draw buffers; the display driver swaps for the panel).
 * The picture keeps its aspect ratio and is shrunk to fit 64 x 64 (never enlarged). Reads in 512-byte pieces and skips
 * header segments without reading them.
 */
art_err_t art_decode(meta_read_fn rd, void *rd_ctx, uint64_t offset, uint32_t length, uint16_t *px, art_info_t *info,
                     art_yield_fn yield, void *yield_ctx);

/* ---- which picture ---- */

typedef enum { ART_SRC_EMBEDDED = 1, ART_SRC_FOLDER } art_src_t;

/**
 * Sources to try, in order: the embedded picture (only a JPEG: PNG and other formats cannot be decoded), then a cover file in
 * the folder. @p folder_has says whether cover.jpg / folder.jpg / front.jpg exists. Returns how many entries of @p out (2) are set.
 */
size_t art_sources(const art_ref_t *embedded, bool folder_has, art_src_t out[2]);

/** 1 for "cover.jpg", 2 for "folder.jpg", 3 for "front.jpg" (case-insensitive); 0 for any other name. Lower wins. */
int art_folder_rank(const char *name);

/** Next file name of a folder listing (false at the end). */
typedef bool (*art_name_next_fn)(void *ctx, char *name, size_t cap);
/** Walk a listing and return the rank of the best cover file; its name (as listed) goes to @p best. 0 = none. */
int art_folder_pick(art_name_next_fn next, void *ctx, char *best, size_t cap);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_ART_H

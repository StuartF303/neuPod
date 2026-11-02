// SPDX-License-Identifier: MPL-2.0
// Configuration of the vendored dr_flac / dr_mp3 for the bare-metal audio core. Included before each dr_*.h, in
// the implementation unit (codec_dr.c) AND wherever the headers are used (struct layouts depend on these).
#ifndef NEUPOD_CODEC_DR_CONFIG_H
#define NEUPOD_CODEC_DR_CONFIG_H

#include "codec_alloc.h"

#define DR_FLAC_NO_STDIO /* no fopen/FILE: the data comes from the core-0 ring */
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_BUFFER_SIZE 2048
#ifndef NEUPOD_FLAC_CRC
#define DR_FLAC_NO_CRC /* skip CRC checks to save CPU; the STREAMINFO MD5 verify mode covers integrity */
#endif
#define DRFLAC_ASSERT(expression) ((void)0)
#define DRFLAC_MALLOC(sz) codec_malloc((sz))
#define DRFLAC_REALLOC(p, sz) codec_realloc((p), (sz))
#define DRFLAC_FREE(p) codec_free((p))

#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DR_MP3_ONLY_MP3
#define DR_MP3_FLOAT_OUTPUT
#define DRMP3_DATA_CHUNK_SIZE 24576 /* input buffer (default 64 KB is too much for the arena) */
#define DRMP3_ASSERT(expression) ((void)0)
#define DRMP3_MALLOC(sz) codec_malloc((sz))
#define DRMP3_REALLOC(p, sz) codec_realloc((p), (sz))
#define DRMP3_FREE(p) codec_free((p))

#endif

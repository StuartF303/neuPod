// SPDX-License-Identifier: MPL-2.0
// Allocator the vendored codecs use on the audio core: a static arena (no malloc on core 1, ADR-0011).
#ifndef NEUPOD_CODEC_ALLOC_H
#define NEUPOD_CODEC_ALLOC_H

#include <stddef.h>

void *codec_malloc(size_t n);
void *codec_realloc(void *p, size_t n);
void codec_free(void *p);

#endif

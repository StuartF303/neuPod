// SPDX-License-Identifier: Apache-2.0
// C interface to the ported ALAC decoder core (see alac_dec.c for provenance and the licence).
#ifndef NEUPOD_ALAC_DEC_H
#define NEUPOD_ALAC_DEC_H

#include <stddef.h>
#include <stdint.h>

#include "ALACAudioTypes.h"

typedef struct {
    ALACSpecificConfig cfg;
    int32_t *mixU, *mixV, *pred;
    uint16_t *shift;
} alac_dec_t;

/** Parse the magic cookie ('alac' atom body, optionally wrapped) and allocate three frameLength-int32 buffers with @p alloc. 0 = ok. */
int32_t alac_dec_init(alac_dec_t *d, const uint8_t *cookie, uint32_t cookie_bytes, void *(*alloc)(size_t));
/** Bytes of one output frame in the packed layout (16: 2/ch, 20 and 24: 3/ch, 32: 4/ch). */
uint32_t alac_dec_out_bytes_per_frame(const alac_dec_t *d);
/** Decode one ALAC packet into @p out (room for frameLength frames of alac_dec_out_bytes_per_frame()). 0 = ok. */
int32_t alac_dec_decode(alac_dec_t *d, const uint8_t *in, uint32_t in_bytes, uint8_t *out, uint32_t *out_frames);

#endif

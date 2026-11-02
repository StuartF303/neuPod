// SPDX-License-Identifier: MPL-2.0
/**
 * @file arena.h
 * @brief Bump allocator over a static buffer, for the codecs on the bare-metal audio core (ADR-0011: no malloc).
 *
 * Blocks are 8-byte aligned and carry an 8-byte header. free() only gives memory back when the block is the
 * newest one (the codecs allocate once at open and free in reverse order at close); everything is released at once
 * by arena_reset() per track. realloc() grows the newest block in place.
 */
#ifndef NEUPOD_ARENA_H
#define NEUPOD_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *base;
    size_t cap, used, high;
    uint32_t failures; /**< allocations refused for lack of space since arena_init */
} arena_t;

void arena_init(arena_t *a, void *storage, size_t bytes);
void arena_reset(arena_t *a);
void *arena_alloc(arena_t *a, size_t n);
void *arena_realloc(arena_t *a, void *p, size_t n);
void arena_free(arena_t *a, void *p);

#ifdef __cplusplus
}
#endif

#endif

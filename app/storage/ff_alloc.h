// SPDX-License-Identifier: MPL-2.0
/**
 * @file ff_alloc.h
 * @brief FatFs working-memory policy (ff_memalloc / ff_memfree, FF_USE_LFN == 3).
 *
 * FatFs asks for scratch buffers as big as a whole cluster (dir_clear: up to 32 KB; 128 KB clusters on big exFAT
 * cards) and is built to halve the request, or fall back to its window buffer, when it gets NULL. On the RP2350 the
 * newlib heap is about 33 KB and the SDK's malloc wrapper panics instead of returning NULL, so the stock
 * ffsystem.c brought the firmware down on the first directory creation. This replacement caps every request and
 * only uses an allocator that reports failure with NULL.
 */
#ifndef NEUPOD_FF_ALLOC_H
#define NEUPOD_FF_ALLOC_H

/** Largest block ff_memalloc will ever hand out; bigger requests get NULL and FatFs falls back to smaller ones. */
#define NEUPOD_FF_ALLOC_MAX_BYTES 4096u

#endif // NEUPOD_FF_ALLOC_H

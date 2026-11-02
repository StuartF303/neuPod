// SPDX-License-Identifier: MPL-2.0
// Private to app/audio: the rings and statistics shared by the core-0 side (audio_ctl.c) and the
// core-1 side (audio_core.c). Plain data and lock-free types only (core-1 rules, ADR-0011).
#ifndef NEUPOD_AUDIO_SHARED_H
#define NEUPOD_AUDIO_SHARED_H

#include "audio_engine.h"
#include "seqlock.h"
#include "spsc_ring.h"

typedef enum {
    AUDIO_CMD_OPEN = 1, /* a = sample rate, b = channels | valid_bits << 8 */
    AUDIO_CMD_CLOSE,
    AUDIO_CMD_NEW_WINDOW,
} audio_cmd_type_t;

typedef struct {
    uint32_t type;
    uint32_t a;
    uint32_t b;
} audio_cmd_t;

extern spsc_ring_t g_audio_cmd_ring;       /* core 0 produces, core 1 loop consumes */
extern spsc_ring_t g_audio_data_ring;      /* core 0 produces, core 1 loop consumes */
extern spsc_ring_t g_audio_evt_isr_ring;   /* produced by the core-1 block interrupt */
extern spsc_ring_t g_audio_evt_loop_ring;  /* produced by the core-1 main loop (one producer each) */
extern seqlock_t g_audio_stats_lock;       /* written only by the core-1 block interrupt */
extern audio_stats_t g_audio_stats;

/** Initialise the rings (core 0, before core 1 is launched). */
void audio_shared_init(void);

#endif // NEUPOD_AUDIO_SHARED_H

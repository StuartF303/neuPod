// SPDX-License-Identifier: MPL-2.0
// The Bluetooth PCM ring (bt_pcm.h). CORE-1 RULES apply (docs/adr/0011-bare-metal-audio-core.md): lock-free, nothing from the RTOS.
#include "bt_pcm.h"

#include <stddef.h>

#include "ram_func.h"
#include "spsc_ring.h"

static spsc_ring_t g_ring;
static volatile bool g_attached;
static volatile bool g_active;
static volatile uint32_t g_flush_req;  // written by one core-0 task (bt_pcm_request_flush)
static uint32_t g_flush_seen;          // written by the consumer only

void bt_pcm_attach(void *storage, uint32_t frames) {
    g_attached = spsc_ring_init(&g_ring, storage, BT_PCM_FRAME_BYTES, frames);
    g_active = false;
    g_flush_seen = g_flush_req;
}

uint32_t NEUPOD_RAM_FUNC(bt_pcm_space)(void) { return g_attached ? spsc_ring_space(&g_ring) : 0u; }

uint32_t NEUPOD_RAM_FUNC(bt_pcm_write)(const int16_t *interleaved, uint32_t frames) {
    return g_attached ? spsc_ring_write(&g_ring, interleaved, frames) : 0u;
}

void NEUPOD_RAM_FUNC(bt_pcm_set_active)(bool active) { g_active = active; }

void bt_pcm_request_flush(void) { g_flush_req = g_flush_req + 1u; }

// Consumer side. A pending flush request is honoured here, by the one reader of the ring.
static void honour_flush(void) {
    uint32_t want = g_flush_req;
    if (want != g_flush_seen) {
        g_flush_seen = want;
        if (g_attached) {
            (void)spsc_ring_discard(&g_ring, 0xFFFFFFFFu);
        }
    }
}

static uint32_t src_available(void *ctx) {
    (void)ctx;
    honour_flush();
    return g_attached ? spsc_ring_available(&g_ring) : 0u;
}

static uint32_t src_capacity(void *ctx) {
    (void)ctx;
    return g_attached ? g_ring.mask + 1u : 0u;
}

static uint32_t src_read(void *ctx, int16_t *dst, uint32_t frames) {
    (void)ctx;
    honour_flush();
    return g_attached ? spsc_ring_read(&g_ring, dst, frames) : 0u;
}

static void src_flush(void *ctx) {
    (void)ctx;
    if (g_attached) {
        (void)spsc_ring_discard(&g_ring, 0xFFFFFFFFu);
    }
    g_flush_seen = g_flush_req;
}

static bool src_active(void *ctx) {
    (void)ctx;
    return g_active;
}

static const hal_bt_pcm_source_t k_source = {.ctx = NULL, .available = src_available, .capacity = src_capacity, .read = src_read,
                                             .flush = src_flush, .active = src_active};

const hal_bt_pcm_source_t *bt_pcm_source(void) { return &k_source; }

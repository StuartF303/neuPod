// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_bt_tlv.h"

#include <pthread.h>
#include <string.h>

#define SLOTS 16u    /* 8 link keys (HAL_BT_KEY_SLOTS) plus room for a record that is deleted and not yet written back */
#define VAL_MAX 48u  /* a link key record is HAL_BT_KEY_RECORD_LEN = 28 bytes */
#define RETRY_MS 1000u

enum { FREE = 0, LIVE, TOMB };
enum { P_NONE = 0, P_PUT, P_DEL };

typedef struct {
    uint32_t tag;
    uint32_t ver; /* bumped by every change: a write-back only clears the pending mark if nothing changed meanwhile */
    uint8_t len;
    uint8_t state;
    uint8_t pending;
    uint8_t val[VAL_MAX];
} slot_t;

/* A native lock, held for a few instructions and never across a call into the settings store. BTstack's thread may wait for a task
 * that the RTOS has switched out (milliseconds); tasks only ever wait for BTstack's thread, which the RTOS never switches out. The
 * only task-side callers are attach (before the thread exists) and flush (one task at a time, see bt_port_stpbt.c). */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static slot_t g_slot[SLOTS];
static const hal_bt_storage_t *g_back;
static hal_bt_storage_t g_view;
static bool g_wait;
static uint32_t g_next_ms;

static slot_t *find(uint32_t tag) {
    for (uint32_t i = 0; i < SLOTS; i++) {
        if (g_slot[i].state != FREE && g_slot[i].tag == tag) {
            return &g_slot[i];
        }
    }
    return NULL;
}

static slot_t *find_free(void) {
    for (uint32_t i = 0; i < SLOTS; i++) {
        if (g_slot[i].state == FREE) {
            return &g_slot[i];
        }
    }
    return NULL;
}

static int view_get(void *ctx, uint32_t tag, uint8_t *buf, uint32_t size) {
    (void)ctx;
    int r = 0;
    pthread_mutex_lock(&g_mu);
    const slot_t *s = find(tag);
    if (s != NULL && s->state == LIVE) {
        uint32_t n = s->len;
        if (buf != NULL) {
            n = n < size ? n : size;
            memcpy(buf, s->val, n);
        }
        r = (int)n;
    }
    pthread_mutex_unlock(&g_mu);
    return r;
}

static int view_put(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size) {
    (void)ctx;
    if (data == NULL || size == 0u || size > VAL_MAX) {
        return -1;
    }
    int r = -1;
    pthread_mutex_lock(&g_mu);
    slot_t *s = find(tag);
    if (s == NULL) {
        s = find_free();
    }
    if (s != NULL) {
        s->tag = tag;
        s->len = (uint8_t)size;
        memcpy(s->val, data, size);
        s->state = LIVE;
        s->pending = P_PUT;
        s->ver++;
        r = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return r;
}

static void view_del(void *ctx, uint32_t tag) {
    (void)ctx;
    pthread_mutex_lock(&g_mu);
    slot_t *s = find(tag);
    if (s != NULL && s->state == LIVE) {
        s->state = TOMB; /* kept until the store has dropped it, so get() says "absent" at once */
        s->pending = P_DEL;
        s->ver++;
    }
    pthread_mutex_unlock(&g_mu);
}

void y2_bt_tlv_reset(void) {
    pthread_mutex_lock(&g_mu);
    memset(g_slot, 0, sizeof g_slot);
    g_back = NULL;
    g_wait = false;
    g_next_ms = 0;
    pthread_mutex_unlock(&g_mu);
}

const hal_bt_storage_t *y2_bt_tlv_attach(const hal_bt_storage_t *backing) {
    y2_bt_tlv_reset();
    if (backing == NULL) {
        return NULL;
    }
    g_back = backing;
    for (uint32_t i = 0; i < HAL_BT_KEY_SLOTS; i++) {
        uint8_t buf[VAL_MAX];
        int n = backing->get(backing->ctx, HAL_BT_KEY_TAG(i), buf, sizeof buf);
        if (n > 0) {
            pthread_mutex_lock(&g_mu);
            slot_t *s = find_free();
            if (s != NULL) {
                s->tag = HAL_BT_KEY_TAG(i);
                s->len = (uint8_t)n;
                memcpy(s->val, buf, (size_t)n);
                s->state = LIVE;
            }
            pthread_mutex_unlock(&g_mu);
        }
    }
    g_view = (hal_bt_storage_t){.get = view_get, .put = view_put, .del = view_del, .ctx = NULL};
    return &g_view;
}

void y2_bt_tlv_flush(uint32_t now_ms) {
    if (g_back == NULL || (g_wait && (int32_t)(now_ms - g_next_ms) < 0)) {
        return;
    }
    for (uint32_t i = 0; i < SLOTS; i++) {
        pthread_mutex_lock(&g_mu);
        slot_t snap = g_slot[i];
        pthread_mutex_unlock(&g_mu);
        if (snap.pending == P_NONE) {
            continue;
        }
        bool ok;
        if (snap.pending == P_PUT) {
            ok = g_back->put(g_back->ctx, snap.tag, snap.val, snap.len) == 0;
        } else {
            g_back->del(g_back->ctx, snap.tag);
            ok = g_back->get(g_back->ctx, snap.tag, NULL, 0) == 0; /* the store drops a delete silently while it is not writable: check */
        }
        pthread_mutex_lock(&g_mu);
        if (ok) {
            g_wait = false;
            if (g_slot[i].ver == snap.ver) {
                g_slot[i].pending = P_NONE;
                if (g_slot[i].state == TOMB) {
                    g_slot[i].state = FREE;
                }
            }
        }
        pthread_mutex_unlock(&g_mu);
        if (!ok) {
            g_wait = true;
            g_next_ms = now_ms + RETRY_MS;
            return;
        }
    }
}

bool y2_bt_tlv_pending(void) {
    bool any = false;
    pthread_mutex_lock(&g_mu);
    for (uint32_t i = 0; i < SLOTS; i++) {
        any = any || g_slot[i].pending != P_NONE;
    }
    pthread_mutex_unlock(&g_mu);
    return any;
}

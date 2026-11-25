// SPDX-License-Identifier: MPL-2.0
// The Y2's RAM copy of BTstack's link keys and its write-behind to the settings store, against a fake store.
#include <string.h>

#include "unit.h"
#include "y2_bt_tlv.h"

#define MAXREC 16

typedef struct {
    uint32_t tag;
    uint8_t len;
    uint8_t val[48];
    bool used;
} rec_t;

typedef struct {
    rec_t rec[MAXREC];
    bool writable; // false: puts fail, deletes are silently dropped (as the settings store does while it is not writable)
    int puts, dels;
} fake_t;

static fake_t g_fake;

static rec_t *fnd(uint32_t tag) {
    for (int i = 0; i < MAXREC; i++) {
        if (g_fake.rec[i].used && g_fake.rec[i].tag == tag) {
            return &g_fake.rec[i];
        }
    }
    return NULL;
}

static int f_get(void *ctx, uint32_t tag, uint8_t *buf, uint32_t size) {
    (void)ctx;
    rec_t *r = fnd(tag);
    if (r == NULL) {
        return 0;
    }
    if (buf == NULL) {
        return r->len;
    }
    uint32_t n = r->len < size ? r->len : size;
    memcpy(buf, r->val, n);
    return (int)n;
}

static int f_put(void *ctx, uint32_t tag, const uint8_t *d, uint32_t n) {
    (void)ctx;
    if (!g_fake.writable) {
        return -1;
    }
    rec_t *r = fnd(tag);
    for (int i = 0; r == NULL && i < MAXREC; i++) {
        if (!g_fake.rec[i].used) {
            r = &g_fake.rec[i];
        }
    }
    r->used = true;
    r->tag = tag;
    r->len = (uint8_t)n;
    memcpy(r->val, d, n);
    g_fake.puts++;
    return 0;
}

static void f_del(void *ctx, uint32_t tag) {
    (void)ctx;
    if (!g_fake.writable) {
        return;
    }
    rec_t *r = fnd(tag);
    if (r != NULL) {
        r->used = false;
        g_fake.dels++;
    }
}

static const hal_bt_storage_t k_back = {.get = f_get, .put = f_put, .del = f_del, .ctx = NULL};

static void fresh(bool writable) {
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.writable = writable;
    y2_bt_tlv_reset();
}

static const uint8_t k_key[28] = {1, 0, 0, 0, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0xAA};
#define TAG HAL_BT_KEY_TAG(0)

TEST(null_backing_gives_no_storage) {
    fresh(true);
    ASSERT_TRUE(y2_bt_tlv_attach(NULL) == NULL);
}

TEST(preloads_link_keys_from_the_store) {
    fresh(true);
    f_put(NULL, HAL_BT_KEY_TAG(3), k_key, sizeof k_key);
    f_put(NULL, 0x12345678u, k_key, sizeof k_key); // not a link key: not cached
    const hal_bt_storage_t *v = y2_bt_tlv_attach(&k_back);
    ASSERT_TRUE(v != NULL);
    uint8_t b[48];
    ASSERT_EQ(28, v->get(v->ctx, HAL_BT_KEY_TAG(3), NULL, 0));
    ASSERT_EQ(28, v->get(v->ctx, HAL_BT_KEY_TAG(3), b, sizeof b));
    ASSERT_TRUE(memcmp(b, k_key, 28) == 0);
    ASSERT_EQ(0, v->get(v->ctx, 0x12345678u, b, sizeof b));
    ASSERT_EQ(0, v->get(v->ctx, HAL_BT_KEY_TAG(0), b, sizeof b));
    ASSERT_FALSE(y2_bt_tlv_pending());
}

TEST(put_is_visible_at_once_and_written_behind) {
    fresh(true);
    const hal_bt_storage_t *v = y2_bt_tlv_attach(&k_back);
    ASSERT_EQ(0, v->put(v->ctx, TAG, k_key, sizeof k_key));
    uint8_t b[48];
    ASSERT_EQ(28, v->get(v->ctx, TAG, b, sizeof b));
    ASSERT_EQ(0, g_fake.puts); // the store has not been touched by BTstack's thread
    ASSERT_TRUE(y2_bt_tlv_pending());
    y2_bt_tlv_flush(0);
    ASSERT_EQ(1, g_fake.puts);
    ASSERT_TRUE(fnd(TAG) != NULL && memcmp(fnd(TAG)->val, k_key, 28) == 0);
    ASSERT_FALSE(y2_bt_tlv_pending());
}

TEST(delete_hides_the_key_at_once_and_is_written_behind) {
    fresh(true);
    f_put(NULL, TAG, k_key, sizeof k_key);
    const hal_bt_storage_t *v = y2_bt_tlv_attach(&k_back);
    v->del(v->ctx, TAG);
    uint8_t b[48];
    ASSERT_EQ(0, v->get(v->ctx, TAG, b, sizeof b));
    ASSERT_TRUE(fnd(TAG) != NULL); // still on the card until the flush
    y2_bt_tlv_flush(0);
    ASSERT_TRUE(fnd(TAG) == NULL);
    ASSERT_FALSE(y2_bt_tlv_pending());
    ASSERT_EQ(0, v->get(v->ctx, TAG, b, sizeof b));
}

TEST(store_not_writable_keeps_the_key_for_the_session_and_retries_once_a_second) {
    fresh(false);
    const hal_bt_storage_t *v = y2_bt_tlv_attach(&k_back);
    ASSERT_EQ(0, v->put(v->ctx, TAG, k_key, sizeof k_key)); // pairing still works
    y2_bt_tlv_flush(100);
    ASSERT_TRUE(y2_bt_tlv_pending());
    uint8_t b[48];
    ASSERT_EQ(28, v->get(v->ctx, TAG, b, sizeof b)); // and the key is usable
    g_fake.writable = true;
    y2_bt_tlv_flush(600); // too soon: no retry
    ASSERT_EQ(0, g_fake.puts);
    y2_bt_tlv_flush(1100);
    ASSERT_EQ(1, g_fake.puts);
    ASSERT_FALSE(y2_bt_tlv_pending());
}

TEST(a_dropped_delete_is_noticed_and_retried) {
    fresh(true);
    f_put(NULL, TAG, k_key, sizeof k_key);
    const hal_bt_storage_t *v = y2_bt_tlv_attach(&k_back);
    g_fake.writable = false;
    v->del(v->ctx, TAG);
    y2_bt_tlv_flush(0);
    ASSERT_TRUE(y2_bt_tlv_pending()); // the store silently ignored the delete
    g_fake.writable = true;
    y2_bt_tlv_flush(1000);
    ASSERT_TRUE(fnd(TAG) == NULL);
    ASSERT_FALSE(y2_bt_tlv_pending());
}

TEST(put_after_delete_of_the_same_tag_wins) {
    fresh(true);
    f_put(NULL, TAG, k_key, sizeof k_key);
    const hal_bt_storage_t *v = y2_bt_tlv_attach(&k_back);
    v->del(v->ctx, TAG);
    uint8_t other[28];
    memset(other, 0x5A, sizeof other);
    ASSERT_EQ(0, v->put(v->ctx, TAG, other, sizeof other));
    y2_bt_tlv_flush(0);
    ASSERT_TRUE(fnd(TAG) != NULL && memcmp(fnd(TAG)->val, other, 28) == 0);
    ASSERT_FALSE(y2_bt_tlv_pending());
}

TEST(oversize_and_full_table_are_refused) {
    fresh(true);
    const hal_bt_storage_t *v = y2_bt_tlv_attach(&k_back);
    uint8_t big[64] = {0};
    ASSERT_EQ(-1, v->put(v->ctx, TAG, big, sizeof big));
    ASSERT_EQ(-1, v->put(v->ctx, TAG, big, 0));
    for (uint32_t i = 0; i < 16; i++) {
        ASSERT_EQ(0, v->put(v->ctx, 0x100u + i, k_key, sizeof k_key));
    }
    ASSERT_EQ(-1, v->put(v->ctx, 0x200u, k_key, sizeof k_key));
}

int main(void) {
    RUN(null_backing_gives_no_storage);
    RUN(preloads_link_keys_from_the_store);
    RUN(put_is_visible_at_once_and_written_behind);
    RUN(delete_hides_the_key_at_once_and_is_written_behind);
    RUN(store_not_writable_keeps_the_key_for_the_session_and_retries_once_a_second);
    RUN(a_dropped_delete_is_noticed_and_retried);
    RUN(put_after_delete_of_the_same_tag_wins);
    RUN(oversize_and_full_table_are_refused);
    return RESULT();
}

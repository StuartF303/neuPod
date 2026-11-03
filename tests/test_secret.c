// SPDX-License-Identifier: MPL-2.0
// Secrets at rest: round trip, tamper, wrong chip ID, tag binding, fresh nonces; settings store integration and BT migration.
#define _GNU_SOURCE
#include <string.h>

#include "crypto_secret.h"
#include "hal_host.h"
#include "settings_store.h"
#include "unit.h"

TEST(test_round_trip_and_nonce) {
    const uint8_t pw[] = "correct horse battery staple";
    uint8_t a[96], b[96], out[96];
    size_t na = secret_seal(0x4E507730, pw, sizeof pw - 1, a, sizeof a);
    size_t nb = secret_seal(0x4E507730, pw, sizeof pw - 1, b, sizeof b);
    ASSERT_EQ(sizeof pw - 1 + SECRET_OVERHEAD, na);
    ASSERT_EQ(na, nb);
    ASSERT_TRUE(memcmp(a, b, na) != 0); // same plaintext, different nonce: different record
    ASSERT_TRUE(memcmp(a + 13, pw, sizeof pw - 1) != 0); // not plaintext
    ASSERT_EQ((int)(sizeof pw - 1), secret_open(0x4E507730, a, na, out, sizeof out));
    ASSERT_EQ(0, memcmp(out, pw, sizeof pw - 1));
    ASSERT_EQ(0, secret_seal(1, pw, 60, a, 70)); // too small a buffer: refused
}

TEST(test_tamper_truncate_and_tag_binding) {
    const uint8_t pw[] = "hunter2hunter2";
    uint8_t rec[96], out[96];
    size_t n = secret_seal(0x11111111, pw, sizeof pw - 1, rec, sizeof rec);
    ASSERT_TRUE(n > 0);
    for (size_t i = 0; i < n; i++) { // flip every single byte in turn: never authenticates
        rec[i] ^= 0x40;
        ASSERT_EQ(-1, secret_open(0x11111111, rec, n, out, sizeof out));
        rec[i] ^= 0x40;
    }
    ASSERT_EQ((int)(sizeof pw - 1), secret_open(0x11111111, rec, n, out, sizeof out));
    ASSERT_EQ(-1, secret_open(0x11111111, rec, n - 1, out, sizeof out)); // truncated
    ASSERT_EQ(-1, secret_open(0x22222222, rec, n, out, sizeof out));     // moved to another tag
    ASSERT_EQ(-1, secret_open(0x11111111, rec, 10, out, sizeof out));
}

TEST(test_wrong_chip_id_is_absent) {
    uint8_t rec[96], out[96];
    const uint8_t pw[] = "wifi-password";
    size_t n = secret_seal(7, pw, sizeof pw - 1, rec, sizeof rec);
    uint8_t other[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    hal_host_set_chip_id(other); // the card is now in another board
    secret_drop_cached_key();
    ASSERT_EQ(-1, secret_open(7, rec, n, out, sizeof out));
    uint8_t orig[8] = {0x4E, 0x45, 0x55, 0x50, 0x4F, 0x44, 0x48, 0x31};
    hal_host_set_chip_id(orig);
    secret_drop_cached_key();
    ASSERT_EQ((int)(sizeof pw - 1), secret_open(7, rec, n, out, sizeof out));
}

TEST(test_store_integration_and_migration) {
    settings_init(NULL);
    settings_attach_storage(false);
    const uint8_t pw[] = "swordfish";
    ASSERT_TRUE(settings_put_secret(0x4E507030, pw, sizeof pw - 1));
    uint8_t raw[96];
    uint32_t rl = settings_get(0x4E507030, raw, sizeof raw);
    ASSERT_EQ(sizeof pw - 1 + SECRET_OVERHEAD, rl);
    ASSERT_TRUE(memmem(raw, rl, "swordfish", 9) == NULL); // the stored value is ciphertext
    uint8_t out[96];
    ASSERT_EQ(sizeof pw - 1, settings_get_secret(0x4E507030, out, sizeof out));
    ASSERT_EQ(0, memcmp(out, pw, sizeof pw - 1));
    ASSERT_FALSE(settings_put_secret(0x4E507031, pw, 80)); // 80 + 29 > 96
    // damaged value: absent
    raw[20] ^= 1;
    ASSERT_TRUE(settings_put(0x4E507030, raw, rl));
    ASSERT_EQ(0, settings_get_secret(0x4E507030, out, sizeof out));
    // A 63-character Wi-Fi password fits.
    uint8_t long_pw[63];
    memset(long_pw, 'p', sizeof long_pw);
    ASSERT_TRUE(settings_put_secret(0x4E507032, long_pw, sizeof long_pw));
    ASSERT_EQ(63, settings_get_secret(0x4E507032, out, sizeof out));
    // Migration: a plaintext 27-byte link key written before encryption becomes sealed.
    uint8_t key[27];
    memset(key, 0xAB, sizeof key);
    ASSERT_TRUE(settings_put(0x42544C00, key, sizeof key));
    settings_migrate_bt_secrets();
    ASSERT_EQ(27 + SECRET_OVERHEAD, settings_get(0x42544C00, NULL, 0));
    ASSERT_EQ(27, settings_get_secret(0x42544C00, out, sizeof out));
    ASSERT_EQ(0, memcmp(out, key, 27));
    settings_migrate_bt_secrets(); // idempotent
    ASSERT_EQ(27 + SECRET_OVERHEAD, settings_get(0x42544C00, NULL, 0));
}

int main(void) {
    RUN(test_round_trip_and_nonce);
    RUN(test_tamper_truncate_and_tag_binding);
    RUN(test_wrong_chip_id_is_absent);
    RUN(test_store_integration_and_migration);
    return RESULT();
}

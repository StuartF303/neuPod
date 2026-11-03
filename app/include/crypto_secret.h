// SPDX-License-Identifier: MPL-2.0
/**
 * @file crypto_secret.h
 * @brief Secrets at rest: AES-256-GCM with a key from a pluggable key provider (docs/wifi.md, "Secrets at rest").
 *
 * Record layout: version(1) | nonce(12, random per write) | ciphertext | tag(16). The additional authenticated data is the
 * record's tag (its 32-bit key in the settings store), so a record cannot be moved to another key. Decryption failure of any
 * kind (wrong key, edited bytes, truncated) is reported as "absent": the caller asks the user again.
 */
#ifndef NEUPOD_CRYPTO_SECRET_H
#define NEUPOD_CRYPTO_SECRET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SECRET_OVERHEAD 29u /**< bytes added to the plaintext */

/**
 * THE key provider: the one function to replace when the key moves to OTP or a secure element (app/crypto/key_provider_chipid.c).
 * Fills @p key (32 bytes). False if no key can be produced (then nothing is encrypted or decrypted).
 */
bool key_provider_get(uint8_t key[32]);

/** Encrypt @p n bytes into @p out (capacity @p cap >= n + SECRET_OVERHEAD). Returns the record length, 0 on failure. */
size_t secret_seal(uint32_t aad_tag, const uint8_t *plain, size_t n, uint8_t *out, size_t cap);
/** Decrypt a record; returns the plaintext length, or -1 if it does not authenticate (absent). */
int secret_open(uint32_t aad_tag, const uint8_t *rec, size_t n, uint8_t *out, size_t cap);

/** The library keeps one small static arena: serialise callers with a mutex (the runtime installs an os mutex at start). */
typedef void (*secret_lock_fn)(void);
void secret_set_lock(secret_lock_fn lock, secret_lock_fn unlock);

/** Overwrite @p n bytes in a way the compiler will not remove (plaintext secrets on the stack). */
void secret_wipe(void *p, size_t n);

/** Tests: forget the cached key so the provider is asked again. */
void secret_drop_cached_key(void);

#ifdef __cplusplus
}
#endif

#endif

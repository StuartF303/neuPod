/* mbed TLS (Apache-2.0 option) configuration for neuPod: only what secrets-at-rest needs: AES-256-GCM, SHA-256, HKDF.
 * No TLS, no PSA, no entropy collector, no heap beyond what these use (none). */
#ifndef NEUPOD_MBEDTLS_CONFIG_H
#define NEUPOD_MBEDTLS_CONFIG_H
#define MBEDTLS_PLATFORM_C
/* Allocation from a tiny static arena (crypto_secret.c): the library allocates a few small contexts per operation, and the
 * firmware has no spare general heap on this path. */
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO neupod_crypto_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO neupod_crypto_free
#include <stddef.h>
void *neupod_crypto_calloc(size_t n, size_t size);
void neupod_crypto_free(void *p);
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_MD_C
#define MBEDTLS_HKDF_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_NO_PLATFORM_ENTROPY
#endif

// SPDX-License-Identifier: MPL-2.0
// Key provider for the testbed: HKDF-SHA256 over the chip's unique ID and a fixed application salt.
//
// THIS IS NOT A SECRET KEY. The chip ID is readable through a debugger and is usually exposed as the USB serial. It protects a
// lost microSD card on its own (nothing readable off the card without this board), not a card together with its board.
// To move the key into OTP or a secure element, replace this file only (docs/wifi.md).
#include <string.h>

#include "crypto_secret.h"
#include "hal_system.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

bool key_provider_get(uint8_t key[32]) {
    uint8_t id[8];
    if (!hal_chip_id(id)) {
        return false;
    }
    static const uint8_t salt[] = "neuPod secrets v1 2026";
    static const uint8_t info[] = "AES-256-GCM settings key";
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md != NULL && mbedtls_hkdf(md, salt, sizeof salt - 1, id, sizeof id, info, sizeof info - 1, key, 32) == 0;
}

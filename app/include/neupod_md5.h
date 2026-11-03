// SPDX-License-Identifier: MPL-2.0
/** @file neupod_md5.h  MD5 (RFC 1321) for the FLAC verify mode: the STREAMINFO checksum is an MD5 of the decoded samples. Not for security. */
#ifndef NEUPOD_MD5_H
#define NEUPOD_MD5_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t s[4];
    uint64_t bytes;
    uint8_t buf[64];
} md5_t;

void md5_init(md5_t *m);
void md5_update(md5_t *m, const void *data, size_t n);
void md5_final(md5_t *m, uint8_t out[16]);

#ifdef __cplusplus
}
#endif

#endif

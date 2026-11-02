// SPDX-License-Identifier: MPL-2.0
#include "wav.h"

#include <stdbool.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

const char *wav_err_str(wav_err_t e) {
    switch (e) {
    case WAV_OK: return "ok";
    case WAV_E_READ: return "Cannot read the file header";
    case WAV_E_NOT_RIFF: return "Not a WAV file";
    case WAV_E_NO_FMT: return "WAV has no format chunk";
    case WAV_E_BAD_FMT: return "WAV format chunk is damaged";
    case WAV_E_NO_DATA: return "WAV has no audio data";
    case WAV_E_NOT_PCM: return "Only integer PCM WAV is supported";
    case WAV_E_CHANNELS: return "Only mono or stereo WAV is supported";
    case WAV_E_BITS: return "Only 16, 24 or 32-bit WAV is supported";
    case WAV_E_RATE: return "Only 44.1, 48, 88.2 or 96 kHz WAV is supported";
    }
    return "?";
}

wav_err_t wav_parse(wav_read_fn rd, void *ctx, uint64_t file_size, wav_info_t *out) {
    uint8_t h[12];
    if (rd(ctx, 0, h, 12) != 12) {
        return WAV_E_READ;
    }
    if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
        return WAV_E_NOT_RIFF;
    }
    bool have_fmt = false;
    uint32_t tag = 0, rate = 0, align = 0, bits = 0, ch = 0;
    uint64_t pos = 12;
    for (int guard = 0; guard < 64; guard++) { // bound the chunk walk
        uint8_t ck[8];
        if (pos + 8 > file_size || rd(ctx, pos, ck, 8) != 8) {
            return have_fmt ? WAV_E_NO_DATA : WAV_E_NO_FMT;
        }
        uint64_t size = le32(ck + 4);
        uint64_t body = pos + 8;
        if (memcmp(ck, "fmt ", 4) == 0) {
            uint8_t f[40];
            if (size < 16) {
                return WAV_E_BAD_FMT;
            }
            uint32_t n = size < sizeof f ? (uint32_t)size : (uint32_t)sizeof f;
            if (rd(ctx, body, f, n) != (int32_t)n) {
                return WAV_E_BAD_FMT;
            }
            tag = le16(f);
            ch = le16(f + 2);
            rate = le32(f + 4);
            align = le16(f + 12);
            bits = le16(f + 14);
            if (tag == 0xFFFEu) { // WAVE_FORMAT_EXTENSIBLE: the real tag is the first two bytes of the sub-format GUID
                if (size < 40 || n < 40) {
                    return WAV_E_BAD_FMT;
                }
                tag = le16(f + 24);
            }
            have_fmt = true;
        } else if (memcmp(ck, "data", 4) == 0) {
            if (!have_fmt) {
                return WAV_E_NO_FMT;
            }
            if (tag != 1u) {
                return WAV_E_NOT_PCM;
            }
            if (ch < 1 || ch > 2) {
                return WAV_E_CHANNELS;
            }
            if (bits != 16 && bits != 24 && bits != 32) {
                return WAV_E_BITS;
            }
            if (rate != 44100u && rate != 48000u && rate != 88200u && rate != 96000u) {
                return WAV_E_RATE;
            }
            if (align != ch * bits / 8u) {
                return WAV_E_BAD_FMT;
            }
            uint64_t avail = file_size > body ? file_size - body : 0;
            if (size == 0 || size > avail) {
                size = avail; // streaming writers leave 0 / 0xFFFFFFFF; a truncated file plays what is there
            }
            size -= size % align;
            if (size == 0) {
                return WAV_E_NO_DATA;
            }
            out->rate = rate;
            out->channels = (uint8_t)ch;
            out->bits = (uint8_t)bits;
            out->block_align = align;
            out->data_offset = body;
            out->data_bytes = size;
            out->frames = size / align;
            return WAV_OK;
        }
        pos = body + size + (size & 1u); // chunks are padded to an even size
    }
    return WAV_E_NO_DATA;
}

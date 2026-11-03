// SPDX-License-Identifier: MPL-2.0
// HOST TEST TOOL ONLY (never part of the firmware): encodes raw little-endian interleaved PCM with Apple's reference
// ALAC encoder (Apache-2.0, this directory) so the decoder tests have real ALAC data.
//   alacenc in.raw rate channels bits out.pk
// out.pk = u32 cookie_len, cookie bytes, then packets as [u32 len][bytes], all little-endian integers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ALACEncoder.h"
#include "ALACAudioTypes.h"

int main(int argc, char **argv) {
    if (argc != 6) return 2;
    FILE *in = fopen(argv[1], "rb");
    FILE *out = fopen(argv[5], "wb");
    uint32_t rate = (uint32_t)atoi(argv[2]), ch = (uint32_t)atoi(argv[3]), bits = (uint32_t)atoi(argv[4]);
    if (!in || !out) return 3;
    AudioFormatDescription inf, outf;
    memset(&inf, 0, sizeof inf);
    memset(&outf, 0, sizeof outf);
    inf.mSampleRate = rate;
    inf.mFormatID = kALACFormatLinearPCM;
    inf.mFormatFlags = 12; // kALACFormatFlagsNativeEndian | kALACFormatFlagIsSignedInteger-like; the encoder only reads bit depth
    inf.mChannelsPerFrame = ch;
    inf.mBitsPerChannel = bits;
    inf.mBytesPerFrame = ch * bits / 8;
    inf.mBytesPerPacket = inf.mBytesPerFrame;
    inf.mFramesPerPacket = 1;
    outf.mSampleRate = rate;
    outf.mFormatID = kALACFormatAppleLossless;
    outf.mFormatFlags = bits == 16 ? 1 : bits == 20 ? 2 : bits == 24 ? 3 : 4;
    outf.mFramesPerPacket = kALACDefaultFrameSize;
    outf.mChannelsPerFrame = ch;
    ALACEncoder enc;
    enc.SetFrameSize(kALACDefaultFrameSize);
    enc.InitializeEncoder(outf);
    uint32_t cs = enc.GetMagicCookieSize(ch);
    uint8_t *cookie = (uint8_t *)calloc(cs, 1);
    enc.GetMagicCookie(cookie, &cs);
    fwrite(&cs, 4, 1, out);
    fwrite(cookie, 1, cs, out);
    uint32_t inbytes = ch * (bits >> 3) * kALACDefaultFrameSize;
    uint8_t *rb = (uint8_t *)calloc(inbytes, 1), *wb = (uint8_t *)calloc(inbytes + kALACMaxEscapeHeaderBytes + 64, 1);
    for (;;) {
        size_t got = fread(rb, 1, inbytes, in);
        if (got == 0) break;
        int32_t n = (int32_t)got;
        enc.Encode(inf, inf, rb, wb, &n);
        uint32_t len = (uint32_t)n;
        fwrite(&len, 4, 1, out);
        fwrite(wb, 1, len, out);
        if (got < inbytes) break;
    }
    fclose(out);
    return 0;
}

#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Builds tests/assets/audio/flac16_44k_st_seek.flac: flac16_44k_st.flac with a SEEKTABLE block (one point per FLAC frame; the
vector uses a fixed block size), for the resume-seek tests. No encoder needed:
    python3 tools/gen_seek_vector.py tests/assets/audio
"""
import struct, sys, os

out = sys.argv[1]
d = open(os.path.join(out, "flac16_44k_st.flac"), "rb").read()


def crc8(b):
    c = 0
    for x in b:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


# metadata blocks
pos, blocks = 4, []
while True:
    h = d[pos]
    n = int.from_bytes(d[pos + 1:pos + 4], "big")
    blocks.append((h & 0x7F, d[pos + 4:pos + 4 + n]))
    pos += 4 + n
    if h & 0x80:
        break
audio = d[pos:]
blk = int.from_bytes(blocks[0][1][0:2], "big")  # fixed block size: min == max
assert blk == int.from_bytes(blocks[0][1][2:4], "big")


def frame_at(a, i):
    """Frame number if a valid FLAC frame header (CRC-8 checked) starts at a[i], else None."""
    if a[i] != 0xFF or a[i + 1] & 0xFE != 0xF8:
        return None
    bs_code, rate_code = a[i + 2] >> 4, a[i + 2] & 0xF
    j = i + 4
    first = a[j]
    ln = 1 if first < 0x80 else (2 if first < 0xE0 else 3 if first < 0xF0 else 4 if first < 0xF8 else 5)
    num = first & (0x7F if ln == 1 else (0x3F >> (ln - 1)))
    for k in range(1, ln):
        num = (num << 6) | (a[j + k] & 0x3F)
    j += ln
    j += 1 if bs_code == 6 else 2 if bs_code == 7 else 0
    j += 1 if rate_code == 12 else 2 if rate_code in (13, 14) else 0
    return num if a[j] == crc8(a[i:j]) else None


points, i = [], 0
while i < len(audio) - 8:
    n = frame_at(audio, i)
    if n is not None and (not points or n == points[-1][0] + 1):
        points.append((n, i))
        i += 8
    else:
        i += 1
assert len(points) >= 3, points
table = b"".join(struct.pack(">QQH", n * blk, off, blk) for n, off in points)
res = bytearray(b"fLaC")
res += bytes([0x00]) + (34).to_bytes(3, "big") + blocks[0][1]            # STREAMINFO
res += bytes([0x03]) + len(table).to_bytes(3, "big") + table            # SEEKTABLE
for k, (t, body) in enumerate(blocks[1:]):
    last = 0x80 if k == len(blocks) - 2 else 0
    res += bytes([last | t]) + len(body).to_bytes(3, "big") + body
res += audio
open(os.path.join(out, "flac16_44k_st_seek.flac"), "wb").write(res)
print("seek points:", [(n * blk, off) for n, off in points])

#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Builds the ALAC / M4A test vectors in tests/assets/audio/ from Apple's reference ALAC encoder (built from
tests/tools/alac_encoder, host test tool only) and a small hand-written MP4 muxer (also exercised by the demuxer tests).
    python3 tools/gen_alac_vectors.py <alacenc binary> tests/assets/audio     (needs numpy: uv run --no-project --with numpy)
Writes <name>.m4a and <name>.md5 (MD5 of the source samples, FLAC layout, plus rate/channels/bits/frames)."""
import hashlib, struct, subprocess, sys, os, tempfile
import numpy as np

enc, out = sys.argv[1], sys.argv[2]
rng = np.random.default_rng(777)

def tone(rate, secs, ch, bits):
    n = int(rate * secs)
    t = np.arange(n) / rate
    sig = 0.35 * np.sin(2 * np.pi * 523 * t) + 0.15 * np.sin(2 * np.pi * 1999 * t) + 0.03 * rng.standard_normal(n)
    sig = np.clip(sig, -0.999, 0.999)
    if ch == 2:
        sig = np.stack([sig, 0.6 * np.roll(sig, 11)], axis=1)
    return np.round(sig * (2 ** (bits - 1) - 1)).astype(np.int64)

def md5_of(x, bits):
    b = (bits + 7) // 8
    return hashlib.md5(b"".join(int(v).to_bytes(b, "little", signed=True) for v in x.reshape(-1))).hexdigest()

def box(t, body):
    return struct.pack(">I", 8 + len(body)) + t + body

def full(t, ver_flags, body):
    return box(t, struct.pack(">I", ver_flags) + body)

def text_item(t, s):
    d = box(b"data", struct.pack(">II", 1, 0) + s.encode())
    return box(t, d)

def build(packets, cookie, rate, ch, bits, nframes, tags, co64, per_chunk):
    sizes = [len(p) for p in packets]
    chunks = [sizes[i:i + per_chunk] for i in range(0, len(sizes), per_chunk)]
    stsz = full(b"stsz", 0, struct.pack(">II", 0, len(sizes)) + b"".join(struct.pack(">I", s) for s in sizes))
    runs = [(1, per_chunk)]
    if len(chunks[-1]) != per_chunk:
        runs.append((len(chunks), len(chunks[-1])))
    stsc = full(b"stsc", 0, struct.pack(">I", len(runs)) + b"".join(struct.pack(">III", f, n, 1) for f, n in runs))
    stts = full(b"stts", 0, struct.pack(">III", 1, len(packets), 4096))
    entry = b"\0" * 6 + struct.pack(">H", 1) + struct.pack(">HHIHHHHI", 0, 0, 0, ch, bits, 0, 0, rate << 16) + full(b"alac", 0, cookie)
    stsd = full(b"stsd", 0, struct.pack(">I", 1) + box(b"alac", entry))
    def moov(offsets):
        stco = full(b"co64" if co64 else b"stco", 0, struct.pack(">I", len(offsets)) + b"".join(struct.pack(">Q" if co64 else ">I", o) for o in offsets))
        stbl = box(b"stbl", stsd + stts + stsc + stsz + stco)
        minf = box(b"minf", full(b"smhd", 0, b"\0" * 4) + box(b"dinf", b"") + stbl)
        mdhd = full(b"mdhd", 0, struct.pack(">IIIIHH", 0, 0, rate, nframes, 0x55C4, 0))
        hdlr = full(b"hdlr", 0, b"\0" * 4 + b"soun" + b"\0" * 12 + b"\0")
        mdia = box(b"mdia", mdhd + hdlr + minf)
        tkhd = full(b"tkhd", 7, b"\0" * 80)
        trak = box(b"trak", tkhd + mdia)
        mvhd = full(b"mvhd", 0, b"\0" * 96)
        ilst = b""
        if tags:
            ilst = box(b"ilst", text_item(b"\xa9nam", tags[0]) + text_item(b"\xa9ART", tags[1]) + text_item(b"\xa9alb", tags[2]) +
                       box(b"trkn", box(b"data", struct.pack(">II", 0, 0) + struct.pack(">HHHH", 0, tags[3], 12, 0))))
        meta = full(b"meta", 0, full(b"hdlr", 0, b"\0" * 4 + b"mdir" + b"\0" * 13) + ilst)
        udta = box(b"udta", meta) if tags else b""
        return box(b"moov", mvhd + trak + udta)
    ftyp = box(b"ftyp", b"M4A " + struct.pack(">I", 0) + b"M4A mp42isom")
    free = box(b"free", b"\0" * 24)
    m0 = moov([0] * len(chunks))
    base = len(ftyp) + len(free) + len(m0) + 8  # first byte of mdat payload
    offsets, pos = [], base
    for c in chunks:
        offsets.append(pos)
        pos += sum(c)
    m1 = moov(offsets)
    assert len(m1) == len(m0)
    return ftyp + free + m1 + box(b"mdat", b"".join(packets))

vectors = [("alac16_44k_st", 44100, 0.35, 2, 16, True, False, 5), ("alac24_48k_st", 48000, 0.3, 2, 24, False, True, 3),
           ("alac16_48k_mono", 48000, 0.2, 1, 16, False, False, 4)]
for name, rate, secs, ch, bits, tags, co64, per_chunk in vectors:
    x = tone(rate, secs, ch, bits)
    with tempfile.TemporaryDirectory() as td:
        raw, pk = os.path.join(td, "in.raw"), os.path.join(td, "out.pk")
        bytes_ = bits // 8
        with open(raw, "wb") as f:
            f.write(b"".join(int(v).to_bytes(bytes_, "little", signed=True) for v in x.reshape(-1)))
        subprocess.check_call([enc, raw, str(rate), str(ch), str(bits), pk])
        d = open(pk, "rb").read()
    cl = struct.unpack("<I", d[:4])[0]
    cookie = d[4:4 + cl]
    pos, packets = 4 + cl, []
    while pos < len(d):
        ln = struct.unpack("<I", d[pos:pos + 4])[0]
        packets.append(d[pos + 4:pos + 4 + ln])
        pos += 4 + ln
    m4a = build(packets, cookie, rate, ch, bits, len(x), ("Test Title é", "Test Artist", "Test Album", 7) if tags else None, co64, per_chunk)
    open(os.path.join(out, name + ".m4a"), "wb").write(m4a)
    open(os.path.join(out, name + ".md5"), "w").write(md5_of(x, bits) + " %d %d %d %d\n" % (rate, ch, bits, len(x)))
    print(name, len(packets), "packets", len(m4a), "bytes")

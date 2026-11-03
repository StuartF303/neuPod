#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Builds the encoder-delay test vectors for ALAC / M4A in tests/assets/audio/ from an existing vector (no encoder needed, no numpy):
    python3 tools/gen_trim_vectors.py tests/assets/audio
It re-muxes the packets of alac16_48k_mono.m4a (9600 frames) with
  alac16_48k_mono_elst.m4a  an edit list (media_time 2112, segment_duration 125 ms in a 1000 Hz movie timescale = 6000 frames), and
  alac16_48k_mono_smpb.m4a  an iTunSMPB tag (priming 0x840 = 2112, padding 0x5D0 = 1488, length 0x1770 = 6000 frames), no edit list.
Both must decode to frames [2112, 8112) of the untrimmed file (tests/test_codecs.c)."""
import struct, sys, os

out = sys.argv[1]
src = open(os.path.join(out, "alac16_48k_mono.m4a"), "rb").read()


def boxes(d, pos, end):
    while pos + 8 <= end:
        size, typ = struct.unpack(">I4s", d[pos:pos + 8])
        yield typ, pos + 8, pos + size
        pos += size


def find(d, path, pos=0, end=None):
    end = len(d) if end is None else end
    for typ, b, e in boxes(d, pos, end):
        if typ == path[0]:
            return (b, e) if len(path) == 1 else find(d, path[1:], b, e)
    raise KeyError(path)


b, e = find(src, [b"moov", b"trak", b"mdia", b"minf", b"stbl"])
stbl = {t: (bb, ee) for t, bb, ee in boxes(src, b, e)}
n_samples = struct.unpack(">I", src[stbl[b"stsz"][0] + 8:stbl[b"stsz"][0] + 12])[0]
sizes = [struct.unpack(">I", src[stbl[b"stsz"][0] + 12 + 4 * i:stbl[b"stsz"][0] + 16 + 4 * i])[0] for i in range(n_samples)]
sb = stbl[b"stsc"][0]
runs = [struct.unpack(">II", src[sb + 8 + 12 * i:sb + 16 + 12 * i]) for i in range(struct.unpack(">I", src[sb + 4:sb + 8])[0])]
cb = stbl[b"stco"][0]
chunks = [struct.unpack(">I", src[cb + 8 + 4 * i:cb + 12 + 4 * i])[0] for i in range(struct.unpack(">I", src[cb + 4:cb + 8])[0])]
packets, idx = [], 0
for c, off in enumerate(chunks):
    per = [n for f, n in runs if f <= c + 1][-1]
    for _ in range(per):
        if idx < n_samples:
            packets.append(src[off:off + sizes[idx]])
            off += sizes[idx]
            idx += 1
sd = stbl[b"stsd"][0]
child = sd + 8 + 8 + 28  # stsd: version/flags + count, the sample entry's size + fourcc, its 28 fixed bytes; then the 'alac' child box
csize, ctyp = struct.unpack(">I4s", src[child:child + 8])
assert ctyp == b"alac"
cookie = src[child + 12:child + csize]  # after the box's version/flags
rate, ch, bits, nframes = 48000, 1, 16, 9600


def box(t, body):
    return struct.pack(">I", 8 + len(body)) + t + body


def full(t, vf, body):
    return box(t, struct.pack(">I", vf) + body)


def build(edit, smpb):
    chunks_ = [sizes[i:i + 4] for i in range(0, len(sizes), 4)]
    stsz = full(b"stsz", 0, struct.pack(">II", 0, len(sizes)) + b"".join(struct.pack(">I", s) for s in sizes))
    stsc = full(b"stsc", 0, struct.pack(">I", 1) + struct.pack(">III", 1, 4, 1))
    stts = full(b"stts", 0, struct.pack(">III", 1, len(packets), 4096))
    entry = b"\0" * 6 + struct.pack(">H", 1) + struct.pack(">HHIHHHHI", 0, 0, 0, ch, bits, 0, 0, rate << 16) + full(b"alac", 0, cookie)
    stsd = full(b"stsd", 0, struct.pack(">I", 1) + box(b"alac", entry))

    def moov(offsets):
        stco = full(b"stco", 0, struct.pack(">I", len(offsets)) + b"".join(struct.pack(">I", o) for o in offsets))
        stbl_ = box(b"stbl", stsd + stts + stsc + stsz + stco)
        minf = box(b"minf", full(b"smhd", 0, b"\0" * 4) + box(b"dinf", b"") + stbl_)
        mdhd = full(b"mdhd", 0, struct.pack(">IIIIHH", 0, 0, rate, nframes, 0x55C4, 0))
        hdlr = full(b"hdlr", 0, b"\0" * 4 + b"soun" + b"\0" * 12 + b"\0")
        mdia = box(b"mdia", mdhd + hdlr + minf)
        tkhd = full(b"tkhd", 7, b"\0" * 80)
        edts = b""
        if edit:  # one real edit: play 125 ms (movie timescale 1000) starting 2112 frames into the media
            edts = box(b"edts", full(b"elst", 0, struct.pack(">I", 1) + struct.pack(">IiI", 125, 2112, 0x10000)))
        trak = box(b"trak", tkhd + edts + mdia)
        mvhd = full(b"mvhd", 0, struct.pack(">IIII", 0, 0, 1000, 200) + b"\0" * 80)
        udta = b""
        if smpb:
            text = " 00000000 00000840 000005D0 0000000000001770 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000"
            item = box(b"----", full(b"mean", 0, b"com.apple.iTunes") + full(b"name", 0, b"iTunSMPB") +
                       box(b"data", struct.pack(">II", 1, 0) + text.encode()))
            meta = full(b"meta", 0, full(b"hdlr", 0, b"\0" * 4 + b"mdir" + b"\0" * 13) + box(b"ilst", item))
            udta = box(b"udta", meta)
        return box(b"moov", mvhd + trak + udta)

    ftyp = box(b"ftyp", b"M4A " + struct.pack(">I", 0) + b"M4A mp42isom")
    free = box(b"free", b"\0" * 24)
    m0 = moov([0] * len(chunks_))
    pos = len(ftyp) + len(free) + len(m0) + 8
    offsets = []
    for c in chunks_:
        offsets.append(pos)
        pos += sum(c)
    m1 = moov(offsets)
    assert len(m1) == len(m0)
    return ftyp + free + m1 + box(b"mdat", b"".join(packets))


open(os.path.join(out, "alac16_48k_mono_elst.m4a"), "wb").write(build(True, False))
open(os.path.join(out, "alac16_48k_mono_smpb.m4a"), "wb").write(build(False, True))
print("wrote alac16_48k_mono_elst.m4a and alac16_48k_mono_smpb.m4a from", len(packets), "packets")

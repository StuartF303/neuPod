#!/usr/bin/env python3
"""Make tests/assets/art/flac_with_jpeg.flac and flac_with_png.flac: the repository's 16/44.1 FLAC test vector with a PICTURE block
added after STREAMINFO (a front-cover JPEG, or a PNG). Used by the album-art host tour. Needs Pillow for the PNG.
    python3 tests/tools/make_art_flacs.py
"""
import io
import os
import struct
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "assets", "audio", "flac16_44k_st.flac")
ART = os.path.join(HERE, "..", "assets", "art")


def picture_block(mime, data, w, h):
    body = struct.pack(">I", 3) + struct.pack(">I", len(mime)) + mime + struct.pack(">I", 0)
    body += struct.pack(">IIII", w, h, 24, 0) + struct.pack(">I", len(data)) + data
    return body


def build(name, mime, data, w, h):
    f = open(SRC, "rb").read()
    assert f[:4] == b"fLaC"
    # STREAMINFO is the first block; clear its "last" flag if set, then append the picture as the last metadata block of the group
    # right after STREAMINFO and keep the rest (comments, padding, audio) as it was.
    first_len = int.from_bytes(f[5:8], "big")
    si_end = 4 + 4 + first_len
    first = bytearray(f[4:si_end])
    was_last = bool(first[0] & 0x80)
    first[0] &= 0x7F
    body = picture_block(mime, data, w, h)
    hdr = bytes([6 | (0x80 if was_last else 0)]) + len(body).to_bytes(3, "big")
    out = f[:4] + bytes(first) + hdr + body + f[si_end:]
    open(os.path.join(ART, name), "wb").write(out)
    print(name, len(out), "bytes")


jpg = open(os.path.join(ART, "q420_500x333.jpg"), "rb").read()
build("flac_with_jpeg.flac", b"image/jpeg", jpg, 500, 333)
png = io.BytesIO()
Image.new("RGB", (64, 64), (200, 30, 30)).save(png, "PNG")
build("flac_with_png.flac", b"image/png", png.getvalue(), 64, 64)

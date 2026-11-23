#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Regenerates mobile/test/fixtures/art/: small FLAC and MP3 files with embedded
pictures, cover files, and expected.tsv with what the firmware itself makes of
them (app/audio/meta.c's choice of embedded picture, app/audio/art.c's order of
sources, cover file names and JPEG decoder, compiled for the host with
scripts/app/art_vectors.c). The app's test test/data/scan/art_vectors_test.dart
checks its Dart port against it.

The pictures are made here: PNGs written directly, JPEGs converted from them
with macOS `sips` (baseline, 4:2:0), a progressive and an oversized one made by
editing a baseline JPEG's frame header (the firmware refuses both from the
header alone).

Usage: scripts/app/gen_art_vectors.py   (macOS: needs cc, sips and python3)
"""

import os
import struct
import subprocess
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "mobile", "test", "fixtures", "art")
AUDIO = os.path.join(REPO, "tests", "assets", "audio")


def png(w, h, colour):
    """An RGB PNG of w x h: colour(x, y) -> (r, g, b)."""
    raw = b"".join(b"\0" + b"".join(bytes(colour(x, y)) for x in range(w)) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def split(a, b):
    """Two colours split on the diagonal, so a picture is told apart from another (and from its mirror)."""
    return lambda x, y: a if x > y else b


def jpeg(png_bytes, tmp, name):
    src = os.path.join(tmp, name + ".png")
    dst = os.path.join(tmp, name + ".jpg")
    open(src, "wb").write(png_bytes)
    subprocess.run(["sips", "-s", "format", "jpeg", "-s", "formatOptions", "80", src, "--out", dst],
                   check=True, capture_output=True)
    return open(dst, "rb").read()


def segments(j):
    """(offset of the marker, marker byte, length) of each segment before the scan."""
    i, out = 2, []
    while i + 4 <= len(j):
        m, n = j[i + 1], int.from_bytes(j[i + 2:i + 4], "big")
        out.append((i, m, n))
        if m == 0xDA:
            break
        i += 2 + n
    return out


def with_sof(j, marker=None, size=None):
    """The JPEG with its SOF0 turned into another SOF marker, or its declared size changed."""
    b = bytearray(j)
    for at, m, _ in segments(j):
        if m == 0xC0:
            if marker is not None:
                b[at + 1] = marker
            if size is not None:
                b[at + 5:at + 7] = size[1].to_bytes(2, "big")
                b[at + 7:at + 9] = size[0].to_bytes(2, "big")
            return bytes(b)
    raise SystemExit("no SOF0")


def flac(pictures):
    """fLaC + the fixture's STREAMINFO + a few comments + PICTURE blocks [(type, mime, description, data)]."""
    src = open(os.path.join(AUDIO, "flac16_48k_mono.flac"), "rb").read()
    si_len = int.from_bytes(src[5:8], "big")
    si = src[8:8 + si_len]
    blocks = [(0, si)]
    body = struct.pack("<I", 6) + b"neuPod" + struct.pack("<I", 1)
    c = b"TITLE=Art"
    body += struct.pack("<I", len(c)) + c
    blocks.append((4, body))
    for ptype, mime, desc, data in pictures:
        p = struct.pack(">I", ptype) + struct.pack(">I", len(mime)) + mime + struct.pack(">I", len(desc)) + desc
        p += struct.pack(">IIII", 48, 48, 24, 0) + struct.pack(">I", len(data)) + data
        blocks.append((6, p))
    out = bytearray(b"fLaC")
    for i, (t, d) in enumerate(blocks):
        out += bytes([t | (0x80 if i == len(blocks) - 1 else 0)]) + len(d).to_bytes(3, "big") + d
    return bytes(out) + src[8 + si_len:8 + si_len + 64]


def syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def apic(version, ptype, data, enc=0, mime=b"image/jpeg", desc=b"", flags=b"\0\0", dli=False):
    """An APIC frame (v2.3 / v2.4), or PIC (v2.2, mime then the 3-letter format)."""
    if version == 2:
        payload = bytes([enc]) + mime[:3] + bytes([ptype]) + desc + (b"\0\0" if enc in (1, 2) else b"\0") + data
        return b"PIC" + len(payload).to_bytes(3, "big") + payload
    payload = bytes([enc]) + mime + b"\0" + bytes([ptype]) + desc + (b"\0\0" if enc in (1, 2) else b"\0") + data
    if dli:
        payload = syncsafe(len(payload)) + payload
    size = syncsafe(len(payload)) if version == 4 else struct.pack(">I", len(payload))
    return b"APIC" + size + flags + payload


def mp3(frames, version, tag_flags=0):
    body = b"".join(frames) + bytes(32)
    tag = b"ID3" + bytes([version, 0, tag_flags]) + syncsafe(len(body)) + body
    audio = open(os.path.join(AUDIO, "mp3_44k_st.mp3"), "rb").read()[:3000]
    return tag + audio


def main():
    os.makedirs(OUT, exist_ok=True)
    for old in os.listdir(OUT):
        if old.endswith((".flac", ".mp3", ".jpg", ".png", ".tsv")):
            os.remove(os.path.join(OUT, old))
    with tempfile.TemporaryDirectory() as tmp:
        red_png = png(48, 48, split((200, 40, 40), (250, 220, 200)))
        blue_png = png(48, 48, split((30, 60, 160), (200, 220, 250)))
        green_png = png(40, 30, split((40, 150, 60), (230, 240, 200)))
        red = jpeg(red_png, tmp, "red")
        blue = jpeg(blue_png, tmp, "blue")
        green = jpeg(green_png, tmp, "green")
        progressive = with_sof(blue, marker=0xC2)
        too_big = with_sof(red, size=(6000, 48))
        gif = b"GIF89a" + bytes(32)

        files = {
            # FLAC PICTURE blocks: meta_art_offer keeps the best (JPEG 4, PNG 2, other 1; +1 for a front cover), the first
            # on a tie; the image's first bytes say what it is, not the mime string.
            "front_jpeg.flac": flac([(3, b"image/jpeg", b"", red)]),
            "png_front_then_jpeg_back.flac": flac([(3, b"image/png", b"", red_png), (4, b"image/jpeg", b"", blue)]),
            "jpeg_back_then_front.flac": flac([(4, b"image/jpeg", b"", blue), (3, b"image/jpeg", b"cover", red)]),
            "two_fronts.flac": flac([(3, b"image/jpeg", b"", red), (3, b"image/jpeg", b"", blue)]),
            "png_only.flac": flac([(3, b"image/png", b"", red_png)]),
            "progressive.flac": flac([(3, b"image/jpeg", b"", progressive)]),
            "wrong_mime.flac": flac([(0, b"image/png", b"", green)]),
            "no_picture.flac": flac([]),
            "short_picture.flac": flac([(3, b"image/jpeg", b"", b"\xff\xd8\xff")]),
            "too_big.flac": flac([(3, b"image/jpeg", b"", too_big)]),
            "gif_only.flac": flac([(3, b"image/gif", b"", gif)]),
            "gif_front_then_png_other.flac": flac([(3, b"image/gif", b"", gif), (0, b"image/png", b"", blue_png)]),
            # ID3v2 APIC / PIC.
            "apic_v23_front.mp3": mp3([apic(3, 3, red)], 3),
            "apic_v24_dli.mp3": mp3([apic(4, 3, blue, enc=3, desc=b"front", flags=b"\0\x01", dli=True)], 4),
            "pic_v22.mp3": mp3([apic(2, 3, green, mime=b"JPG")], 2),
            "apic_utf16_desc.mp3": mp3([apic(3, 3, red, enc=1, desc=b"\xff\xfe" + "Cover".encode("utf-16-le"))], 3),
            "apic_long_desc.mp3": mp3([apic(3, 3, red, desc=b"d" * 200)], 3),
            "apic_png_then_jpeg_other.mp3": mp3([apic(3, 3, red_png, mime=b"image/png"), apic(3, 0, blue)], 3),
            "apic_compressed.mp3": mp3([apic(3, 3, red, flags=b"\0\x80")], 3),
            "apic_unsync_tag.mp3": mp3([apic(3, 3, red)], 3, tag_flags=0x80),
        }
        images = {
            # Cover files in a folder: decoded as a whole file.
            "cover.jpg": red,
            "folder_blue.jpg": blue,
            "progressive.jpg": progressive,
            "too_big.jpg": too_big,
            "png_named.jpg": red_png,
            "truncated.jpg": red[: len(red) // 2],
        }
        names = [
            "cover.jpg", "COVER.JPG", "Cover.Jpg", "folder.jpg", "Folder.jpg", "FOLDER.JPG", "front.jpg", "Front.JPG",
            "cover.jpeg", "cover.png", "Folder.png", "folder.gif", "AlbumArtSmall.jpg", "albumart.jpg", " cover.jpg",
            "cover.jpg ", "cover.jpg.bak", "back.jpg", "çover.jpg", "ｃover.jpg", "",
        ]
        listings = [
            "Folder.jpg|front.jpg",
            "front.jpg|Folder.jpg|cover.jpg",
            "folder.jpg|Folder.jpg",
            "Folder.jpg|folder.jpg",
            "a.flac|b.png|AlbumArt.jpg",
            "FRONT.JPG|x.flac",
            "cover.png|Folder.jpg",
            "Cover.jpg|cover.jpg",
        ]

        paths = []
        for name, data in {**files, **images}.items():
            p = os.path.join(OUT, name)
            open(p, "wb").write(data)
            paths.append(p)
        exe = os.path.join(tmp, "art_vectors")
        subprocess.run(
            [
                "cc", "-std=c11", "-O1",
                "-I" + os.path.join(REPO, "app", "include"),
                "-I" + os.path.join(REPO, "app", "third_party", "tjpgd"),
                "-I" + os.path.join(REPO, "app", "os"),
                "-o", exe,
                os.path.join(HERE, "art_vectors.c"),
                os.path.join(REPO, "app", "audio", "meta.c"),
                os.path.join(REPO, "app", "audio", "art.c"),
                os.path.join(REPO, "app", "third_party", "tjpgd", "tjpgd.c"),
                os.path.join(REPO, "app", "os", "os_alloc_malloc.c"),
            ],
            check=True,
        )

        def run(mode, args):
            return subprocess.run([exe, mode] + args, check=True, capture_output=True).stdout.decode()

        tags = run("tags", [os.path.join(OUT, n) for n in files])
        imgs = run("image", [os.path.join(OUT, n) for n in images])
        ranks = run("rank", names)
        picks = run("pick", listings)

    with open(os.path.join(OUT, "expected.tsv"), "w") as f:
        f.write("# Generated by scripts/app/gen_art_vectors.py from app/audio/meta.c and app/audio/art.c; do not edit.\n")
        f.write("# tags: name, offset, length, kind, type, decode, sources\n")
        for line in tags.splitlines():
            f.write("tags\t" + line + "\n")
        f.write("# image: name, decode\n")
        for line in imgs.splitlines():
            f.write("image\t" + line + "\n")
        f.write("# rank: name, rank\n")
        for line in ranks.splitlines():
            f.write("rank\t" + line + "\n")
        f.write("# pick: listing (| between names), rank, name picked\n")
        for line in picks.splitlines():
            f.write("pick\t" + line + "\n")


if __name__ == "__main__":
    main()

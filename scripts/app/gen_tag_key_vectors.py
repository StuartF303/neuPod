#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Regenerates mobile/test/fixtures/tag_keys/: small tagged FLAC and MP3 files
built from the firmware's freely licensed fixtures (tests/assets/audio), and
expected.tsv with what the firmware itself makes of each one (its tag parser
app/audio/meta.c and the song key of app/library/lib_build.c, compiled for the
host with scripts/app/tag_key_vectors.c). The app's test
test/data/scan/tag_key_vectors_test.dart checks its Dart port against it
(owner decision 11: the app sorts exactly as the firmware does).

Usage: scripts/app/gen_tag_key_vectors.py   (needs cc and python3)
"""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "mobile", "test", "fixtures", "tag_keys")
AUDIO = os.path.join(REPO, "tests", "assets", "audio")


def flac(comments_raw, last_pad=0):
    """fLaC + the fixture's STREAMINFO + a VORBIS_COMMENT of raw byte comments
    (None for no comment block), then a few frame bytes."""
    src = open(os.path.join(AUDIO, "flac16_48k_mono.flac"), "rb").read()
    assert src[:4] == b"fLaC"
    si_len = int.from_bytes(src[5:8], "big")
    si = src[8 : 8 + si_len]
    out = bytearray(b"fLaC")
    if comments_raw is None:
        out += bytes([0x80]) + si_len.to_bytes(3, "big") + si
    else:
        out += bytes([0x00]) + si_len.to_bytes(3, "big") + si
        body = bytearray(struct.pack("<I", 6) + b"neuPod" + struct.pack("<I", len(comments_raw)))
        for c in comments_raw:
            body += struct.pack("<I", len(c)) + c
        out += bytes([0x84]) + len(body).to_bytes(3, "big") + body
    return bytes(out) + src[8 + si_len : 8 + si_len + 64]


def flac_truncated(comments_raw):
    """fLaC, STREAMINFO, a VORBIS_COMMENT, then a block that is not the last and runs past the end of the file: the
    parse fails after the tags were read."""
    src = open(os.path.join(AUDIO, "flac16_48k_mono.flac"), "rb").read()
    si_len = int.from_bytes(src[5:8], "big")
    si = src[8 : 8 + si_len]
    out = bytearray(b"fLaC") + bytes([0x00]) + si_len.to_bytes(3, "big") + si
    body = bytearray(struct.pack("<I", 6) + b"neuPod" + struct.pack("<I", len(comments_raw)))
    for c in comments_raw:
        body += struct.pack("<I", len(c)) + c
    out += bytes([0x04]) + len(body).to_bytes(3, "big") + body
    out += bytes([0x01]) + (1000).to_bytes(3, "big") + bytes(16)  # PADDING, said to be 1000 bytes
    return bytes(out)


def syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def frame(fid, enc, payload, version):
    data = bytes([enc]) + payload
    size = syncsafe(len(data)) if version == 4 else struct.pack(">I", len(data))
    return fid.encode() + size + b"\0\0" + data


def mp3(frames, version):
    body = b"".join(frames) + bytes(32)
    tag = b"ID3" + bytes([version, 0, 0]) + syncsafe(len(body)) + body
    audio = open(os.path.join(AUDIO, "mp3_44k_st.mp3"), "rb").read()[:3000]
    return tag + audio


def mp3_v22(frames):
    """An ID3v2.2 tag (three-letter ids, 24-bit sizes) of (id, enc, payload) frames."""
    body = b""
    for fid, enc, payload in frames:
        data = bytes([enc]) + payload
        body += fid.encode() + len(data).to_bytes(3, "big") + data
    body += bytes(32)
    tag = b"ID3" + bytes([2, 0, 0]) + syncsafe(len(body)) + body
    audio = open(os.path.join(AUDIO, "mp3_44k_st.mp3"), "rb").read()[:3000]
    return tag + audio


def u16le(s):
    return b"\xff\xfe" + s.encode("utf-16-le")


def u16be(s):
    return s.encode("utf-16-be")


CASES = {
    "utf8_sort.flac": flac([b"TITLE=The \xc3\x89clair", b"TITLESORT=Eclair, The"]),
    # A title past the 63-byte buffer, with a two-byte character across the cut.
    "long_title.flac": flac([b"TITLE=" + b"a" * 62 + "é".encode() + b"tail"]),
    # Malformed UTF-8: a Latin-1 byte, a lead byte without its continuation, a stray continuation.
    "malformed.flac": flac([b"TITLE=Caf\xe9 \xc3( \x80x", b"TITLESORT=S\xe9rt"]),
    # A comment block over the 4096-byte scratch: the firmware drops its tags (the key uses the file name).
    "Big Comments.flac": flac([b"TITLE=Ignored By Device", b"COMMENT=" + b"x" * 5000]),
    "The Untagged Ümlaut.flac".encode().decode(): flac(None),
    # UTF-16 with a BOM: an emoji (a surrogate pair) and a lone high surrogate become '?' in the firmware.
    "utf16_pairs.mp3": mp3(
        [
            frame("TIT2", 1, u16le("Mood \U0001f3b5 x") + b"\x3d\xd8\x41\x00", 3),
            frame("TSOT", 1, u16le("Mood"), 3),
        ],
        3,
    ),
    "utf16be_latin1.mp3": mp3(
        [frame("TIT2", 2, u16be("Łódź"), 4), frame("TSOT", 0, b"Lodz \xe9", 4)],
        4,
    ),
    "latin1.mp3": mp3([frame("TIT2", 0, b"Se\xf1or \xd7 2", 3)], 3),
    "long_sort.mp3": mp3(
        [frame("TIT2", 3, b"Title", 4), frame("TSOT", 3, b"s" * 30 + "ß".encode() + b"zz", 4)],
        4,
    ),
    # Artist and album: UTF-16 emoji (surrogate pairs) that differ only in the emoji. The firmware stores both as
    # "? Band" / "? Hits", so they are one artist and one album there; a lone low surrogate in the album is '?' too.
    "utf16_band_a.mp3": mp3(
        [
            frame("TIT2", 1, u16le("Smile"), 3),
            frame("TPE1", 1, u16le("\U0001f600 Band"), 3),
            frame("TALB", 1, u16le("\U0001f600 Hits") + b"\x00\xdc", 3),
        ],
        3,
    ),
    "utf16_band_b.mp3": mp3(
        [
            frame("TIT2", 1, u16le("Grin"), 3),
            frame("TPE1", 1, u16le("\U0001f603 Band"), 3),
            frame("TALB", 1, u16le("\U0001f603 Hits") + b"\x00\xdc", 3),
        ],
        3,
    ),
    # Malformed UTF-8 in the artist and album (copied raw by the firmware), and an artist past the 63-byte buffer.
    "malformed_artist.flac": flac([b"TITLE=Bad", b"ARTIST=Caf\xe9 \xc3( \x80x", b"ALBUM=Alb\xff\xfe" + b"u" * 70]),
    # UTF-16BE artist and a Latin-1 album.
    "utf16be_artist.mp3": mp3(
        [frame("TIT2", 3, b"Plain", 4), frame("TPE1", 2, u16be("\u0141\u00f3d\u017a"), 4), frame("TALB", 0, b"S\xe9r", 4)],
        4,
    ),
    # The library tags (phase 3). The owner's "Back to Mine": each track's ARTIST is the performer, ALBUMARTIST the
    # compiler, COMPILATION=1; numbers with totals; a full date.
    "back_to_mine.flac": flac(
        [
            b"TITLE=Take Me Away",
            b"ARTIST=Annette Peacock",
            b"ALBUM=Back to Mine",
            b"ALBUMARTIST=Morcheeba",
            b"COMPILATION=1",
            b"TRACKNUMBER=3/18",
            b"DISCNUMBER=1/2",
            b"DATE=2001-05-04",
            b"GENRE=Trip-Hop",
        ]
    ),
    # Keys in any case; "ALBUM ARTIST" and ALBUMARTIST are the same field and the later one stands; DISC is DISCNUMBER;
    # YEAR then DATE: the later stands even when it is not a year (0); a flag that starts with "y"; a bare genre
    # number stays text in Vorbis comments.
    "keys_last_wins.flac": flac(
        [
            b"title=Order",
            b"Artist=Track Artist",
            b"ALBUM ARTIST=First Album Artist",
            b"albumartist=Second Album Artist",
            b"DISC=2",
            b"YEAR=1999",
            b"DATE=19",
            b"COMPILATION=yes",
            b"GENRE=17",
        ]
    ),
    # Sort tags: ALBUMARTISTSORT keys the album artist; ARTISTSORT is ignored when there is an album artist tag.
    "sorts_aa.flac": flac(
        [
            b"TITLE=S1",
            b"ARTIST=The Artist",
            b"ARTISTSORT=Artist Sort",
            b"ALBUMARTIST=The Album Artist",
            b"ALBUMARTISTSORT=Album Artist, The",
            b"ALBUM=The Album",
            b"ALBUMSORT=Album, The",
            b"DISCNUMBER=300",
            b"COMPILATION=0",
        ]
    ),
    # No album artist tag: ARTISTSORT keys the artist; the album artist is the artist; "Various Artists" in any case
    # is a compilation without the flag; a track number with a leading space is 0.
    "sorts_artist.flac": flac(
        [b"TITLE=S2", b"ARTIST=various ARTISTS", b"ARTISTSORT=Zzz", b"TRACKNUMBER= 4", b"GENRE=(17)Custom"]
    ),
    # A comment block over the scratch: no album artist, flag, numbers or genre on the device.
    "big_library_tags.flac": flac(
        [b"ALBUMARTIST=Lost", b"COMPILATION=1", b"DISCNUMBER=2", b"DATE=2000", b"GENRE=Lost", b"LYRICS=" + b"x" * 5000]
    ),
    # ID3v2.3: TPE2, TPOS with a total, TYER, TCMP, TCON as an ID3v1 reference, the three sort frames.
    "id3v23_library.mp3": mp3(
        [
            frame("TIT2", 0, b"Rider", 3),
            frame("TPE1", 0, b"Guest", 3),
            frame("TALB", 0, b"Collected", 3),
            frame("TPE2", 1, u16le("Host É"), 3),
            frame("TRCK", 0, b"07/12", 3),
            frame("TPOS", 0, b"2/3", 3),
            frame("TYER", 0, b"1987", 3),
            frame("TCMP", 0, b"1", 3),
            frame("TCON", 0, b"(17)", 3),
            frame("TSOP", 0, b"Guest, The", 3),
            frame("TSOA", 0, b"Collected, The", 3),
            frame("TSO2", 0, b"Host", 3),
        ],
        3,
    ),
    # ID3v2.4: TDRC with a time, a bare genre number (ID3 allows it), a remix reference, TCMP 0.
    "id3v24_library.mp3": mp3(
        [
            frame("TIT2", 3, b"Late", 4),
            frame("TPE1", 3, b"Solo", 4),
            frame("TDRC", 3, b"2019-05-04T08:00:00Z", 4),
            frame("TCON", 3, b"18", 4),
            frame("TCMP", 3, b"0", 4),
            frame("TPOS", 3, b"1", 4),
        ],
        4,
    ),
    # ID3v2.4 genre forms: "(RX)" (a remix, not a genre), and a TYER after TDRC (the later stands).
    "id3v24_genre_rx.mp3": mp3(
        [frame("TIT2", 3, b"Mix", 4), frame("TCON", 3, b"(RX)", 4), frame("TDRC", 3, b"2001", 4), frame("TYER", 3, b"2002", 4)],
        4,
    ),
    # "((" is a literal "(", and "(17)(18)": the first reference stands.
    "genre_paren.mp3": mp3([frame("TIT2", 0, b"Paren", 3), frame("TCON", 0, b"((Not a ref)", 3)], 3),
    "genre_two_refs.mp3": mp3([frame("TIT2", 0, b"Refs", 3), frame("TCON", 0, b"(17)(18)", 3)], 3),
    # A genre past the 31-byte buffer, with a two-byte character across the cut; a genre number past the table.
    "genre_long.flac": flac([b"TITLE=Long Genre", b"GENRE=" + b"g" * 30 + "é".encode() + b"tail"]),
    "genre_unknown_ref.mp3": mp3([frame("TIT2", 0, b"Unknown Ref", 3), frame("TCON", 0, b"(200)", 3)], 3),
    # A parse that fails part-way keeps what it read (index_file takes the tags whatever the parse returned): a FLAC
    # whose blocks run past the end after its comments, and an MP3 whose tag is followed by no audio frame.
    "truncated.flac": flac_truncated(
        [b"TITLE=Cut Short", b"ARTIST=Kept", b"ALBUMARTIST=Kept Too", b"TRACKNUMBER=4", b"DATE=1990", b"GENRE=Blues"]
    ),
    "no_frames.mp3": (
        b"ID3"
        + bytes([3, 0, 0])
        + syncsafe(len(frame("TIT2", 0, b"Tag Only", 3) + frame("TPE2", 0, b"Host", 3)) + 32)
        + frame("TIT2", 0, b"Tag Only", 3)
        + frame("TPE2", 0, b"Host", 3)
        + bytes(32)
        + b"not audio" * 40
    ),
    # ID3v2.2: TP2, TPA, TYE, TCP, TCO, TS2.
    "id3v22_library.mp3": mp3_v22(
        [
            ("TT2", 0, b"Old"),
            ("TP1", 0, b"Old Guest"),
            ("TAL", 0, b"Old Album"),
            ("TP2", 0, b"Old Host"),
            ("TPA", 0, b"3"),
            ("TYE", 0, b"1979"),
            ("TCP", 0, b"1"),
            ("TCO", 0, b"Jazz"),
            ("TS2", 0, b"Host, Old"),
        ]
    ),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for old in os.listdir(OUT):
        if old.endswith((".flac", ".mp3", ".tsv")):
            os.remove(os.path.join(OUT, old))
    paths = []
    for name, data in CASES.items():
        p = os.path.join(OUT, name)
        open(p, "wb").write(data)
        paths.append(p)
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "tag_key_vectors")
        subprocess.run(
            [
                "cc", "-std=c11", "-O1",
                "-I" + os.path.join(REPO, "app", "include"),
                "-I" + os.path.join(REPO, "app", "third_party", "fatfs"),
                "-o", exe,
                os.path.join(HERE, "tag_key_vectors.c"),
                os.path.join(REPO, "app", "audio", "meta.c"),
                os.path.join(REPO, "app", "library", "lib_collate.c"),
            ],
            check=True,
        )
        out = subprocess.run([exe] + paths, check=True, capture_output=True).stdout.decode()
    with open(os.path.join(OUT, "expected.tsv"), "w") as f:
        f.write("# Generated by scripts/app/gen_tag_key_vectors.py from app/audio/meta.c and app/library; do not edit.\n")
        f.write(
            "# file\ttitle\tsort_title\tsong key\tartist\talbum\talbum_artist\tsort_artist\tsort_album\tsort_album_artist\t"
            "genre\ttrack\tdisc\tyear\tcompilation flag\taa\tsort_aa\tcomp\tartist key\talbum key\tduration s"
            " (texts and keys in hex)\n"
        )
        f.write(out)
    print(f"wrote {len(paths)} cases to {OUT}")


if __name__ == "__main__":
    sys.exit(main())

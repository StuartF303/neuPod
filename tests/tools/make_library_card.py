#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A fake card with a small tagged music library, for the host tours and tests (docs/adr/0017-music-library-index.md, L2).

usage: make_library_card.py OUTDIR AUDIO_ASSETS_DIR [--many N] [--playlists]

Writes OUTDIR/Music/<artist>/<album>/NN Title.<ext>. The FLAC files are the real (0.3 s) test vectors from AUDIO_ASSETS_DIR with their tags
replaced, so they decode and play; one album is silent 3 s WAVs (no tags, long enough for a gapless handoff); one track is an AAC .m4a header
(listed, greyed out: the player has no AAC decoder). --many N adds N more FLAC files under /Music/Bulk for a scan-speed run.
"""
import os
import shutil
import struct
import sys


def le32(v):
    return struct.pack("<I", v)


def be32(v):
    return struct.pack(">I", v)


def vorbis_comment(tags):
    items = [f"{k}={v}".encode() for k, v in tags if v not in (None, "")]
    body = le32(0) + le32(len(items)) + b"".join(le32(len(i)) + i for i in items)
    return body


def flac_with_tags(src, tags):
    d = open(src, "rb").read()
    assert d[:4] == b"fLaC"
    p = 4
    streaminfo = None
    while True:
        h = d[p]
        ln = int.from_bytes(d[p + 1:p + 4], "big")
        if (h & 127) == 0:
            streaminfo = d[p + 4:p + 4 + ln]
        p += 4 + ln
        if h & 128:
            break
    vc = vorbis_comment(tags)
    out = b"fLaC" + bytes([0]) + (34).to_bytes(3, "big") + streaminfo
    out += bytes([0x80 | 4]) + len(vc).to_bytes(3, "big") + vc
    return out + d[p:]


def id3_frame(fid, text):
    if not text:
        return b""
    body = b"\x00" + text.encode("latin-1", "replace")
    return fid.encode() + be32(len(body)) + b"\x00\x00" + body


def mp3_with_tags(src, tags):
    d = open(src, "rb").read()
    if d[:3] == b"ID3":
        size = (d[6] << 21) | (d[7] << 14) | (d[8] << 7) | d[9]
        d = d[10 + size:]
    t = dict(tags)
    frames = b"".join(id3_frame(k, t.get(n)) for k, n in (("TIT2", "TITLE"), ("TPE1", "ARTIST"), ("TALB", "ALBUM"), ("TPE2", "ALBUMARTIST"),
                                                         ("TCON", "GENRE"), ("TRCK", "TRACKNUMBER")))
    n = len(frames)
    hdr = b"ID3\x03\x00\x00" + bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])
    return hdr + frames + d


def wav_silent(seconds):
    data = 44100 * 4 * seconds
    return (b"RIFF" + le32(36 + data) + b"WAVEfmt " + le32(16) + struct.pack("<HHIIHH", 1, 2, 44100, 44100 * 4, 4, 16) + b"data" + le32(data) +
            bytes(data))


def box(t, body):
    return be32(8 + len(body)) + t + body


def m4a_aac(tags):
    t = dict(tags)

    def text(typ, v):
        if not v:
            return b""
        return box(typ, box(b"data", be32(1) + be32(0) + v.encode()))

    ilst = text(b"\xa9nam", t.get("TITLE")) + text(b"\xa9ART", t.get("ARTIST")) + text(b"\xa9alb", t.get("ALBUM")) + text(b"\xa9gen", t.get("GENRE"))
    if t.get("TRACKNUMBER"):
        ilst += box(b"trkn", box(b"data", be32(0) + be32(0) + struct.pack(">HHHH", 0, int(t["TRACKNUMBER"]), 12, 0)))
    meta = box(b"meta", be32(0) + box(b"ilst", ilst))
    udta = box(b"udta", meta)
    stsd = box(b"stsd", be32(0) + be32(1) + box(b"mp4a", bytes(28)))
    stbl = box(b"stbl", stsd)
    minf = box(b"minf", stbl)
    hdlr = box(b"hdlr", be32(0) + be32(0) + b"soun" + bytes(12) + b"\x00")
    mdia = box(b"mdia", hdlr + minf)
    trak = box(b"trak", mdia)
    moov = box(b"moov", trak + udta)
    return box(b"ftyp", b"M4A \x00\x00\x00\x00M4A mp42isom") + moov


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def main():
    out, assets = sys.argv[1], sys.argv[2]
    many = int(sys.argv[sys.argv.index("--many") + 1]) if "--many" in sys.argv else 0
    shutil.rmtree(out, ignore_errors=True)  # every run starts from a card with no index and no saved settings
    music = os.path.join(out, "Music")
    f44 = os.path.join(assets, "flac16_44k_st.flac")
    f48 = os.path.join(assets, "flac16_48k_mono.flac")
    mp3 = os.path.join(assets, "mp3_44k_st.mp3")

    def flac(rel, src, title, artist, album, genre, track, aartist=None, comp=False):
        tags = [("TITLE", title), ("ARTIST", artist), ("ALBUM", album), ("ALBUMARTIST", aartist), ("GENRE", genre), ("TRACKNUMBER", str(track)),
                ("COMPILATION", "1" if comp else None)]
        put(os.path.join(music, rel), flac_with_tags(src, tags))

    # Aurora Lane: two albums, Pop (First Light's tracks are named in file order that is not track order)
    flac("Aurora Lane/First Light/02 Morning Rain.flac", f44, "Morning Rain", "Aurora Lane", "First Light", "Pop", 2)
    flac("Aurora Lane/First Light/01 Opening.flac", f44, "Opening", "Aurora Lane", "First Light", "Pop", 1)
    flac("Aurora Lane/First Light/03 Harbour Lights.flac", f48, "Harbour Lights", "Aurora Lane", "First Light", "Pop", 3)
    flac("Aurora Lane/Second Wind/01 Tailwind.flac", f44, "Tailwind", "Aurora Lane", "Second Wind", "Pop", 1)
    flac("Aurora Lane/Second Wind/02 Café Del Mar.flac", f44, "Café Del Mar", "Aurora Lane", "Second Wind", "Electronic", 2)
    # Beacon: one album with an AAC track (greyed out) and a very long title
    flac("Beacon/Signal/01 Pulse.flac", f48, "Pulse", "Beacon", "Signal", "Electronic", 1)
    put(os.path.join(music, "Beacon/Signal/02 Carrier Wave.m4a"),
        m4a_aac([("TITLE", "Carrier Wave"), ("ARTIST", "Beacon"), ("ALBUM", "Signal"), ("GENRE", "Electronic"), ("TRACKNUMBER", "2")]))
    flac("Beacon/Signal/03 An Unreasonably Long Song Title That Will Not Fit On One Line.flac", f44,
         "An Unreasonably Long Song Title That Will Not Fit On One Line", "Beacon", "Signal", "Electronic", 3)
    # The Cinders: Rock, an MP3, and an album with a title too long for one line
    flac("Cinders/Ash/01 Smoulder.flac", f44, "Smoulder", "The Cinders", "An Unreasonably Long Album Title That Will Not Fit Either", "Rock", 1)
    put(os.path.join(music, "Cinders/Ash/02 Ember.mp3"),
        mp3_with_tags(mp3, [("TITLE", "Ember"), ("ARTIST", "The Cinders"), ("ALBUM", "An Unreasonably Long Album Title That Will Not Fit Either"),
                            ("GENRE", "Rock"), ("TRACKNUMBER", "2")]))
    # cover files for three albums (the Cover Flow shows pictures for them and placeholders for the rest); the picture assets are next to the audio ones
    art = os.path.join(os.path.dirname(assets.rstrip("/")), "art")
    for rel, jpg in (("Aurora Lane/First Light", "q420_600x600.jpg"), ("Aurora Lane/Second Wind", "q444_128x128.jpg"), ("Beacon/Signal", "q422_517x389.jpg")):
        with open(os.path.join(art, jpg), "rb") as f:
            put(os.path.join(music, rel, "cover.jpg"), f.read())
    # a compilation
    flac("Various/Now 7/01 Hit A.flac", f44, "Hit A", "Zed Zed", "Now 7", None, 1, comp=True)
    flac("Various/Now 7/02 Hit B.flac", f48, "Hit B", "Quartz", "Now 7", None, 2, comp=True)
    # untagged: title is the file name, artist Unknown Artist, album the folder name
    put(os.path.join(music, "Loose/loose song.mp3"), mp3_with_tags(mp3, []))
    # three silent 3 s WAVs: a gapless album that plays long enough to see the handoffs (the library tour checks them in the log)
    for i in (1, 2, 3):
        put(os.path.join(music, f"Longplay/0{i}.wav"), wav_silent(3))
    if "--playlists" in sys.argv:
        # the Playlists tour (LB-7): On-The-Go seeded (three songs and one that was deleted), a playlist file with relative paths and bad entries in /Playlists,
        # and one at the root with a UTF-8 byte-order mark and CRLF line ends
        put(os.path.join(out, ".neupod/otg.m3u8"),
            ("#EXTM3U\n#EXTINF:3,Longplay - Track One\n/Music/Longplay/01.wav\n#EXTINF:3,Longplay - Track Two\n/Music/Longplay/02.wav\n"
             "#EXTINF:215,Aurora Lane - Caf\u00e9 Del Mar\n/Music/Aurora Lane/Second Wind/02 Caf\u00e9 Del Mar.flac\n"
             "#EXTINF:1,Gone - Deleted\n/Music/Gone/Deleted.wav\n# end\n").encode())
        put(os.path.join(out, "Playlists/Night drive.m3u"),
            ("#EXTM3U\n#EXTINF:3,Aurora Lane - Opening\n../Music/Aurora Lane/First Light/01 Opening.flac\n#EXTINF:3,Beacon - Pulse\n"
             "../Music/Beacon/Signal/01 Pulse.flac\n../Music/Beacon/Signal/02 Carrier Wave.m4a\n../Music/Gone/Missing Song.flac\nhttp://example.com/stream.mp3\n").encode())
        put(os.path.join(out, "Running.m3u8"),
            ("\ufeff#EXTM3U\r\n#EXTINF:3,Quiet Room - First Silence\r\n/Music/Longplay/01.wav\r\n/Music/Longplay/02.wav\r\n/Music/Longplay/03.wav\r\n").encode())
    for i in range(many):
        flac(f"Bulk/Artist {i // 50:03d}/Album {i // 10:04d}/{i % 10:02d} Song {i}.flac", f44, f"Song {i}", f"Artist {i // 50:03d}",
             f"Album {i // 10:04d}", ("Pop", "Rock", "Jazz")[i % 3], i % 10 + 1)


if __name__ == "__main__":
    main()

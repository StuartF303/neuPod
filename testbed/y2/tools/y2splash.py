#!/usr/bin/env python3
"""Convert a PNG to the Y2I1 splash format read by `fbtool image` (stdlib only; run with python3 -I).

    python3 -I testbed/y2/tools/y2splash.py convert in.png out.y2i
    python3 -I testbed/y2/tools/y2splash.py info file.y2i

Y2I1 layout (little endian):
    "Y2I1"  u16 width  u16 height  u16 palette_size (1..256)
    palette_size * (R, G, B)
    (run u8 1..255, index u8) pairs covering exactly width*height pixels, row-major, nothing after.
The splash is a vertical gradient, so every row is one or two flat runs and plain RLE gives ~3 KB.
The decoder here is as strict as fbtool's and is the reference the tests compare fbtool against.
"""
import struct
import sys
import zlib

MAGIC = b"Y2I1"
MAX_DIM = 4096


class FormatError(ValueError):
    pass


def encode(width, height, rgb):
    """rgb: bytes of width*height*3. Returns the Y2I1 file as bytes."""
    if not (1 <= width <= MAX_DIM and 1 <= height <= MAX_DIM) or len(rgb) != width * height * 3:
        raise FormatError("bad dimensions")
    pal, idx = {}, bytearray()
    for i in range(0, len(rgb), 3):
        c = rgb[i:i + 3]
        if c not in pal:
            if len(pal) == 256:
                raise FormatError("more than 256 colours; reduce the palette first")
            pal[c] = len(pal)
        idx.append(pal[c])
    out = bytearray(MAGIC + struct.pack("<HHH", width, height, len(pal)))
    for c in pal:  # dicts keep insertion order = index order
        out += c
    i = 0
    while i < len(idx):
        j = i
        while j < len(idx) and idx[j] == idx[i] and j - i < 255:
            j += 1
        out += bytes((j - i, idx[i]))
        i = j
    return bytes(out)


def decode(data):
    """Strict decode. Returns (width, height, rgb bytes). Raises FormatError."""
    if len(data) < 10 or data[:4] != MAGIC:
        raise FormatError("bad magic")
    w, h, n = struct.unpack_from("<HHH", data, 4)
    if not (1 <= w <= MAX_DIM and 1 <= h <= MAX_DIM):
        raise FormatError("bad dimensions")
    if not 1 <= n <= 256:
        raise FormatError("bad palette size")
    pos = 10 + 3 * n
    if len(data) < pos:
        raise FormatError("truncated palette")
    pal = [data[10 + 3 * i:13 + 3 * i] for i in range(n)]
    total, out = w * h, bytearray()
    while len(out) < total * 3:
        if len(data) - pos < 2:
            raise FormatError("truncated pixel data")
        run, ix = data[pos], data[pos + 1]
        pos += 2
        if run == 0:
            raise FormatError("zero-length run")
        if ix >= n:
            raise FormatError("palette index out of range")
        if len(out) + run * 3 > total * 3:
            raise FormatError("pixel data overruns image")
        out += pal[ix] * run
    if pos != len(data):
        raise FormatError("trailing bytes")
    return w, h, bytes(out)


# ---- minimal PNG reader / writer (8-bit, non-interlaced) -----------------------------------
def read_png(data):
    """Returns (width, height, rgb bytes); alpha is composited onto black."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise FormatError("not a PNG")
    pos, idat, plte, hdr = 8, bytearray(), None, None
    while pos + 8 <= len(data):
        ln, typ = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
    if hdr is None:
        raise FormatError("no IHDR")
    w, h, depth, ctype, _, _, interlace = hdr
    if depth != 8 or interlace:
        raise FormatError("only 8-bit non-interlaced PNGs are supported")
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if ch is None or (ctype == 3 and plte is None):
        raise FormatError("unsupported PNG colour type")
    raw, stride, prev, rows = zlib.decompress(bytes(idat)), w * ch, bytearray(w * ch), []
    if len(raw) != (stride + 1) * h:
        raise FormatError("PNG data size mismatch")
    for y in range(h):
        ft, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b = prev[i]
            c = prev[i - ch] if i >= ch else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif ft != 0:
                raise FormatError("bad PNG filter")
        rows.append(line)
        prev = line
    out = bytearray()
    for line in rows:
        for x in range(w):
            px = line[x * ch:(x + 1) * ch]
            if ctype == 0:
                out += bytes((px[0],) * 3)
            elif ctype == 2:
                out += px
            elif ctype == 3:
                out += plte[px[0] * 3:px[0] * 3 + 3]
            elif ctype == 4:
                out += bytes((px[0] * px[1] // 255,) * 3)
            else:
                out += bytes(v * px[3] // 255 for v in px[:3])
    return w, h, bytes(out)


def write_png(width, height, rgb):
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    raw = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(argv):
    if len(argv) == 4 and argv[1] == "convert":
        with open(argv[2], "rb") as fh:
            w, h, rgb = read_png(fh.read())
        blob = encode(w, h, rgb)
        assert decode(blob) == (w, h, rgb)  # never write a file that does not round-trip
        with open(argv[3], "wb") as fh:
            fh.write(blob)
        print(f"{argv[3]}: {w}x{h}, {len(blob)} bytes")
        return 0
    if len(argv) == 3 and argv[1] == "info":
        with open(argv[2], "rb") as fh:
            w, h, _ = decode(fh.read())
        print(f"{w}x{h}")
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))

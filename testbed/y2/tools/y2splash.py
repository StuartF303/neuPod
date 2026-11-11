#!/usr/bin/env python3
"""Convert a PNG to the Y2I1 splash format read by `fbtool image` (stdlib only; run with python3 -I).

    python3 -I testbed/y2/tools/y2splash.py convert in.png out.y2i
    python3 -I testbed/y2/tools/y2splash.py info file.y2i
    python3 -I testbed/y2/tools/y2splash.py smooth in.png out.png
    python3 -I testbed/y2/tools/y2splash.py quantize in.png out.png

`quantize` is the colour step shared with the LK logo (tools/y2logo.py): every pixel is rounded ONCE to the nearest
colour RGB565 can hold, as LK expands it (see below), with no dithering. The result is the canonical splash: it is
both what the stock LK shows from the LOGO partition and what fbtool draws.

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

# ---- RGB565 as the stock LK shows it ------------------------------------------------------
# LK (lk.bin of y2_v3.2.0, thumb function at file offset 0x1c314, the 32 bpp branch of the pixel writer) expands a
# 565 pixel x to the framebuffer word  0xff000000 | (x & 0xf800) << 8 | (x & 0x07e0) << 5 | (x << 3) & 0xff,
# i.e. a PLAIN SHIFT: R = r5 << 3, G = g6 << 2, B = b5 << 3, low bits zero (no bit replication). So the
# brightest value LK can show is (248, 252, 248), never 255.
# Two separate steps, so the result is stable:
#   1. quantize (8-bit master -> 565): q = round(v * max / 255), the usual scaling. A master that was itself rendered
#      as 565 and expanded by bit replication (the host simulator's snapshots) comes back EXACTLY, so nothing is lost.
#   2. expand_565 (565 -> the canonical 8-bit PNG): LK's plain shift. Every pixel of the canonical PNG has its low
#      bits zero, and `exact_565` reads it back losslessly (no second rounding).
def quantize_channel(v, bits):
    top = (1 << bits) - 1
    return (v * top + 127) // 255


def exact_565(rgb):
    """canonical RGB888 (low bits zero, i.e. a shift-expanded 565 image) -> list of 565 values. Raises FormatError for
    any pixel LK could not show exactly (the caller must quantize first)."""
    out = []
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
        if r & 7 or g & 3 or b & 7:
            raise FormatError(f"pixel {i // 3} ({r},{g},{b}) is not an RGB565 colour as LK expands it; "
                              "run `y2splash.py quantize` (or pass --quantize) first")
        out.append(r >> 3 << 11 | g >> 2 << 5 | b >> 3)
    return out


def rgb_to_565(rgb):
    """bytes RGB888 (any 8-bit image) -> list of 565 values, rounded, no dithering."""
    out = []
    for i in range(0, len(rgb), 3):
        out.append(quantize_channel(rgb[i], 5) << 11 | quantize_channel(rgb[i + 1], 6) << 5
                   | quantize_channel(rgb[i + 2], 5))
    return out


def expand_565(values):
    """list of 565 values -> RGB888 bytes, the way LK expands them (plain shift)."""
    out = bytearray()
    for v in values:
        out += bytes(((v >> 11 & 31) << 3, (v >> 5 & 63) << 2, (v & 31) << 3))
    return bytes(out)


def quantize_rgb(rgb):
    """The canonical colour step: any RGB888 -> the RGB888 LK would show (quantize, then LK's plain-shift expand)."""
    return expand_565(rgb_to_565(rgb))


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


def smooth_background(width, height, rgb):
    """Replace the vertical background gradient by one smooth linear ramp between the first and last
    row's colours. A pixel is background if it equals its row's left-edge pixel; every other pixel
    (the wordmark and its anti-aliased edge) is kept exactly. Idempotent."""
    top, bot = rgb[:3], rgb[(height - 1) * width * 3:(height - 1) * width * 3 + 3]
    out = bytearray(rgb)
    for y in range(height):
        base = y * width * 3
        old = rgb[base:base + 3]
        new = bytes((t * (height - 1) + (b - t) * y + (height - 1) // 2) // (height - 1)
                    for t, b in zip(top, bot)) if height > 1 else old
        for x in range(width):
            if rgb[base + x * 3:base + x * 3 + 3] == old:
                out[base + x * 3:base + x * 3 + 3] = new
    return bytes(out)


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
    if len(argv) == 4 and argv[1] == "smooth":
        with open(argv[2], "rb") as fh:
            w, h, rgb = read_png(fh.read())
        with open(argv[3], "wb") as fh:
            fh.write(write_png(w, h, smooth_background(w, h, rgb)))
        return 0
    if len(argv) == 4 and argv[1] == "quantize":
        with open(argv[2], "rb") as fh:
            w, h, rgb = read_png(fh.read())
        q = quantize_rgb(rgb)
        with open(argv[3], "wb") as fh:
            fh.write(write_png(w, h, q))
        print(f"{argv[3]}: {w}x{h}, {len({q[i:i + 3] for i in range(0, len(q), 3)})} colours")
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

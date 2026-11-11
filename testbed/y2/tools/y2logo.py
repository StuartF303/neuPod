#!/usr/bin/env python3
"""Inspect and rebuild the Innioasis Y2 LOGO image (logo.bin, the 3 MiB LOGO partition holds it). Stdlib only; run
with python3 -I.

    y2logo.py info    [--partition] FILE                    strict parse + validate (exit 1 and a reason if anything is off)
    y2logo.py replace STOCK.bin SPLASH.png OUT.bin [--quantize] [--force]
    y2logo.py extract FILE OUTDIR                           480x360 frames as PNG, small sprites as raw .rgb565

Format (verified against y2_v3.2.0 logo.bin, 102,593 bytes, byte-identical to the device's LOGO):
    512-byte MTK header: magic bytes 88 16 88 58, u32le payload size, name[32] = "LOGO" + NULs, 0xFF to the end.
    payload: u32le count (39), u32le total (= payload size), count x u32le offsets (relative to the payload start,
    the first is 8 + 4 * count), then the images back to back: each is a zlib stream (78 da) of RGB565 little
    endian pixels, top row first. Image i ends where image i+1 starts; the last ends at total.
Images (sizes are checked): 480x360 frames at 0-3, 25-35, 37, 38 (345,600 bytes); small sprites 4-24, 36. Image 0
is the picture LK shows at power-on and image 38 is the same picture (byte-identical) for the kernel's boot logo.
Image 37 is the 100 % battery frame of the charging animation, NOT a logo. `replace` swaps 0 and 38 only and keeps
every other image's compressed bytes exactly.

Colours. LK expands 565 with a plain shift (see y2splash.py): the picture must already be a 565-exact image, i.e.
the canonical splash produced by `y2splash.py quantize`. `replace` refuses any other PNG unless --quantize is given
(then the same rounding, no dithering, is applied for you). The output is re-parsed and every replaced image is
decoded back and compared with the input before the file is written; the output may not exceed the LOGO partition.
"""
import hashlib
import importlib.util
import os
import struct
import sys
import zlib

HEADER_LEN = 512
MAGIC = bytes.fromhex("88168858")   # the first four bytes of the file (an MTK partition header magic)
NAME = b"LOGO"
COUNT = 39
PARTITION_SIZE = 0x300000          # LOGO in the scatter file (physical 0x4400000, size 0x300000)
W, H = 480, 360
FRAME = W * H * 2
LOGO_INDICES = (0, 38)             # the two innioasis pictures (LK power-on and kernel boot logo); NOT 37 (battery full)
# decompressed size of every image in the stock file; anything else is rejected
SIZES = ([FRAME] * 4 + [600] * 10 + [720] + [2184] * 10 + [FRAME] * 11 + [156] + [FRAME] * 2)
assert len(SIZES) == COUNT


class LogoError(ValueError):
    pass


def _splash():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "y2splash.py")
    spec = importlib.util.spec_from_file_location("y2splash", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def parse(data, partition=False):
    """Strict parse. Returns (payload_size, blobs) where blobs are the compressed images as stored.
    partition=True accepts a full partition dump: whatever follows the payload (stale data or filler) is ignored."""
    data = bytes(data)
    if len(data) < HEADER_LEN + 8:
        raise LogoError(f"file is {len(data)} bytes, shorter than a header and a table")
    (size,) = struct.unpack_from("<I", data, 4)
    if data[:4] != MAGIC:
        raise LogoError(f"bad magic {data[:4].hex()}, expected {MAGIC.hex()}")
    if data[8:40] != NAME + bytes(28):
        raise LogoError(f"header name is {data[8:40].rstrip(bytes(1))!r}, expected {NAME!r}")
    if data[40:HEADER_LEN] != b"\xff" * (HEADER_LEN - 40):
        raise LogoError("header padding is not all 0xFF")
    avail = len(data) - HEADER_LEN
    if size > avail or size < 8 + 4 * COUNT:
        raise LogoError(f"header payload size {size} does not fit the file ({avail} bytes after the header)")
    tail = data[HEADER_LEN + size:]
    if tail:
        if not partition:
            raise LogoError(f"{len(tail)} bytes after the payload (header size {size}); use --partition for a partition dump")
    p = data[HEADER_LEN:HEADER_LEN + size]
    count, total = struct.unpack_from("<II", p, 0)
    if count != COUNT:
        raise LogoError(f"image count {count}, expected {COUNT}")
    if total != size:
        raise LogoError(f"table total {total} differs from the header payload size {size}")
    offs = list(struct.unpack_from(f"<{count}I", p, 8))
    if offs[0] != 8 + 4 * count:
        raise LogoError(f"first offset {offs[0]}, expected {8 + 4 * count}")
    ends = offs[1:] + [total]
    blobs = []
    for i, (a, b) in enumerate(zip(offs, ends)):
        if not (a < b <= total):
            raise LogoError(f"image {i}: offsets {a}..{b} are not increasing / inside the payload ({total})")
        blob = p[a:b]
        d = zlib.decompressobj()
        try:
            raw = d.decompress(blob, SIZES[i] + 1)
        except zlib.error as e:
            raise LogoError(f"image {i}: zlib failure ({e})")
        if not d.eof or d.unused_data or d.unconsumed_tail:
            raise LogoError(f"image {i}: zlib stream is incomplete, oversize or has trailing bytes")
        if len(raw) != SIZES[i]:
            raise LogoError(f"image {i}: decompressed to {len(raw)} bytes, expected {SIZES[i]}")
        blobs.append(blob)
    return size, blobs


def raw_image(blob):
    return zlib.decompress(blob)


def build(blobs):
    """Assemble header + payload from compressed images."""
    if len(blobs) != COUNT:
        raise LogoError("need exactly %d images" % COUNT)
    offs, pos = [], 8 + 4 * COUNT
    for b in blobs:
        offs.append(pos)
        pos += len(b)
    payload = struct.pack(f"<II{COUNT}I", COUNT, pos, *offs) + b"".join(blobs)
    header = MAGIC + struct.pack("<I", len(payload)) + NAME.ljust(32, b"\0")
    return header.ljust(HEADER_LEN, b"\xff") + payload


def pack565(values):
    return struct.pack(f"<{len(values)}H", *values)


def unpack565(raw):
    return struct.unpack(f"<{len(raw) // 2}H", raw)


def replace(stock, png, quantize=False):
    """Returns the new logo.bin bytes. Verifies its own output before returning."""
    sp = _splash()
    old_size, old = parse(stock)
    try:
        w, h, rgb = sp.read_png(png)
    except (sp.FormatError, zlib.error, struct.error, IndexError) as e:
        raise LogoError(f"cannot read the PNG: {e}")
    if (w, h) != (W, H):
        raise LogoError(f"PNG is {w}x{h}, must be {W}x{H}")
    try:
        vals = sp.rgb_to_565(rgb) if quantize else sp.exact_565(rgb)
    except sp.FormatError as e:
        raise LogoError(str(e))
    raw = pack565(vals)
    new_blob = zlib.compress(raw, 9)
    blobs = list(old)
    for i in LOGO_INDICES:
        blobs[i] = new_blob
    out = build(blobs)
    if len(out) > PARTITION_SIZE:
        raise LogoError(f"output is {len(out)} bytes, larger than the LOGO partition ({PARTITION_SIZE})")
    verify(out, old, raw)
    return out


def verify(out, old_blobs, raw):
    """Re-parse the output and check: images 0 and 38 decode to `raw`, every other image keeps its compressed bytes."""
    size, blobs = parse(out)
    if size != len(out) - HEADER_LEN:
        raise LogoError("verify: header payload size does not match the file")
    for i, (a, b) in enumerate(zip(old_blobs, blobs)):
        if i in LOGO_INDICES:
            if raw_image(b) != raw:
                raise LogoError(f"verify: image {i} does not decode to the requested picture")
        elif a != b:
            raise LogoError(f"verify: image {i} changed (it must stay byte-identical)")


def describe(data, partition=False):
    size, blobs = parse(data, partition)
    lines = [f"valid logo.bin: {len(data)} bytes, header + payload {size}, {len(blobs)} images, sha256 "
             f"{hashlib.sha256(data).hexdigest()}"]
    pos = 8 + 4 * COUNT
    for i, b in enumerate(blobs):
        raw = raw_image(b)
        note = {0: "  <- LK power-on logo", 38: "  <- kernel boot logo", 37: "  (battery full frame, not a logo)"}.get(i, "")
        lines.append(f"  [{i:2d}] offset {pos:6d} compressed {len(b):5d} raw {len(raw):6d} "
                     f"sha256 {hashlib.sha256(raw).hexdigest()[:12]}{note}")
        pos += len(b)
    return "\n".join(lines)


def extract(data, outdir, partition=False):
    sp = _splash()
    _, blobs = parse(data, partition)
    os.makedirs(outdir, exist_ok=True)
    written = []
    for i, b in enumerate(blobs):
        raw = raw_image(b)
        if len(raw) == FRAME:
            path = os.path.join(outdir, f"image{i:02d}.png")
            with open(path, "wb") as f:
                f.write(sp.write_png(W, H, sp.expand_565(unpack565(raw))))
        else:  # sprite: its width and height are not stored, so keep the pixels raw
            path = os.path.join(outdir, f"image{i:02d}.rgb565")
            with open(path, "wb") as f:
                f.write(raw)
        written.append(path)
    return written


def _write_new(path, data, force):
    if os.path.exists(path) and not force:
        raise LogoError(f"{path} already exists; pass --force to overwrite")
    tmp = path + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}
    cmd = args[0] if args else ""
    try:
        if cmd == "info" and len(args) == 2 and flags <= {"--partition"}:
            with open(args[1], "rb") as f:
                print(describe(f.read(), "--partition" in flags))
            return 0
        if cmd == "replace" and len(args) == 4 and flags <= {"--quantize", "--force"}:
            with open(args[1], "rb") as f:
                stock = f.read()
            with open(args[2], "rb") as f:
                png = f.read()
            out = replace(stock, png, "--quantize" in flags)
            _write_new(args[3], out, "--force" in flags)
            print(f"{args[3]}: {len(out)} bytes, sha256 {hashlib.sha256(out).hexdigest()} "
                  f"(images {', '.join(map(str, LOGO_INDICES))} replaced, the other 37 images byte-identical)")
            return 0
        if cmd == "extract" and len(args) == 3 and flags <= {"--partition"}:
            with open(args[1], "rb") as f:
                paths = extract(f.read(), args[2], "--partition" in flags)
            print(f"wrote {len(paths)} files to {args[2]}")
            return 0
    except (LogoError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))

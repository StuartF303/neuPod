"""y2logo.py (logo.bin parser/builder) and the colour identity between LK, fbtool and the committed splash.

The stock logo.bin is firmware and not in the repo, so most tests build a synthetic one with the same structure
(39 images, the stock decompressed sizes). Tests against the real factory file skip without ~/y2-firmware.
"""
import hashlib
import importlib.util
import os
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

Y2 = Path(__file__).resolve().parents[2]
TOOLS = Y2 / "tools"
PNG = Y2 / "rescue" / "splash" / "neupod-splash-480x360.png"
MASTER = Y2 / "rescue" / "splash" / "neupod-splash-480x360-master.png"
ASSET = Y2 / "rescue" / "overlay" / "etc" / "splash.y2i"
FBTOOL_C = Y2 / "buildroot" / "package" / "y2-fbtool" / "src" / "fbtool.c"
FW = Path(os.environ.get("Y2_FIRMWARE", "~/y2-firmware/y2_v3.2.0_FM-20260813")).expanduser()
BACKUP_LOGO = Path("~/y2-backup/2026-11-10/partitions/LOGO.bin").expanduser()
STOCK_SHA = "028d12afefef0a9b3b24879bbc95a6dfb179e55955625474d9528fd5263b105d"

_spec = importlib.util.spec_from_file_location("y2logo", TOOLS / "y2logo.py")
lg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lg)
_spec = importlib.util.spec_from_file_location("y2splash", TOOLS / "y2splash.py")
sp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sp)
CC = shutil.which("cc") or shutil.which("gcc")


def lk_expand(x):
    """LK's 32 bpp pixel writer (lk.bin 0x1c314), written out independently of y2splash: returns (R, G, B)."""
    word = ((x << 3) & 0xFF) | ((x & 0x7E0) << 5) | ((x & 0xF800) << 8) | 0xFF000000
    return (word >> 16) & 0xFF, (word >> 8) & 0xFF, word & 0xFF


def synthetic_images(seed=1, noisy=False):
    """39 raw images with the stock sizes. Frames are smooth gradients (small), or random (noisy: incompressible)."""
    rng = random.Random(seed)
    out = []
    for i, n in enumerate(lg.SIZES):
        if noisy and n == lg.FRAME:
            out.append(rng.randbytes(n))
        else:
            out.append(bytes(((j // 5) + i * 7) & 0xFF for j in range(n)))
    return out


def synthetic_stock(**k):
    return lg.build([zlib.compress(r, 9) for r in synthetic_images(**k)])


def canonical_png(seed=3):
    """A 480x360 PNG that is RGB565-exact (low bits zero), with a flat background and a bright block."""
    vals = [((y * 31 // 360) << 11) | ((x * 63 // 480) << 5) | ((x + y + seed) % 32) for y in range(360) for x in range(480)]
    return sp.write_png(480, 360, sp.expand_565(vals)), vals


class Container(unittest.TestCase):
    def setUp(self):
        self.stock = synthetic_stock()

    def test_parse_describe_and_rebuild_identity(self):
        size, blobs = lg.parse(self.stock)
        self.assertEqual((size, len(blobs)), (len(self.stock) - 512, 39))
        self.assertEqual(lg.build(blobs), self.stock)
        text = lg.describe(self.stock)
        self.assertTrue(text.startswith(f"valid logo.bin: {len(self.stock)} bytes"))
        self.assertIn("[38]", text)
        self.assertEqual(len(text.splitlines()), 40)

    def test_header_layout(self):
        self.assertEqual(self.stock[:4], bytes.fromhex("88168858"))
        self.assertEqual(struct.unpack_from("<I", self.stock, 4)[0], len(self.stock) - 512)
        self.assertEqual(self.stock[8:40], b"LOGO" + bytes(28))
        self.assertEqual(self.stock[40:512], b"\xff" * 472)
        count, total = struct.unpack_from("<II", self.stock, 512)
        self.assertEqual((count, total), (39, len(self.stock) - 512))
        self.assertEqual(struct.unpack_from("<I", self.stock, 520)[0], 8 + 4 * 39)

    def mutate(self, off, data):
        b = bytearray(self.stock)
        b[off:off + len(data)] = data
        return bytes(b)

    def test_rejects_bad_containers(self):
        _, blobs = lg.parse(self.stock)
        off1 = struct.unpack_from("<I", self.stock, 512 + 8 + 4)[0]            # offset of image 1
        bad_size = [zlib.compress(b"\0" * 100, 9) if i == 5 else b for i, b in enumerate(blobs)]
        cases = {
            "empty": b"",
            "short": self.stock[:100],
            "magic": self.mutate(0, b"\x00\x00\x00\x00"),
            "name": self.mutate(8, b"LOGX"),
            "name padding": self.mutate(12, b"\x01"),
            "header padding": self.mutate(300, b"\x00"),
            "header size too big": self.mutate(4, struct.pack("<I", len(self.stock))),
            "header size too small": self.mutate(4, struct.pack("<I", len(self.stock) - 512 - 1)),
            "count 38": self.mutate(512, struct.pack("<I", 38)),
            "count 40": self.mutate(512, struct.pack("<I", 40)),
            "total": self.mutate(516, struct.pack("<I", len(self.stock) - 512 - 1)),
            "first offset": self.mutate(520, struct.pack("<I", 165)),
            "offsets not increasing": self.mutate(512 + 8 + 4, struct.pack("<I", 100)),
            "offset past the end": self.mutate(512 + 8 + 4 * 38, struct.pack("<I", len(self.stock))),
            "offsets equal": self.mutate(512 + 8 + 4 * 2, struct.pack("<I", off1)),
            "zlib corrupted": self.mutate(512 + 164 + 40, b"\xde\xad\xbe\xef"),
            "zlib header": self.mutate(512 + 164, b"\x00\x00"),
            "truncated stream": self.stock[:-3],
            "trailing bytes": self.stock + b"\x00",
            "wrong image size": lg.build(bad_size),
        }
        for name, data in cases.items():
            with self.assertRaises(lg.LogoError, msg=name):
                lg.parse(data)

    def test_trailing_garbage_inside_a_zlib_stream_and_decompression_bombs_are_rejected(self):
        _, blobs = lg.parse(self.stock)
        for extra in (b"\x00", b"junk"):
            b2 = list(blobs)
            b2[3] = blobs[3] + extra
            with self.assertRaises(lg.LogoError):
                lg.parse(lg.build(b2))
        b2 = list(blobs)
        b2[0] = zlib.compress(bytes(lg.FRAME * 100), 9)               # inflates far beyond the allowed size
        with self.assertRaises(lg.LogoError):
            lg.parse(lg.build(b2))

    def test_partition_dump_accepts_a_tail_only_with_the_flag(self):
        dump = self.stock + b"\xff" * 1000 + b"\x00" * 50
        with self.assertRaises(lg.LogoError):
            lg.parse(dump)
        self.assertEqual(lg.parse(dump, partition=True)[0], len(self.stock) - 512)

    def test_info_cli_exit_codes(self):
        with tempfile.TemporaryDirectory() as d:
            good, bad = Path(d) / "g.bin", Path(d) / "b.bin"
            good.write_bytes(self.stock)
            bad.write_bytes(self.mutate(0, b"\0\0\0\0"))
            run = lambda *a: subprocess.run(["python3", "-I", str(TOOLS / "y2logo.py"), *map(str, a)], capture_output=True, text=True)
            r = run("info", good)
            self.assertEqual((r.returncode, r.stdout.startswith("valid logo.bin")), (0, True), r.stderr)
            r = run("info", bad)
            self.assertEqual(r.returncode, 1)
            self.assertIn("bad magic", r.stderr)
            self.assertEqual(run("info").returncode, 2)
            self.assertEqual(run("info", Path(d) / "missing").returncode, 1)


class Replace(unittest.TestCase):
    def setUp(self):
        self.stock = synthetic_stock()
        self.png, self.vals = canonical_png()

    def test_replaces_images_0_and_38_only_and_keeps_every_other_blob_byte_identical(self):
        out = lg.replace(self.stock, self.png)
        old_size, old = lg.parse(self.stock)
        new_size, new = lg.parse(out)
        want = lg.pack565(self.vals)
        for i in range(39):
            if i in (0, 38):
                self.assertEqual(lg.raw_image(new[i]), want, i)
            else:
                self.assertIs(type(new[i]), bytes)
                self.assertEqual(new[i], old[i], i)                  # the same compressed bytes
        self.assertEqual(lg.LOGO_INDICES, (0, 38))
        self.assertNotEqual(old[37], new[0])                         # 37 is the battery frame: untouched
        self.assertEqual(new_size, len(out) - 512)
        self.assertEqual(struct.unpack_from("<I", out, 4)[0], new_size)
        offs = struct.unpack_from("<39I", out, 520)                  # rebuilt, consistent with the blob lengths
        self.assertEqual(offs[0], 164)
        self.assertEqual([b - a for a, b in zip(offs, offs[1:])], [len(x) for x in new[:-1]])
        self.assertEqual(offs[-1] + len(new[-1]), new_size)

    def test_pixel_format_is_rgb565_little_endian_top_row_first(self):
        raw = lg.raw_image(lg.parse(lg.replace(self.stock, self.png))[1][0])
        self.assertEqual(len(raw), 480 * 360 * 2)
        self.assertEqual(struct.unpack_from("<H", raw, 0)[0], self.vals[0])
        self.assertEqual(struct.unpack_from("<H", raw, 2 * 479)[0], self.vals[479])
        self.assertEqual(struct.unpack_from("<H", raw, 2 * 480)[0], self.vals[480])

    def test_refuses_a_png_lk_cannot_show_exactly_unless_quantize_is_given(self):
        png8 = sp.write_png(480, 360, bytes((9, 17, 33)) * (480 * 360))
        with self.assertRaises(lg.LogoError) as cm:
            lg.replace(self.stock, png8)
        self.assertIn("quantize", str(cm.exception))
        out = lg.replace(self.stock, png8, quantize=True)
        want = struct.pack("<H", (9 * 31 + 127) // 255 << 11 | (17 * 63 + 127) // 255 << 5 | (33 * 31 + 127) // 255)
        self.assertEqual(lg.raw_image(lg.parse(out)[1][0])[:2], want)

    def test_refuses_wrong_size_or_unreadable_pngs(self):
        for w, h in ((320, 240), (360, 480), (481, 360)):
            with self.assertRaises(lg.LogoError):
                lg.replace(self.stock, sp.write_png(w, h, bytes(w * h * 3)))
        for junk in (b"", b"not a png", self.png[:200]):
            with self.assertRaises(lg.LogoError):
                lg.replace(self.stock, junk)

    def test_refuses_an_invalid_stock_file(self):
        with self.assertRaises(lg.LogoError):
            lg.replace(self.stock[:-1], self.png)

    def test_refuses_output_larger_than_the_logo_partition(self):
        noisy_stock = synthetic_stock(noisy=True)                     # 17 random frames: about 5.9 MB
        self.assertGreater(len(noisy_stock), lg.PARTITION_SIZE)
        with self.assertRaises(lg.LogoError) as cm:
            lg.replace(noisy_stock, self.png)
        self.assertIn("larger than the LOGO partition", str(cm.exception))
        # an output just under the limit is fine, the limit is the partition size exactly
        self.assertEqual(lg.PARTITION_SIZE, 0x300000)

    def test_verify_catches_a_changed_image(self):
        out = lg.replace(self.stock, self.png)
        _, old = lg.parse(self.stock)
        _, new = lg.parse(out)
        new[7] = zlib.compress(bytes(600), 9)
        with self.assertRaises(lg.LogoError):
            lg.verify(lg.build(new), old, lg.pack565(self.vals))
        new = list(lg.parse(out)[1])
        with self.assertRaises(lg.LogoError):
            lg.verify(out, old, bytes(lg.FRAME))                      # wrong expected picture

    def test_cli_replace_extract_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "stock.bin").write_bytes(self.stock)
            (d / "in.png").write_bytes(self.png)
            run = lambda *a: subprocess.run(["python3", "-I", str(TOOLS / "y2logo.py"), *map(str, a)], capture_output=True, text=True)
            r = run("replace", d / "stock.bin", d / "in.png", d / "out.bin")
            self.assertEqual(r.returncode, 0, r.stderr)
            out = (d / "out.bin").read_bytes()
            self.assertEqual(out, lg.replace(self.stock, self.png))
            self.assertIn(hashlib.sha256(out).hexdigest(), r.stdout)
            r = run("replace", d / "stock.bin", d / "in.png", d / "out.bin")
            self.assertEqual(r.returncode, 1)
            self.assertIn("--force", r.stderr)
            self.assertEqual(run("replace", d / "stock.bin", d / "in.png", d / "out.bin", "--force").returncode, 0)
            self.assertFalse((d / "out.bin.part").exists())
            r = run("extract", d / "out.bin", d / "x")
            self.assertEqual(r.returncode, 0, r.stderr)
            names = sorted(p.name for p in (d / "x").iterdir())
            self.assertEqual(len(names), 39)
            self.assertIn("image00.png", names)
            self.assertIn("image04.rgb565", names)
            w, h, rgb = sp.read_png((d / "x" / "image38.png").read_bytes())
            self.assertEqual((w, h, rgb), (480, 360, sp.expand_565(self.vals)))
            self.assertEqual(run("replace", d / "stock.bin", d / "stock.bin", d / "o2.bin").returncode, 1)
            self.assertFalse((d / "o2.bin").exists())


class Colour(unittest.TestCase):
    """LK, the fbtool splash and the logo.bin show the same pixels."""

    def test_committed_splash_is_what_lk_would_show_from_the_logo(self):
        w, h, canon = sp.read_png(PNG.read_bytes())
        out = lg.replace(synthetic_stock(), PNG.read_bytes())          # no --quantize: it is already 565-exact
        raw = lg.raw_image(lg.parse(out)[1][0])
        vals = struct.unpack(f"<{len(raw) // 2}H", raw)
        lk = b"".join(bytes(lk_expand(v)) for v in vals)
        self.assertEqual(lk, canon)                                    # LK's pixels == the committed canonical PNG
        self.assertEqual(sp.decode(ASSET.read_bytes())[2], lk)         # and == what splash.y2i decodes to (fbtool)
        self.assertEqual(lg.raw_image(lg.parse(out)[1][38]), raw)       # the kernel boot logo is the same picture

    def test_canonical_png_regenerates_from_the_master(self):
        _, _, master = sp.read_png(MASTER.read_bytes())
        self.assertEqual(sp.read_png(PNG.read_bytes())[2], sp.quantize_rgb(master))

    @unittest.skipUnless(CC, "no C compiler")
    def test_fbtool_output_equals_lk_pixels(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            r = subprocess.run([CC, "-Wall", "-Wextra", "-Werror", "-O2", "-o", str(d / "fbtool"), str(FBTOOL_C)],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            fb = d / "fb.raw"
            fb.write_bytes(b"\xaa" * (480 * 4 * 360 * 3))
            env = {"FBTOOL_FAKE_FB": str(fb), "FBTOOL_FAKE_SIZE": "480x360"}
            r = subprocess.run([str(d / "fbtool"), "image", str(ASSET)], capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            xrgb = fb.read_bytes()[:480 * 360 * 4]                    # page 0, XRGB8888 little endian: B G R X
            drawn = b"".join(xrgb[i + 2:i + 3] + xrgb[i + 1:i + 2] + xrgb[i:i + 1] for i in range(0, len(xrgb), 4))
            out = lg.replace(synthetic_stock(), PNG.read_bytes())
            raw = lg.raw_image(lg.parse(out)[1][0])
            lk = b"".join(bytes(lk_expand(v)) for v in struct.unpack(f"<{len(raw) // 2}H", raw))
            self.assertEqual(drawn, lk)

    def test_banding_is_known_and_small(self):
        """565 cannot show the smooth gradient: the background moves in whole 565 steps. Documented, not hidden."""
        w, h, rgb = sp.read_png(PNG.read_bytes())
        rows = [rgb[y * w * 3:y * w * 3 + 3] for y in range(h)]
        steps = [y for y in range(1, h) if rows[y] != rows[y - 1]]
        self.assertLessEqual(len(steps), 12)
        for a, b in zip(rows, rows[1:]):
            self.assertLessEqual(max(abs(x - y) for x, y in zip(a, b)), 8)   # never more than one 5-bit step per channel


@unittest.skipUnless((FW / "logo.bin").is_file(), "factory firmware not present")
class FactoryFile(unittest.TestCase):
    def test_stock_logo_parses_and_rebuilds_byte_identically(self):
        data = (FW / "logo.bin").read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), STOCK_SHA)
        _, blobs = lg.parse(data)
        self.assertEqual(lg.build(blobs), data)
        # 0 and 38 are the innioasis logo (identical), 37 is the battery-full frame
        self.assertEqual(blobs[0], blobs[38])
        self.assertNotEqual(blobs[0], blobs[37])

    def test_replace_with_the_committed_splash(self):
        data = (FW / "logo.bin").read_bytes()
        out = lg.replace(data, PNG.read_bytes())
        _, old = lg.parse(data)
        _, new = lg.parse(out)
        self.assertEqual([i for i in range(39) if old[i] != new[i]], [0, 38])
        self.assertLessEqual(len(out), lg.PARTITION_SIZE)

    @unittest.skipUnless(BACKUP_LOGO.is_file(), "backup not present")
    def test_device_logo_partition_is_the_stock_file_plus_filler(self):
        part = BACKUP_LOGO.read_bytes()
        data = (FW / "logo.bin").read_bytes()
        self.assertEqual(len(part), lg.PARTITION_SIZE)
        self.assertEqual(part[:len(data)], data)
        lg.parse(part, partition=True)
        with self.assertRaises(lg.LogoError):
            lg.parse(part)


if __name__ == "__main__":
    unittest.main()

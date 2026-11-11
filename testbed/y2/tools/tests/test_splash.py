"""Splash: the Y2I1 generator (tools/y2splash.py) and `fbtool image|check|status`.

fbtool is built with the host C compiler and drawn onto a regular file via FBTOOL_FAKE_FB, so the real
drawing code runs without a panel. Tests that need the compiler skip when none is installed.
"""
import importlib.util
import os
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

Y2 = Path(__file__).resolve().parents[2]
FBTOOL_C = Y2 / "buildroot" / "package" / "y2-fbtool" / "src" / "fbtool.c"
PNG = Y2 / "rescue" / "splash" / "neupod-splash-480x360.png"
ASSET = Y2 / "rescue" / "overlay" / "etc" / "splash.y2i"

_spec = importlib.util.spec_from_file_location("y2splash", Y2 / "tools" / "y2splash.py")
sp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sp)

CC = shutil.which("cc") or shutil.which("gcc")


def gradient(w, h):
    return b"".join(bytes((y * 7 % 256, y * 3 % 256, 40)) * w for y in range(h))


class Generator(unittest.TestCase):
    def test_round_trip_synthetic(self):
        rng = random.Random(1)
        for w, h in ((1, 1), (7, 5), (300, 4)):
            pix = [bytes(rng.choice((0, 128, 255)) for _ in range(3)) for _ in range(w * h)]
            rgb = b"".join(pix)
            self.assertEqual(sp.decode(sp.encode(w, h, rgb)), (w, h, rgb))

    def test_long_runs_split_at_255(self):
        rgb = bytes((9, 9, 9)) * 1000
        blob = sp.encode(1000, 1, rgb)
        self.assertEqual(sp.decode(blob), (1000, 1, rgb))
        self.assertEqual(len(blob), 10 + 3 + 2 * 4)

    def test_too_many_colours_rejected(self):
        rgb = b"".join(bytes((i % 256, i // 256, 0)) for i in range(257))
        with self.assertRaises(sp.FormatError):
            sp.encode(257, 1, rgb)

    def test_decoder_rejects_malformed(self):
        good = sp.encode(4, 2, gradient(4, 2))
        bad = {
            "magic": b"Y2I2" + good[4:],
            "empty": b"",
            "zero width": good[:4] + struct.pack("<H", 0) + good[6:],
            "oversize": good[:4] + struct.pack("<HH", 5000, 5000) + good[8:],
            "palette 0": good[:8] + struct.pack("<H", 0) + good[10:],
            "palette 257": good[:8] + struct.pack("<H", 257) + good[10:],
            "trailing": good + b"\x00",
            "truncated": good[:-1],
            "zero run": good[:10 + 3 * 2] + b"\x00\x00" + good[10 + 3 * 2 + 2:],
        }
        for name, blob in bad.items():
            with self.assertRaises(sp.FormatError, msg=name):
                sp.decode(blob)

    def test_png_round_trip_and_committed_asset(self):
        w, h, rgb = sp.read_png(PNG.read_bytes())
        self.assertEqual((w, h), (480, 360))
        self.assertEqual(sp.read_png(sp.write_png(w, h, rgb)), (w, h, rgb))
        blob = ASSET.read_bytes()
        self.assertLess(len(blob), 50 * 1024)
        self.assertEqual(sp.decode(blob), (w, h, rgb))  # the asset is exactly the PNG
        self.assertEqual(blob, sp.encode(w, h, rgb))  # and is reproducible from it

    def test_cli_convert(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "x.y2i"
            r = subprocess.run(["python3", "-I", str(Y2 / "tools" / "y2splash.py"), "convert", str(PNG), str(out)],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(out.read_bytes(), ASSET.read_bytes())


@unittest.skipUnless(CC, "no C compiler")
class Fbtool(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)
        cls.bin = cls.dir / "fbtool"
        r = subprocess.run([CC, "-Wall", "-Wextra", "-Werror", "-O2", "-o", str(cls.bin), str(FBTOOL_C)],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_fb(self, *args, size=(480, 360), fb=None, env_extra=None):
        w, h = size
        fbp = fb or self.dir / "fb.raw"
        if fb is None:
            fbp.write_bytes(b"\xaa" * (w * 4 * h * 3))  # sentinel: a failed command must leave this alone
        env = {"FBTOOL_FAKE_FB": str(fbp), "FBTOOL_FAKE_SIZE": f"{w}x{h}"}
        env.update(env_extra or {})
        r = subprocess.run([str(self.bin), *map(str, args)], capture_output=True, text=True, env=env)
        return r, fbp

    @staticmethod
    def page(fbp, w, h, p=0):
        raw = fbp.read_bytes()[p * w * h * 4:(p + 1) * w * h * 4]
        return b"".join(raw[i + 2:i + 3] + raw[i + 1:i + 2] + raw[i:i + 1] for i in range(0, len(raw), 4))

    def blob(self, name, data):
        p = self.dir / name
        p.write_bytes(data)
        return p

    def test_check_matches_reference_decoder(self):
        out = self.dir / "dec.ppm"
        r = subprocess.run([str(self.bin), "check", str(ASSET), str(out)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(r.stdout.startswith("ok 480x360"))
        ppm = out.read_bytes()
        hdr = b"P6\n480 360\n255\n"
        self.assertTrue(ppm.startswith(hdr))
        self.assertEqual(ppm[len(hdr):], sp.decode(ASSET.read_bytes())[2])

    def test_image_draws_every_page_exactly(self):
        r, fbp = self.run_fb("image", ASSET)
        self.assertEqual(r.returncode, 0, r.stderr)
        want = sp.decode(ASSET.read_bytes())[2]
        for p in range(3):
            self.assertEqual(self.page(fbp, 480, 360, p), want, f"page {p}")

    def test_bad_files_draw_nothing(self):
        good = ASSET.read_bytes()
        n = struct.unpack_from("<H", good, 8)[0]
        cases = {
            "bad magic": b"XXXX" + good[4:],
            "truncated header": good[:7],
            "truncated palette": good[:10 + 3 * n - 1],
            "truncated pixels": good[:-2],
            "odd length": good[:-1],
            "trailing": good + b"\x01\x00",
            "oversize dims": good[:4] + struct.pack("<HH", 4097, 360) + good[8:],
            "zero dims": good[:4] + struct.pack("<HH", 0, 0) + good[8:],
            "huge declared size": good[:4] + struct.pack("<HH", 4096, 4096) + good[8:],
            "index out of range": good[:10 + 3 * n + 1] + bytes((n,)) + good[10 + 3 * n + 2:],
            "zero run": good[:10 + 3 * n] + b"\x00\x00" + good[10 + 3 * n + 2:],
            "empty": b"",
            "too large": b"Y2I1" + b"\x00" * (1 << 20),
        }
        for name, data in cases.items():
            f = self.blob("bad.y2i", data)
            r, fbp = self.run_fb("image", f)
            self.assertEqual(r.returncode, 3, f"{name}: {r.stderr}")
            self.assertIn("fbtool:", r.stderr, name)
            self.assertEqual(fbp.read_bytes(), b"\xaa" * (480 * 4 * 360 * 3), f"{name}: framebuffer touched")
            r = subprocess.run([str(self.bin), "check", str(f)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 3, name)

    def test_every_truncation_is_rejected(self):
        blob = sp.encode(6, 3, gradient(6, 3))
        for cut in range(len(blob)):
            f = self.blob("t.y2i", blob[:cut])
            r = subprocess.run([str(self.bin), "check", str(f)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 3, f"cut at {cut}")
        r = subprocess.run([str(self.bin), "check", str(self.blob("t.y2i", blob))], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)

    def test_missing_file(self):
        r, fbp = self.run_fb("image", self.dir / "nope.y2i")
        self.assertEqual(r.returncode, 3)
        self.assertEqual(fbp.read_bytes(), b"\xaa" * (480 * 4 * 360 * 3))

    def test_smaller_panel_clips_centre(self):
        w, h = 320, 240
        r, fbp = self.run_fb("image", ASSET, size=(w, h))
        self.assertEqual(r.returncode, 0, r.stderr)
        _, _, src = sp.decode(ASSET.read_bytes())
        sx, sy = (480 - w) // 2, (360 - h) // 2
        want = b"".join(src[((sy + y) * 480 + sx) * 3:((sy + y) * 480 + sx + w) * 3] for y in range(h))
        self.assertEqual(self.page(fbp, w, h), want)

    def test_larger_panel_centres_on_black(self):
        w, h = 640, 480
        r, fbp = self.run_fb("image", ASSET, size=(w, h))
        self.assertEqual(r.returncode, 0, r.stderr)
        _, _, src = sp.decode(ASSET.read_bytes())
        dx, dy = (w - 480) // 2, (h - 360) // 2
        pg = self.page(fbp, w, h)
        black = bytes(3)
        for y in range(h):
            row = pg[y * w * 3:(y + 1) * w * 3]
            if dy <= y < dy + 360:
                self.assertEqual(row[:dx * 3], black * dx)
                self.assertEqual(row[dx * 3:(dx + 480) * 3], src[(y - dy) * 480 * 3:(y - dy + 1) * 480 * 3])
                self.assertEqual(row[(dx + 480) * 3:], black * (w - dx - 480))
            else:
                self.assertEqual(row, black * w)

    def test_status_dots_overlay_bottom_only(self):
        r, fbp = self.run_fb("image", ASSET)
        base = self.page(fbp, 480, 360)
        shots = {}
        for n in (0, 3, 5):
            r, _ = self.run_fb("status", n, fb=fbp)
            self.assertEqual(r.returncode, 0, r.stderr)
            shots[n] = self.page(fbp, 480, 360)
            self.run_fb("image", ASSET, fb=fbp)
        # nothing above the bottom band changes, and the top-left blocks are gone
        band = 360 - 40
        for n, pg in shots.items():
            self.assertEqual(pg[:band * 480 * 3], base[:band * 480 * 3], f"status {n} drew above the bottom edge")
            self.assertNotEqual(pg, base)
        self.assertEqual(pg[:band * 480 * 3], base[:band * 480 * 3])

        def green(pg):
            return sum(1 for i in range(0, len(pg), 3) if pg[i:i + 3] == b"\x00\xff\x00")
        self.assertEqual(green(shots[0]), 0)
        self.assertGreater(green(shots[3]), 0)
        # 3 of 5 lit: green area is exactly 3/5 of the fully lit area
        self.assertEqual(green(shots[3]) * 5, green(shots[5]) * 3)
        # all pages carry the dots (the panel may be showing any of them while panning)
        r, _ = self.run_fb("status", 2, fb=fbp)
        self.assertEqual(self.page(fbp, 480, 360, 0), self.page(fbp, 480, 360, 2))

    def test_fill_and_bars_unchanged(self):
        r, fbp = self.run_fb("fill", "ff0000")
        self.assertEqual(self.page(fbp, 480, 360), b"\xff\x00\x00" * (480 * 360))
        r, fbp = self.run_fb("bars")
        self.assertEqual(r.returncode, 0)
        pg = self.page(fbp, 480, 360)
        self.assertEqual(pg[:3], b"\xff\xff\xff")
        self.assertEqual(pg[479 * 3:480 * 3], b"\x00\x00\x00")


class Wiring(unittest.TestCase):
    def test_rescue_init_uses_splash_with_bars_fallback(self):
        s = (Y2 / "rescue" / "overlay" / "init").read_text()
        self.assertIn("fbtool image /etc/splash.y2i >/dev/null 2>&1 || fbtool bars", s)
        self.assertIn("fbtool fill ff0000", s)  # panic stays red

    def test_rootfs_display_script_uses_splash_with_grey_fallback(self):
        s = (Y2 / "buildroot" / "board" / "y2" / "overlay" / "etc" / "init.d" / "S20display").read_text()
        self.assertIn("fbtool image /etc/splash.y2i >/dev/null 2>&1 || fbtool fill 202020", s)

    def test_rootfs_installs_the_rescue_asset(self):
        s = (Y2 / "buildroot" / "board" / "y2" / "post-build.sh").read_text()
        self.assertIn('rescue/overlay/etc/splash.y2i" "$TARGET/etc/splash.y2i"', s)


if __name__ == "__main__":
    unittest.main()

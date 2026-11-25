import gzip, hashlib, importlib.util, os, struct, tempfile, unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("mtk_bootimg", Path(__file__).parent.parent / "mtk-bootimg.py")
tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tool)

FW = Path(os.environ.get("Y2_FIRMWARE", "~/y2-firmware/y2_v3.2.0_FM-20260813")).expanduser()
sha = lambda b: hashlib.sha256(b).hexdigest()


def mtk(name, payload):
    h = struct.pack("<II32s", tool.MTK_MAGIC, len(payload), name.encode())
    return h.ljust(512, b"\xff") + payload


META = dict(kernel_addr=0x10008000, ramdisk_addr=0x11000000, second_addr=0x10f00000,
            tags_addr=0x10000100, page_size=2048, header_version=0, os_version=0,
            name="test", cmdline="", extra_cmdline="")


def synth(ramdisk_payload=b"RAMDISK" * 500, trailer=b""):
    k, r = mtk("KERNEL", b"KERNEL!" * 700), mtk("ROOTFS", ramdisk_payload)
    return tool.build(META, k, r, b"", trailer), k, r


class Base(unittest.TestCase):
    def setUp(self):
        self._t = tempfile.TemporaryDirectory()
        self.tmp = Path(self._t.name)
        self.addCleanup(self._t.cleanup)

    def run_tool(self, *args):
        self.assertEqual(tool.main([str(a) for a in args]), 0)


class Synthetic(Base):
    def roundtrip(self, img):
        (self.tmp / "a.img").write_bytes(img)
        self.run_tool("unpack", self.tmp / "a.img", self.tmp / "d")
        self.run_tool("repack", self.tmp / "d", self.tmp / "b.img")
        return (self.tmp / "b.img").read_bytes()

    def test_identical(self):
        img, k, r = synth()
        self.assertEqual(self.roundtrip(img), img)
        self.assertEqual((self.tmp / "d/kernel.mtkhdr").read_bytes() + (self.tmp / "d/kernel").read_bytes(), k)

    def test_trailer_preserved(self):
        img, _, _ = synth(trailer=b"TRAILER")
        self.assertEqual(self.roundtrip(img), img)

    def test_no_mtk_header(self):
        img = tool.build(META, b"plainkernel", b"plainramdisk", b"", b"")
        self.assertEqual(self.roundtrip(img), img)
        self.assertFalse((self.tmp / "d/kernel.mtkhdr").exists())

    def test_modified_ramdisk(self):
        img, _, _ = synth()
        (self.tmp / "a.img").write_bytes(img)
        self.run_tool("unpack", self.tmp / "a.img", self.tmp / "d")
        new = b"NEWRAMDISK" * 321
        (self.tmp / "d/ramdisk.cpio.gz").write_bytes(new)
        self.run_tool("repack", self.tmp / "d", self.tmp / "b.img")
        meta, parts, _ = tool.parse((self.tmp / "b.img").read_bytes())
        h, p = tool.split_mtk(parts["ramdisk"], "ramdisk")
        self.assertEqual(p, new)
        self.assertEqual(struct.unpack_from("<I", h, 4)[0], len(new))
        self.assertEqual(h[:4] + h[8:], mtk("ROOTFS", b"")[:4] + mtk("ROOTFS", b"")[8:512])
        self.assertEqual(meta["id"], tool.compute_id(parts["kernel"], parts["ramdisk"], b"").hex())
        self.assertNotEqual(meta["id"], tool.parse(img)[0]["id"])
        self.run_tool("unpack", self.tmp / "b.img", self.tmp / "d2")
        self.assertEqual((self.tmp / "d2/ramdisk.cpio.gz").read_bytes(), new)

    def test_force_and_errors(self):
        img, _, _ = synth()
        (self.tmp / "a.img").write_bytes(img)
        self.run_tool("unpack", self.tmp / "a.img", self.tmp / "d")
        self.run_tool("repack", self.tmp / "d", self.tmp / "b.img")
        self.assertEqual(tool.main(["repack", str(self.tmp / "d"), str(self.tmp / "b.img")]), 1)
        self.run_tool("repack", "--force", self.tmp / "d", self.tmp / "b.img")
        for bad in (b"NOTANDROID" * 300, img[:4000], img[:36] + struct.pack("<I", 3000) + img[40:]):
            (self.tmp / "bad.img").write_bytes(bad)
            self.assertEqual(tool.main(["info", str(self.tmp / "bad.img")]), 1)

    def unpacked(self, img, name="d"):
        (self.tmp / "a.img").write_bytes(img)
        self.run_tool("unpack", "--force", self.tmp / "a.img", self.tmp / name)
        return self.tmp / name

    def repack_rc(self, d):
        return tool.main(["repack", "--force", str(d), str(self.tmp / "b.img")])

    def test_unpack_refuses_nonempty_and_force_clears_stale(self):
        d = self.unpacked(synth(trailer=b"TRAILER")[0])
        self.assertEqual(tool.main(["unpack", str(self.tmp / "a.img"), str(d)]), 1)
        img2 = synth(b"X" * 100)[0]
        self.assertEqual(self.unpacked(img2).name, "d")
        self.assertFalse((d / "trailer").exists())
        self.run_tool("repack", "--force", d, self.tmp / "b.img")
        self.assertEqual((self.tmp / "b.img").read_bytes(), img2)

    def test_field_overflow_rejected(self):
        for k, n in (("cmdline", 512), ("extra_cmdline", 1024), ("name", 16)):
            with self.assertRaises(tool.BootError):
                tool.build({**META, k: "a" * n}, b"k", b"r", b"", b"")
            tool.build({**META, k: "a" * (n - 1)}, b"k", b"r", b"", b"")
        with self.assertRaises(tool.BootError):
            tool.build({**META, "name": "\u20ac"}, b"k", b"r", b"", b"")

    def test_bad_mtkhdr_and_flags(self):
        d = self.unpacked(synth()[0])
        hdr = (d / "ramdisk.mtkhdr").read_bytes()
        (d / "ramdisk.mtkhdr").write_bytes(hdr[:100])
        self.assertEqual(self.repack_rc(d), 1)
        (d / "ramdisk.mtkhdr").write_bytes(b"\0\0\0\0" + hdr[4:])
        self.assertEqual(self.repack_rc(d), 1)
        (d / "ramdisk.mtkhdr").unlink()
        self.assertEqual(self.repack_rc(d), 1)
        (d / "ramdisk.mtkhdr").write_bytes(hdr)
        self.assertEqual(self.repack_rc(d), 0)
        # payload with MTK magic but flag false
        d2 = self.unpacked(tool.build(META, b"k", b"r", b"", b""), "d2")
        (d2 / "kernel").write_bytes(mtk("KERNEL", b"zz"))
        self.assertEqual(self.repack_rc(d2), 1)

    def test_bad_json_values(self):
        import json
        d = self.unpacked(synth()[0])
        j = json.loads((d / "bootimg.json").read_text())
        for k, v in (("kernel_addr", "0x1ffffffff"), ("header_version", 1), ("os_version", "zz"),
                     ("page_size", 3000), ("name", 5)):
            (d / "bootimg.json").write_text(json.dumps({**j, k: v}))
            self.assertEqual(self.repack_rc(d), 1, k)

    def test_size_change_message_and_no_tmp_left(self):
        import contextlib, io
        d = self.unpacked(synth()[0])
        (d / "ramdisk.cpio.gz").write_bytes(b"abc")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(self.repack_rc(d), 0)
        self.assertIn("ramdisk: MTK size 3500 -> 3", err.getvalue())
        self.assertFalse((self.tmp / "b.img.tmp").exists())

    def test_info_mismatch_exit(self):
        img = bytearray(synth()[0])
        img[0x240] ^= 1
        (self.tmp / "m.img").write_bytes(img)
        self.assertEqual(tool.main(["info", str(self.tmp / "m.img")]), 1)


@unittest.skipUnless((FW / "boot.img").exists() and (FW / "recovery.img").exists(), "factory firmware not found")
class Factory(Base):
    def test_roundtrip_identical(self):
        for n in ("boot.img", "recovery.img"):
            with self.subTest(n):
                d, out = self.tmp / n, self.tmp / (n + ".new")
                self.run_tool("unpack", FW / n, d)
                self.run_tool("repack", d, out)
                self.assertEqual(sha(out.read_bytes()), sha((FW / n).read_bytes()))
                meta, parts, _ = tool.parse((FW / n).read_bytes())
                self.assertEqual(meta["id"], tool.compute_id(parts["kernel"], parts["ramdisk"], b"").hex())

    def test_payload_files(self):
        self.run_tool("unpack", FW / "boot.img", self.tmp / "d")
        d = self.tmp / "d"
        self.assertEqual((d / "kernel.mtkhdr").read_bytes() + (d / "kernel").read_bytes(), (FW / "kernel").read_bytes())
        self.assertEqual((d / "ramdisk.mtkhdr").read_bytes() + (d / "ramdisk.cpio.gz").read_bytes(), (FW / "ramdisk.img").read_bytes())
        self.assertTrue(gzip.decompress((d / "ramdisk.cpio.gz").read_bytes()).startswith(b"070701"))


if __name__ == "__main__":
    unittest.main()

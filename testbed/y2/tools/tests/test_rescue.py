"""Checks for the y2-rescue initramfs (T1.2).

Overlay/script checks always run. Checks on build outputs skip cleanly when ~/y2-build (or the
backup) is absent, e.g. in CI. Override with Y2_BUILD / Y2_BACKUP.
"""
import gzip, os, re, shutil, stat, subprocess, tempfile, unittest, importlib.util
from pathlib import Path

Y2 = Path(__file__).resolve().parents[2]
OVERLAY = Y2 / "rescue" / "overlay"
BUILD = Path(os.environ.get("Y2_BUILD", "~/y2-build")).expanduser()
BACKUP = Path(os.environ.get("Y2_BACKUP", "~/y2-backup/2026-11-10")).expanduser()
CPIO = BUILD / "out-rescue" / "images" / "rootfs.cpio.gz"

_spec = importlib.util.spec_from_file_location("mtk_bootimg", Y2 / "tools" / "mtk-bootimg.py")
tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tool)

SCRIPTS = ["init", "bin/diag.sh", "bin/respawn", "bin/console-shell"]


class Overlay(unittest.TestCase):
    def read(self, rel):
        return (OVERLAY / rel).read_text()

    def test_scripts_are_executable_with_shebang(self):
        for rel in SCRIPTS:
            p = OVERLAY / rel
            self.assertTrue(p.stat().st_mode & stat.S_IXUSR, rel)
            self.assertEqual(p.read_text().splitlines()[0], "#!/bin/sh", rel)

    @unittest.skipUnless(shutil.which("dash"), "dash not installed")
    def test_posix_syntax(self):  # dash approximates busybox ash
        for rel in SCRIPTS:
            r = subprocess.run(["dash", "-n", str(OVERLAY / rel)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, f"{rel}: {r.stderr}")

    def test_init_never_writes_block_devices(self):
        s = self.read("init")
        for bad in (r"\bmkfs", r"\bdd\b", r"of=/dev", r">\s*/dev/(mmcblk|block)", r"\bfsck", r"\be2fsck"):
            self.assertIsNone(re.search(bad, s), bad)
        # every ext4 probe of USRDATA is read-only and journal-less
        self.assertIn("ro,noload", s)

    def test_gadget_sequence(self):
        s = self.read("init")
        for needle in ("idVendor 18d1", "idProduct 4ee7", "iManufacturer neuPod", "iProduct y2-rescue",
                       "iSerial y2rescue0001", "f_rndis/ethaddr 02:59:32:00:00:02", "f_rndis/wceis 1",
                       "f_acm/instances 1", "rndis,acm", "192.168.7.2"):
            self.assertIn(needle, s)
        body = s[s.index("gadget_try()"):s.index("GADGET=none")]
        order = [body.index(k) for k in ("gw enable 0", "gw idVendor", "gw f_acm/instances", "gw functions", "gw enable 1")]
        self.assertEqual(order, sorted(order))
        self.assertLess(s.index("gadget_try rndis,acm"), s.index("gadget_try acm 02"))
        self.assertLess(s.index("gadget_try acm 02"), s.index("gadget_try mass_storage"))
        self.assertLess(s.index('echo "0 20 30" >/proc/wdk'), s.index("mdev -s"))

    def test_usrdata_found_by_geometry(self):
        s = self.read("init")
        self.assertIn("USRDATA_START=2104320", s)
        self.assertEqual(0x40380000 // 512, 2104320)
        self.assertIn("USRDATA_SECTORS=1638400", s)
        self.assertEqual(0x32000000 // 512, 1638400)

    def test_mdev_conf(self):
        s = self.read("etc/mdev.conf")
        self.assertNotIn("%", s)  # regexp renames are not compiled into our busybox
        for k in ("fb[0-9]+", "mmcblk", "ttyGS", "input/.*"):
            self.assertIn(k, s)

    def test_inittab_keeps_console(self):
        self.assertIn("::respawn:/bin/console-shell", self.read("etc/inittab"))


@unittest.skipUnless(shutil.which("cpio") and CPIO.exists(), "no built rootfs.cpio.gz")
class Rootfs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw = gzip.decompress(CPIO.read_bytes())
        out = subprocess.run(["cpio", "-tv", "--quiet"], input=raw, capture_output=True, check=True).stdout.decode()
        cls.entries = {}
        for line in out.splitlines():
            m = re.match(r"(\S{10})\s+\d+\s+\S+\s+\S+\s+\d+\s+\S+\s+\d+\s+(?:\d+|\S+)\s+(\S+)(?: -> (\S+))?$", line)
            if m:
                cls.entries[m.group(2).lstrip("./") or "."] = (m.group(1), m.group(3))

    def mode(self, name):
        self.assertIn(name, self.entries, f"{name} missing from cpio")
        return self.entries[name][0]

    def test_init_and_scripts(self):
        self.assertEqual(self.mode("init"), "-rwxr-xr-x")
        for n in ("bin/diag.sh", "bin/respawn", "bin/console-shell"):
            self.assertEqual(self.mode(n), "-rwxr-xr-x", n)
        self.assertTrue(self.mode("etc/mdev.conf").startswith("-rw"))
        self.assertTrue(self.mode("etc/inittab").startswith("-rw"))

    def test_binaries(self):
        self.assertEqual(self.mode("usr/bin/fbtool"), "-rwxr-xr-x")
        self.assertEqual(self.mode("bin/busybox")[0], "-")
        # applets we call from init/diag are present (busybox symlinks)
        for n in ("mdev", "hexdump", "timeout", "telnetd", "getty", "httpd", "switch_root", "ifconfig", "mount",
                  "init", "killall", "setsid", "blkid", "fbset", "devmem", "nc", "wget"):
            self.assertTrue(any(f"{d}/{n}" in self.entries for d in ("bin", "sbin", "usr/bin", "usr/sbin")),
                            f"applet {n} missing")

    def test_init_matches_overlay(self):
        raw = gzip.decompress(CPIO.read_bytes())
        with tempfile.TemporaryDirectory() as d:
            subprocess.run(["cpio", "-id", "--quiet", "init"], input=raw, cwd=d, check=True)
            self.assertEqual((Path(d) / "init").read_bytes(), (OVERLAY / "init").read_bytes())


IMG = {"recovery": (BUILD / "y2-rescue-recovery.img", BACKUP / "partitions" / "RECOVERY.bin", "RECOVERY"),
       "boot": (BUILD / "y2-rescue-boot.img", BACKUP / "partitions" / "BOOTIMG.bin", "ROOTFS")}


@unittest.skipUnless(CPIO.exists(), "no built rootfs.cpio.gz")
class Images(unittest.TestCase):
    def unpack(self, path, sub):
        d = Path(self._t.name) / sub
        self.assertEqual(tool.main(["unpack", str(path), str(d)]), 0)
        return d

    def setUp(self):
        self._t = tempfile.TemporaryDirectory()
        self.addCleanup(self._t.cleanup)

    def check(self, key):
        img, stock, name = IMG[key]
        if not (img.exists() and stock.exists()):
            self.skipTest(f"{img} or {stock} absent")
        self.assertLessEqual(img.stat().st_size, 16 * 1024 * 1024)
        new, old = self.unpack(img, "new"), self.unpack(stock, "old")
        self.assertEqual((new / "kernel").read_bytes(), (old / "kernel").read_bytes())
        self.assertEqual((new / "kernel.mtkhdr").read_bytes(), (old / "kernel.mtkhdr").read_bytes())
        rd = (new / "ramdisk.cpio.gz").read_bytes()
        self.assertEqual(rd, CPIO.read_bytes())
        self.assertLessEqual(len(rd), 8 * 1024 * 1024)
        # MTK ramdisk header keeps the stock name (it is rewritten only for the new length)
        self.assertEqual(tool.mtk_name((new / "ramdisk.mtkhdr").read_bytes()), name)
        self.assertEqual(tool.mtk_name((old / "ramdisk.mtkhdr").read_bytes()), name)
        import json
        jn, jo = (json.loads((d / "bootimg.json").read_text()) for d in (new, old))
        for k in jo:
            if k != "id":
                self.assertEqual(jn[k], jo[k], k)
        self.assertNotEqual(jn["id"], jo["id"])  # id is the sha1 over the new ramdisk
        self.assertFalse((new / "trailer").exists())  # trimmed to the header's length

    def test_recovery_image(self):
        self.check("recovery")

    def test_boot_image(self):
        self.check("boot")


if __name__ == "__main__":
    unittest.main()

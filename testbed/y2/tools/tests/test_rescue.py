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


class KeyHeld(unittest.TestCase):
    """key_held() from rescue's init, run under sh on captured evdev bytes. A hold needs a real press: the touch wheel
    emits a ghost press/release pair 21 us apart at power-on."""

    GHOST = bytes.fromhex(
        "259ab16a86a50a00" "01006700" "01000000" "259ab16a8ea50a00" "00000000" "00000000"
        "259ab16a9ba50a00" "01006700" "00000000" "259ab16a9ea50a00" "00000000" "00000000")

    @staticmethod
    def ev(sec, usec, typ, code, val):
        import struct
        return struct.pack("<IIHHi", sec, usec, typ, code, val)

    def held(self, data):
        s = (OVERLAY / "init").read_text()
        i = s.index("HOLD_MIN_US=")
        j = s.index("\nrm -f /tmp/key.*", i)
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "key.event2"
            f.write_bytes(data)
            r = subprocess.run(["sh", "-c", s[i:j] + '\nkey_held "$1"', "sh", str(f)], capture_output=True, text=True)
        self.assertEqual(r.stderr, "")
        return r.returncode == 0

    def test_captured_ghost_event_does_not_hold(self):
        self.assertEqual(len(self.GHOST), 64)
        self.assertFalse(self.held(self.GHOST))

    def test_200ms_press_holds(self):
        t = 1790000421
        self.assertTrue(self.held(self.ev(t, 100000, 1, 0x67, 1) + self.ev(t, 100001, 0, 0, 0) +
                                  self.ev(t, 300000, 1, 0x67, 0) + self.ev(t, 300001, 0, 0, 0)))

    def test_press_across_a_second_boundary_holds(self):
        t = 1790000421
        self.assertTrue(self.held(self.ev(t, 900000, 1, 0x67, 1) + self.ev(t + 1, 100000, 1, 0x67, 0)))

    def test_press_still_down_at_end_of_window_holds(self):
        self.assertTrue(self.held(self.ev(1790000421, 500, 1, 0x67, 1)))

    def test_just_under_and_at_30ms(self):
        t = 1790000421
        self.assertFalse(self.held(self.ev(t, 0, 1, 0x67, 1) + self.ev(t, 29999, 1, 0x67, 0)))
        self.assertTrue(self.held(self.ev(t, 0, 1, 0x67, 1) + self.ev(t, 30000, 1, 0x67, 0)))

    def test_ghost_then_real_press_holds(self):
        t = 1790000421
        self.assertTrue(self.held(self.GHOST + self.ev(t, 0, 1, 0x67, 1) + self.ev(t + 1, 0, 1, 0x67, 0)))

    def test_glitch_on_another_key_during_a_real_hold(self):
        t = 1790000421
        self.assertTrue(self.held(self.ev(t, 0, 1, 103, 1) + self.ev(t, 1000, 1, 108, 1) +
                                  self.ev(t, 11000, 1, 108, 0) + self.ev(t, 100000, 1, 103, 0)))
        self.assertTrue(self.held(self.ev(t, 0, 1, 108, 1) + self.ev(t, 100, 1, 103, 1) +
                                  self.ev(t, 150, 1, 103, 0) + self.ev(t + 1, 0, 1, 108, 0)))

    def test_non_key_events_and_empty_file_do_not_hold(self):
        self.assertFalse(self.held(self.ev(1, 0, 3, 0x30, 1)))  # EV_ABS with value 1
        self.assertFalse(self.held(b""))
        self.assertFalse(self.held(self.GHOST[:12]))  # truncated record

    def test_sd_marker_and_cmdline_paths_unchanged(self):
        s = (OVERLAY / "init").read_text()
        self.assertIn('[ -e /mnt/sd/y2-hold ] && HOLD="y2-hold on SD"', s)
        self.assertIn('*" y2.hold "*) HOLD="y2.hold on cmdline"', s)


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

    def test_network_after_address_and_generic_iface(self):
        s = self.read("init")
        self.assertIn("usb0 | rndis0", s)
        self.assertIn("*android_usb*", s)
        net = s[s.index("# ---- 5."):s.index("# ---- 6.")]
        order = [net.index(k) for k in ("ifconfig \"$NETIF\" 192.168.7.2", "udhcpd -f", "telnetd -F -b 192.168.7.2",
                                         "httpd -f -p 192.168.7.2:80 -h /tmp")]
        self.assertEqual(order, sorted(order))
        self.assertNotIn("usb0 up", net)

    def test_udhcpd_conf_offers_only_the_host_address(self):
        c = self.read("etc/udhcpd.conf")
        lines = [l.split() for l in c.splitlines() if l.strip() and not l.startswith("#")]
        keys = {l[0] + (" " + l[1] if l[0] == "option" else "") for l in lines}
        self.assertIn(["start", "192.168.7.1"], lines)
        self.assertIn(["end", "192.168.7.1"], lines)
        self.assertIn(["interface", "@IFACE@"], lines)
        self.assertTrue(keys.isdisjoint({"option router", "option dns"}), keys)
        self.assertIn("lease_file /tmp/", c)

    def test_hold_uses_cat_on_event0_and_event2_only(self):
        s = self.read("init")
        hold = s[s.index("# A held key on event0"):s.index("# Find USRDATA")]
        self.assertIn("/dev/input/event0 /dev/input/event2", hold)
        self.assertIn('timeout $((HOLD_SECS + 1)) cat "$ev"', hold)
        self.assertLess(hold.index("sd_mount &&"), hold.index("cat \"$ev\""))  # readers start after the SD probe
        self.assertLess(hold.index("cat \"$ev\""), hold.index("sleep $HOLD_SECS"))
        self.assertIn("udhcpd", s[s.index("killall"):s.index("killall") + 60])
        self.assertNotIn("hexdump -", hold)

    def test_diag_capture_and_flags(self):
        s = self.read("bin/diag.sh")
        self.assertIn("--no-capture | --no-input", s)
        self.assertIn('timeout 10 cat "$ev"', s)
        self.assertNotIn("hexdump -C \"$ev\"", s)
        for k in ("h2w/state", "lcd-backlight"):
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
        self.assertTrue(self.mode("etc/udhcpd.conf").startswith("-rw"))

    def test_binaries(self):
        self.assertEqual(self.mode("usr/bin/fbtool"), "-rwxr-xr-x")
        self.assertEqual(self.mode("bin/busybox")[0], "-")
        # applets we call from init/diag are present (busybox symlinks)
        for n in ("mdev", "hexdump", "timeout", "telnetd", "getty", "httpd", "switch_root", "ifconfig", "mount",
                  "init", "killall", "setsid", "blkid", "fbset", "devmem", "nc", "wget", "udhcpd", "od", "awk",
                  "readlink", "sed", "cat"):
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

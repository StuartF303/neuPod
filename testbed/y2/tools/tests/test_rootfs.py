"""Checks for the y2 glibc rootfs (T1.4).

Source checks (defconfig, overlay scripts) always run. Image checks skip cleanly when the built image
(~/y2-build/y2-rootfs.ext4, override with Y2_BUILD) is absent, e.g. in CI.
"""
import os, re, shutil, stat, subprocess, tempfile, unittest
from pathlib import Path

Y2 = Path(__file__).resolve().parents[2]
BR = Y2 / "buildroot"
OVERLAY = BR / "board" / "y2" / "overlay"
DEFCONFIG = BR / "configs" / "y2_defconfig"
BUILD = Path(os.environ.get("Y2_BUILD", "~/y2-build")).expanduser()
IMG = BUILD / "y2-rootfs.ext4"
MAX_IMAGE = 0x31F00000
# Not mountable by the stock 3.4.67 kernel.
FORBIDDEN = {"metadata_csum", "64bit", "orphan_file", "metadata_csum_seed", "inline_data", "encrypt",
             "casefold", "ea_inode", "large_dir", "project", "quota", "verity", "stable_inodes", "bigalloc"}
INIT_SCRIPTS = ["S00dev", "S01mounts", "S01wdk", "S10usb-gadget", "S20display", "S50dropbear", "S90neupod-demo", "S99diag"]
# Buildroot scripts that stay in the image (S02sysctl only if /etc/sysctl.conf has content).
KEPT_BR_SCRIPTS = {"S01seedrng"}


class Sources(unittest.TestCase):
    def setUp(self):
        self.cfg = DEFCONFIG.read_text()

    def test_defconfig_core_choices(self):
        for line in ("BR2_TOOLCHAIN_BUILDROOT_GLIBC=y", "BR2_TOOLCHAIN_BUILDROOT_CXX=y", "BR2_ARM_EABIHF=y",
                     "BR2_ARM_FPU_NEON_VFPV4=y", "BR2_cortex_a7=y", "BR2_REPRODUCIBLE=y",
                     "BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_3_4=y", "BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV=y",
                     "BR2_INIT_BUSYBOX=y", "BR2_PACKAGE_Y2_FBTOOL=y",
                     "BR2_PACKAGE_DROPBEAR=y", 'BR2_TARGET_ROOTFS_EXT2_LABEL="y2root"'):
            self.assertIn(line + "\n", self.cfg, line)

    def test_no_bluetooth_stack(self):
        self.assertNotRegex(self.cfg, r"(?m)^BR2_PACKAGE_(BLUEZ|BLUEZ5|BLUEZ_ALSA)\w*=y")

    def test_mkfs_options_drop_unsupported_features(self):
        m = re.search(r'BR2_TARGET_ROOTFS_EXT2_MKFS_OPTIONS="([^"]*)"', self.cfg)
        self.assertIsNotNone(m)
        for f in ("metadata_csum", "64bit", "orphan_file"):
            self.assertIn("^" + f, m.group(1))

    def test_fs_size_within_usrdata(self):
        m = re.search(r'BR2_TARGET_ROOTFS_EXT2_SIZE="(\d+)M"', self.cfg)
        self.assertIsNotNone(m)
        self.assertLessEqual(int(m.group(1)) << 20, MAX_IMAGE)

    def test_no_ssh_key_in_tree(self):
        self.assertFalse((OVERLAY / "root").exists())
        for p in BR.rglob("*"):
            if p.is_file():
                self.assertNotEqual(p.name, "authorized_keys", p)
                self.assertNotRegex(p.read_text(errors="ignore"), r"ssh-(ed25519|rsa) AAAA", p)

    def test_init_scripts_executable_with_shebang(self):
        for n in INIT_SCRIPTS:
            p = OVERLAY / "etc" / "init.d" / n
            self.assertTrue(p.stat().st_mode & stat.S_IXUSR, n)
            self.assertEqual(p.read_text().splitlines()[0], "#!/bin/sh", n)

    @unittest.skipUnless(shutil.which("dash"), "dash not installed")
    def test_posix_syntax(self):
        files = [OVERLAY / "etc/init.d" / n for n in INIT_SCRIPTS] + [OVERLAY / "usr/sbin/y2-console"]
        for p in files:
            r = subprocess.run(["dash", "-n", str(p)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, f"{p.name}: {r.stderr}")

    def test_bash_syntax_of_build_scripts(self):
        for p in (Y2 / "rootfs/build-rootfs.sh", BR / "board/y2/post-build.sh", BR / "board/y2/post-image.sh"):
            r = subprocess.run(["bash", "-n", str(p)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, f"{p.name}: {r.stderr}")

    def test_gadget_ids_differ_from_rescue(self):
        s = (OVERLAY / "etc/init.d/S10usb-gadget").read_text()
        self.assertIn("idProduct 4ee8", s)
        self.assertIn("02:59:32:00:00:03", s)
        self.assertIn("iProduct y2-linux", s)
        self.assertIn("192.168.7.2", s)

    def test_watchdog_and_dotnet_env(self):
        self.assertIn('echo "0 20 30" >/proc/wdk', (OVERLAY / "etc/init.d/S01wdk").read_text())
        d = (OVERLAY / "etc/profile.d/dotnet.sh").read_text()
        self.assertIn("DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1", d)
        self.assertIn("DOTNET_EnableWriteXorExecute=0", d)

    def test_dropbear_is_key_only(self):
        self.assertIn('DROPBEAR_ARGS="-s"', (OVERLAY / "etc/default/dropbear").read_text())

    def test_mounts_sorts_after_dev_and_gets_lo(self):
        names = sorted(INIT_SCRIPTS)
        self.assertLess(names.index("S00dev"), names.index("S01mounts"))
        m = (OVERLAY / "etc/init.d/S01mounts").read_text()
        self.assertLess(m.index("mount -a"), m.index("mkdir -p /run/lock"))
        self.assertIn("ifconfig lo", m)

    def test_console_uses_sysfs_and_never_execs_getty(self):
        c = (OVERLAY / "usr/sbin/y2-console").read_text()
        self.assertIn("/sys/class/tty/ttyGS0", c)
        self.assertNotIn("exec ", c)
        self.assertRegex(c, r"getty[^\n]*\nsleep 2")

    def test_diag_is_gated_and_off_by_default(self):
        d = (OVERLAY / "etc/init.d/S99diag").read_text()
        self.assertIn("/etc/y2-diag-on-boot", d)
        self.assertIn("Y2_DIAG_NO_SD=1", d)
        self.assertNotIn("/var/log", d.split("\n", 1)[1].replace("# ", ""))
        self.assertFalse((OVERLAY / "etc/y2-diag-on-boot").exists())

    def test_post_build_validates_key_and_prunes_scripts(self):
        pb = (BR / "board/y2/post-build.sh").read_text()
        for needle in ("ssh-keygen -l -f", "PRIVATE KEY", "S01syslogd", "S50crond", "S11modules", "S40network",
                       "source-date-epoch", "describe --always --dirty"):
            self.assertIn(needle, pb)
        self.assertNotIn("GETTY_PORT", self.cfg)

    def test_root_not_fscked_or_remounted(self):
        for rel in ("etc/inittab", "etc/fstab"):
            s = OVERLAY.joinpath(rel).read_text()
            self.assertNotRegex(s, r"(?m)^[^#]*(fsck|remount)", rel)


@unittest.skipUnless(IMG.exists() and shutil.which("debugfs"), f"{IMG} not built (run rootfs/build-rootfs.sh)")
class Image(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def dbg(self, cmd):
        return subprocess.run(["debugfs", "-R", cmd, str(IMG)], capture_output=True, text=True).stdout

    def extract(self, path):
        out = Path(self.tmp) / path.strip("/").replace("/", "_")
        if not out.exists():
            subprocess.run(["debugfs", "-R", f"dump {path} {out}", str(IMG)], capture_output=True, check=True)
        return out

    def file(self, path):
        return subprocess.run(["file", "-L", str(self.extract(path))], capture_output=True, text=True).stdout

    def test_size_within_usrdata(self):
        self.assertLessEqual(IMG.stat().st_size, MAX_IMAGE)

    def test_features_mountable_by_3_4(self):
        hdr = subprocess.run(["dumpe2fs", "-h", str(IMG)], capture_output=True, text=True).stdout
        m = re.search(r"^Filesystem features:\s*(.*)$", hdr, re.M)
        self.assertIsNotNone(m)
        feats = set(m.group(1).split())
        self.assertFalse(feats & FORBIDDEN, feats & FORBIDDEN)
        self.assertIn("extent", feats)
        self.assertRegex(hdr, r"Filesystem volume name:\s+y2root")

    def test_required_paths(self):
        for p in ("/sbin/init", "/dev", "/proc", "/sys", "/usr/bin/diag.sh", "/etc/profile.d/dotnet.sh",
                  "/etc/y2-release", "/usr/bin/fbtool", "/usr/sbin/dropbear"):
            self.assertIn("Inode:", self.dbg(f"stat {p}"), p)

    def test_init_scripts_mode_0755(self):
        for n in INIT_SCRIPTS:
            out = self.dbg(f"stat /etc/init.d/{n}")
            self.assertRegex(out, r"Mode:\s+0755", n)

    def test_init_script_set_is_exactly_ours(self):
        out = self.dbg("ls /etc/init.d")
        found = {n for n in out.split() if re.fullmatch(r"S\d\d\S+", n)}
        extra = found - set(INIT_SCRIPTS) - KEPT_BR_SCRIPTS - {"S02sysctl"}
        self.assertFalse(extra, extra)
        self.assertTrue(set(INIT_SCRIPTS) <= found)

    def test_sbin_init_is_relative_symlink_or_file(self):
        out = self.dbg("stat /sbin/init")
        m = re.search(r'Fast link dest: "([^"]*)"', out)
        if m:
            self.assertFalse(m.group(1).startswith("/"), m.group(1))  # absolute would escape /newroot
        else:
            self.assertIn("regular", out)

    def test_busybox_needed_libs_present(self):
        bb = self.extract("/bin/busybox")
        needed = re.findall(r"Shared library: \[([^\]]+)\]",
                            subprocess.run(["readelf", "-d", str(bb)], capture_output=True, text=True).stdout)
        self.assertTrue(needed)
        for lib in needed:
            hit = any("Inode:" in self.dbg(f"stat {d}/{lib}") for d in ("/lib", "/usr/lib"))
            self.assertTrue(hit, lib)

    def test_release_stamp_labels(self):
        r = self.dbg("cat /etc/y2-release")
        self.assertIn("source-date-epoch", r)
        self.assertNotIn("built ", r)

    def test_no_mdev_daemon_script(self):
        self.assertNotIn("Inode:", self.dbg("stat /etc/init.d/S10mdev"))

    def test_glibc_present(self):
        self.assertIn("Inode:", self.dbg("stat /lib/ld-linux-armhf.so.3"))
        names = self.dbg("ls /lib")
        self.assertRegex(names, r"libc\.so\.6")
        libc = self.extract("/lib/libc.so.6")
        s = subprocess.run(["strings", str(libc)], capture_output=True, text=True).stdout
        self.assertRegex(s, r"GNU C Library .*release version 2\.\d+")

    def test_binaries_are_arm_hardfloat_dynamic(self):
        for p in ("/bin/busybox", "/usr/bin/strace"):
            out = self.file(p)
            self.assertIn("ELF 32-bit LSB", out, p)
            self.assertIn("ARM, EABI5", out, p)
            self.assertIn("dynamically linked", out, p)
            self.assertIn("/lib/ld-linux-armhf.so.3", out, p)  # the hard-float loader

    def test_root_login_is_key_only(self):
        self.assertIn('DROPBEAR_ARGS="-s"', self.dbg("cat /etc/default/dropbear"))
        ssh = self.dbg("stat /root/.ssh/authorized_keys")
        if "Inode:" in ssh:
            self.assertRegex(ssh, r"Mode:\s+0600")


if __name__ == "__main__":
    unittest.main()

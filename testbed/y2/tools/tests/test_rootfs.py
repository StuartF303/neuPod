"""Checks for the y2 glibc rootfs (T1.4).

Source checks (defconfig, overlay scripts) always run. Image checks skip cleanly when the built image
(~/y2-build/y2-rootfs.ext4, override with Y2_BUILD) is absent, e.g. in CI.
"""
import os, re, shutil, stat, subprocess, tempfile, time, unittest
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
INIT_SCRIPTS = ["S00dev", "S01mounts", "S01wdk", "S10usb-gadget", "S20display", "S50dropbear", "S90neupod-demo", "S91neupod-fw", "S99diag"]
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


class S91EnvFile(unittest.TestCase):
    """S91neupod-fw sandbox: /etc/neupod-fw.env is optional, and only a root-owned, not group/world-writable regular file
    is sourced. The script's absolute paths are rewritten into a temp dir and a fake app records its environment."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="s91env-"))
        t = self.tmp
        src = (OVERLAY / "etc" / "init.d" / "S91neupod-fw").read_text()
        for a, b in (("/opt/neupod-fw/neupod", f"{t}/app"), ("/etc/neupod-ui", f"{t}/ui"), ("/run/", f"{t}/run-"),
                     ("/tmp/neupod-fw.log", f"{t}/log")):
            src = src.replace(a, b)
        self.script = t / "S91"
        self.script.write_text(src)
        (t / "ui").write_text("fw\n")
        (t / "app").write_text(f'#!/bin/sh\necho "[$NEUPOD_Y2_HAPTIC_MS]" > {t}/seen\nexec sleep 30\n')
        (t / "app").chmod(0o755)
        self.env = dict(os.environ, NEUPOD_BOOT_MODE_FILE=str(t / "none"), NEUPOD_FW_ENV_FILE=str(t / "fw.env"),
                        NEUPOD_FW_ENV_UID=str(os.getuid()), NEUPOD_Y2_HAPTIC_MS="")

    def tearDown(self):
        self.stop()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def stop(self):
        subprocess.run(["sh", str(self.script), "stop"], env=self.env, capture_output=True)
        (self.tmp / "seen").unlink(missing_ok=True)

    def run_start(self, **extra):
        env = dict(self.env, **extra)
        out = subprocess.run(["sh", str(self.script), "start"], env=env, capture_output=True, text=True, timeout=20).stdout
        seen = self.tmp / "seen"
        for _ in range(50):
            if seen.exists() and seen.read_text().strip():
                break
            time.sleep(0.1)
        return out, seen.read_text().strip() if seen.exists() else None

    def env_file(self, text, mode=0o644):
        f = self.tmp / "fw.env"
        f.unlink(missing_ok=True)
        f.write_text(text)
        f.chmod(mode)
        return f

    def test_no_env_file_is_fine(self):
        out, seen = self.run_start()
        self.assertEqual(seen, "[]")
        self.assertEqual(out, "")

    def test_var_from_env_file_reaches_the_app(self):
        self.env_file('NEUPOD_Y2_HAPTIC_MS="20,40,90"\n')
        out, seen = self.run_start()
        self.assertEqual(seen, "[20,40,90]")
        self.assertEqual(out, "")

    def test_world_writable_env_file_is_ignored(self):
        for mode in (0o666, 0o664, 0o646):
            self.stop()
            self.env_file('NEUPOD_Y2_HAPTIC_MS="99"\n', mode)
            out, seen = self.run_start()
            self.assertEqual(seen, "[]", oct(mode))
            self.assertIn("ignored (writable by group or others)", out)

    def test_wrong_owner_bad_syntax_and_symlink_are_ignored(self):
        self.env_file('NEUPOD_Y2_HAPTIC_MS="99"\n')
        out, seen = self.run_start(NEUPOD_FW_ENV_UID="123456")
        self.assertEqual(seen, "[]")
        self.assertIn("not owned by root", out)
        self.stop()
        self.env_file('NEUPOD_Y2_HAPTIC_MS="99\nif then\n')
        out, seen = self.run_start()
        self.assertEqual(seen, "[]")
        self.assertIn("syntax error", out)
        self.stop()
        real = self.tmp / "real.env"
        real.write_text('NEUPOD_Y2_HAPTIC_MS="99"\n')
        real.chmod(0o644)
        (self.tmp / "fw.env").unlink()
        (self.tmp / "fw.env").symlink_to(real)
        out, seen = self.run_start()
        self.assertEqual(seen, "[]")
        self.assertIn("not a regular file", out)

    def test_deploy_script_never_touches_the_env_file(self):
        self.assertNotIn("neupod-fw.env", (Y2 / "scripts" / "deploy-fw.sh").read_text())


class S80MtkBt(unittest.TestCase):
    """S80mtk-bt creates /dev/stpwmt, /dev/stpbt and /dev/wmtWifi. The kernel registers those drivers in /proc/devices
    under different names (below, copied from the real device); looking the node name up there created no node at all.
    mknod needs root, so a fake mknod on PATH records its arguments."""

    PROC_DEVICES = """Character devices:
  1 mem
  4 tty
141 mtk_stp_usktrk
153 mtk_wmt_WIFI_chrdev
190 mtk_stp_wmt
191 mtk_stp_GPS_chrdev
192 mtk_stp_BT_chrdev

Block devices:
  8 sd
179 mmc
"""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="s80-"))
        t = self.tmp
        for d in ("bin", "dev", "run", "stage", "fakebin"):
            (t / d).mkdir()
        for n, body in (("wmt_loader", "exit 255"), ("6620_launcher", "exec sleep 30")):
            (t / "bin" / n).write_text("#!/bin/sh\n" + body + "\n")
            (t / "bin" / n).chmod(0o755)
        (t / "fakebin" / "mknod").write_text('#!/bin/sh\necho "$@" >> "%s/mknod.log"\n: > "$1"\n' % t)
        (t / "fakebin" / "mknod").chmod(0o755)
        (t / "proc-devices").write_text(self.PROC_DEVICES)
        self.env = dict(os.environ, PATH=f"{t}/fakebin:" + os.environ["PATH"], MTK_STAGE=str(t / "stage"),
                        MTK_BIN=str(t / "bin"), MTK_DEV=str(t / "dev"), MTK_RUN=str(t / "run"),
                        MTK_PROC_DEVICES=str(t / "proc-devices"), MTK_WAIT_S="0")

    def tearDown(self):
        self.run_s80("stop")
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_s80(self, action="start"):
        return subprocess.run(["sh", str(OVERLAY / "etc" / "init.d" / "S80mtk-bt"), action], env=self.env,
                              capture_output=True, text=True, timeout=20)

    def mknods(self):
        log = self.tmp / "mknod.log"
        return sorted(log.read_text().splitlines()) if log.exists() else []

    def test_nodes_use_the_kernels_registered_names(self):
        r = self.run_s80()
        self.assertEqual(r.returncode, 0, r.stdout)
        # The node is "<dev>/<name> c <major> 0": majors of mtk_wmt_WIFI_chrdev, mtk_stp_BT_chrdev, mtk_stp_wmt.
        d = self.tmp / "dev"
        self.assertEqual(self.mknods(), sorted([f"{d}/stpwmt c 190 0", f"{d}/stpbt c 192 0", f"{d}/wmtWifi c 153 0"]))
        for n in ("stpwmt", "stpbt", "wmtWifi"):
            self.assertEqual((d / n).stat().st_mode & 0o777, 0o660, n)
        self.assertNotIn("is not in", r.stdout)

    def test_a_proc_devices_with_only_the_node_names_creates_nothing(self):
        # What the old script expected; the real kernel never says this.
        (self.tmp / "proc-devices").write_text("Character devices:\n190 stpwmt\n192 stpbt\n153 wmtWifi\n")
        r = self.run_s80()
        self.assertEqual(self.mknods(), [])
        self.assertIn("mtk_stp_BT_chrdev", r.stdout)

    def test_block_device_with_same_name_is_not_used(self):
        (self.tmp / "proc-devices").write_text("Character devices:\n190 mtk_stp_wmt\n\nBlock devices:\n192 mtk_stp_BT_chrdev\n")
        self.run_s80()
        self.assertEqual(self.mknods(), [f"{self.tmp}/dev/stpwmt c 190 0"])

    def test_loader_runs_once_per_boot_and_existing_nodes_are_kept(self):
        self.run_s80()
        self.run_s80("stop")
        n = len(self.mknods())
        (self.tmp / "bin" / "wmt_loader").write_text("#!/bin/sh\necho again >> %s/loader-ran\n" % self.tmp)
        self.run_s80()
        self.assertFalse((self.tmp / "loader-ran").exists())
        self.assertEqual(len(self.mknods()), n)

    def test_nothing_writes_to_the_wifi_node(self):
        s = (OVERLAY / "etc" / "init.d" / "S80mtk-bt").read_text()
        self.assertNotRegex(s, r"(?m)^[^#]*(>>?|tee|dd of=)\s*\"?\$DEV/wmtWifi")


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

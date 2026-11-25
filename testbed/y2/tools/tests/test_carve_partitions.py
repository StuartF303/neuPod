import contextlib, shutil, hashlib, importlib.util, io, os, struct, tempfile, unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("carve_partitions", Path(__file__).parent.parent / "carve-partitions.py")
tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tool)

FW = Path(os.environ.get("Y2_FIRMWARE", "~/y2-firmware/y2_v3.2.0_FM-20260813")).expanduser()
sha = lambda b: hashlib.sha256(b).hexdigest()


def entry(typ, lba, secs):
    return b"\0\0\0\0" + bytes([typ]) + b"\0\0\0" + struct.pack("<II", lba, secs)


def sector(*ents):
    return (b"\0" * 446 + b"".join(ents)).ljust(510, b"\0") + b"\x55\xaa"


S = 512
DEV = 0x20000
# name, start, size, region
LAYOUT = [("MBR", 0, 0x200), ("NVRAM", 0x200, 0xe00), ("EBR1", 0x1000, 0x200), ("PRI", 0x2000, 0x1000),
          ("LOGA", 0x4000, 0x2000), ("EBR2", 0x8000, 0x200), ("LOGB", 0xA000, 0x1000), ("FAT", 0xC000, 0)]
SCATTER = "- general: MTK_PLATFORM_CFG\n  info:\n    - platform: X\n" + "".join(
    f"- partition_index: SYS{i}\n  partition_name: {n}\n  physical_start_addr: {s:#x}\n"
    f"  partition_size: {z:#x}\n  region: EMMC_USER\n" for i, (n, s, z) in enumerate(LAYOUT)) + (
    "- partition_index: SYS9\n  partition_name: PRELOADER\n  physical_start_addr: 0x0\n"
    "  partition_size: 0x1400000\n  region: EMMC_BOOT_1\n"
    "- partition_index: SYS10\n  partition_name: BMTPOOL\n  physical_start_addr: 0xffff00a8\n"
    "  partition_size: 0x1000\n  region: EMMC_USER\n")


def make_device():
    dev = bytearray(os.urandom(DEV))
    mbr = sector(entry(5, 0x1000 // S, 0xffffffff), entry(0x83, 0x2000 // S, 0x1000 // S))
    # logical LBAs relative to their own EBR, link relative to extended start (0x1000)
    ebr1 = sector(entry(0x83, (0x4000 - 0x1000) // S, 0x2000 // S), entry(5, (0x8000 - 0x1000) // S, 0x200 // S))
    ebr2 = sector(entry(0x83, (0xA000 - 0x8000) // S, 0x1000 // S),
                  entry(0x83, (0xC000 - 0x8000) // S, 0xfffff000))  # bogus FAT size, runs to end
    dev[0:S], dev[0x1000:0x1000 + S], dev[0x8000:0x8000 + S] = mbr, ebr1, ebr2
    return bytes(dev), mbr, ebr1, ebr2


def run(*args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = tool.main([str(a) for a in args])
    return rc, out.getvalue(), err.getvalue()


class Synthetic(unittest.TestCase):
    BASE = 0  # zero prefix before device address 0, like the real dump (0xb80000)

    def setUp(self):
        self._t = tempfile.TemporaryDirectory()
        self.addCleanup(self._t.cleanup)
        self.tmp = Path(self._t.name)
        self.dev, self.mbr, self.ebr1, self.ebr2 = make_device()
        self.boot = os.urandom(0x1000)
        self.bk = self.tmp / "bk"
        self.bk.mkdir()
        (self.bk / "emmc_user.bin").write_bytes(bytes(self.BASE) + self.dev)
        (self.bk / "emmc_boot1.bin").write_bytes(self.boot)
        self.sc = self.tmp / "scatter.txt"
        self.sc.write_text(SCATTER)
        self.boot2 = os.urandom(0x1000)
        (self.bk / "emmc_boot2.bin").write_bytes(self.boot2)
        self.sums()

    def sums(self):
        (self.bk / "SHA256SUMS").write_text("".join(
            f"{sha((self.bk / n).read_bytes())}  {n}\n" for n in tool.BACKUP_FILES))

    def carve(self, *extra, scatter=None):
        return run("carve", self.bk, "--scatter", scatter or self.sc, "--expect-user-size", DEV + self.BASE,
                   "--expect-boot-size", 0x1000, *extra)

    def test_table_dump(self):
        rc, out, _ = run("table", self.bk / "emmc_user.bin", "--scatter", self.sc)
        self.assertEqual(rc, 0)
        self.assertIn(f"base: {self.BASE:#x}", out)
        for n in ("PRI", "LOGA", "LOGB", "FAT"):
            line = next(l for l in out.splitlines() if f" {n} " in l)
            self.assertIn("ok", line)
        self.assertIn("scatter-only", next(l for l in out.splitlines() if "NVRAM" in l))
        self.assertNotIn("MISMATCH", out)

    def test_table_files(self):
        for n, d in (("mbr", self.mbr), ("e1", self.ebr1), ("e2", self.ebr2)):
            (self.tmp / n).write_bytes(d)
        rc, out, _ = run("table", self.tmp / "mbr", "--scatter", self.sc, "--ebr1", self.tmp / "e1", "--ebr2", self.tmp / "e2")
        self.assertEqual(rc, 0)
        self.assertIn("LOGB", out)
        self.assertIn("to end", out)

    def test_carve(self):
        rc, out, _ = self.carve()
        self.assertEqual(rc, 0)
        p = self.bk / "partitions"
        for n, s, z in LAYOUT:
            z = z or DEV - s
            self.assertEqual((p / f"{n}.bin").read_bytes(), self.dev[s:s + z], n)
        self.assertEqual((p / "PRELOADER.bin").read_bytes(), self.boot)
        self.assertFalse((p / "BMTPOOL.bin").exists())
        sums = dict(reversed(l.split("  ")) for l in (p / "SHA256SUMS").read_text().splitlines())
        self.assertEqual(sums["FAT.bin"], sha(self.dev[0xC000:]))
        self.assertEqual(sums["PRELOADER.bin"], sha(self.boot))
        self.assertEqual(len(sums), len(LAYOUT) + 1)
        md = (p / "table.md").read_text()
        self.assertIn(f"| FAT | EMMC_USER | 0xc000 | {0xC000 + self.BASE:#x} |", md)
        self.assertIn("DA addr", md)

    def test_base_subcommand(self):
        rc, out, _ = run("base", self.bk / "emmc_user.bin", "--scatter", self.sc)
        self.assertEqual((rc, out.strip()), (0, f"{self.BASE:#x}"))
        rc, out, _ = run("table", self.bk / "emmc_user.bin", "--scatter", self.sc, "--base", self.BASE + 0x200)
        self.assertEqual(rc, 1)  # wrong forced base: no 55AA at that offset

    def test_skip_and_force(self):
        rc, *_ = self.carve("--skip", "FAT")
        self.assertEqual(rc, 0)
        p = self.bk / "partitions"
        self.assertFalse((p / "FAT.bin").exists())
        self.assertNotIn("FAT.bin", (p / "SHA256SUMS").read_text())
        rc, _, err = self.carve()
        self.assertEqual(rc, 1)
        self.assertTrue(err.startswith("error:") and err.count("\n") == 1)
        rc, *_ = self.carve("--force")
        self.assertEqual(rc, 0)
        self.assertTrue((p / "FAT.bin").exists())
        olds = list(self.bk.glob("partitions.old-*"))
        self.assertEqual(len(olds), 1)
        self.assertFalse((olds[0] / "FAT.bin").exists())  # earlier --skip FAT run kept intact
        self.assertFalse((self.bk / "partitions.tmp").exists())

    def test_inputs_unmodified_and_short_dump(self):
        (self.bk / "emmc_user.bin").write_bytes(bytes(self.BASE) + self.dev[:0xB000])
        self.sums()
        rc, _, err = run("carve", self.bk, "--scatter", self.sc, "--expect-user-size", 0xB000 + self.BASE,
                         "--expect-boot-size", 0x1000, "--out", self.tmp / "o")
        self.assertEqual(rc, 1)
        self.assertFalse((self.tmp / "o").exists())
        self.assertIn("dump too short", err)
        self.assertEqual((self.bk / "emmc_user.bin").read_bytes(), bytes(self.BASE) + self.dev[:0xB000])
        self.assertFalse((self.tmp / "o" / "SHA256SUMS").exists())

    def fails(self, *extra, msg, **kw):
        out = self.tmp / "o"
        rc, _, err = self.carve("--out", out, *extra, **kw)
        self.assertEqual(rc, 1, err)
        self.assertIn(msg, err)
        self.assertFalse(out.exists())
        self.assertFalse((self.tmp / "o.tmp").exists() and any((self.tmp / "o.tmp").iterdir()))

    def test_sha_mismatch_aborts(self):
        d = bytearray(bytes(self.BASE) + self.dev)
        d[self.BASE + 0x5000] ^= 1
        (self.bk / "emmc_user.bin").write_bytes(d)
        self.fails(msg="SHA256 mismatch")
        rc, *_ = self.carve("--out", self.tmp / "o", "--no-verify")
        self.assertEqual(rc, 0)

    def test_wrong_sizes_abort(self):
        rc, _, err = run("carve", self.bk, "--scatter", self.sc, "--expect-user-size", DEV + 1,
                         "--expect-boot-size", 0x1000, "--out", self.tmp / "o")
        self.assertEqual(rc, 1)
        self.assertIn("expected", err)
        rc, _, err = run("carve", self.bk, "--scatter", self.sc, "--expect-user-size", DEV + self.BASE,
                         "--out", self.tmp / "o")  # default boot size 4 MiB
        self.assertEqual(rc, 1)
        self.assertIn("emmc_boot1.bin is", err)
        self.assertFalse((self.tmp / "o").exists())

    def test_layout_mismatch_aborts(self):
        bad = self.tmp / "bad.txt"
        bad.write_text(SCATTER.replace("physical_start_addr: 0x4000", "physical_start_addr: 0x4200"))
        self.fails(msg="layout disagrees", scatter=bad)
        rc, *_ = self.carve("--out", self.tmp / "o", "--force-layout", scatter=bad)
        self.assertEqual(rc, 0)
        rc, out, _ = run("table", self.bk / "emmc_user.bin", "--scatter", bad)
        self.assertEqual(rc, 2)

    def test_boot_region_start_size(self):
        sc = self.tmp / "b.txt"
        sc.write_text(SCATTER.replace("partition_size: 0x1400000", "partition_size: 0x400").replace(
            "region: EMMC_BOOT_1", "region: EMMC_BOOT_1\n  x: 1").replace(
            "partition_name: PRELOADER\n  physical_start_addr: 0x0", "partition_name: PRELOADER\n  physical_start_addr: 0x200") +
            "- partition_index: SYS11\n  partition_name: B2\n  physical_start_addr: 0x0\n"
            "  partition_size: 0x0\n  region: EMMC_BOOT_2\n")
        rc, *_ = self.carve("--out", self.tmp / "o", scatter=sc)
        self.assertEqual(rc, 0)
        self.assertEqual((self.tmp / "o" / "PRELOADER.bin").read_bytes(), self.boot[0x200:0x600])
        self.assertEqual((self.tmp / "o" / "B2.bin").read_bytes(), self.boot2)
        sc.write_text(SCATTER.replace("physical_start_addr: 0x0\n  partition_size: 0x1400000",
                                      "physical_start_addr: 0x2000\n  partition_size: 0x1400000"))
        shutil.rmtree(self.tmp / "o")
        self.fails(msg="beyond emmc_boot1.bin", scatter=sc)

    def test_bad_names_rejected(self):
        for n in ("../evil", ".hidden", "a/b"):
            sc = self.tmp / "n.txt"
            sc.write_text(SCATTER.replace("partition_name: LOGA", f"partition_name: {n}"))
            self.fails(msg="not a plain filename", scatter=sc)

    def test_fat_size_from_bpb(self):
        bs = bytearray(S)
        bs[11:13] = struct.pack("<H", 512)
        bs[32:36] = struct.pack("<I", 0x10)  # 0x2000 bytes
        bs[0x52:0x5a] = b"FAT32   "
        bs[510:512] = b"\x55\xaa"
        dev = bytearray(self.dev)
        dev[0xC000:0xC000 + S] = bs
        (self.bk / "emmc_user.bin").write_bytes(bytes(self.BASE) + dev)
        self.sums()
        rc, out, err = self.carve("--out", self.tmp / "o")
        self.assertEqual(rc, 0, err)
        self.assertEqual((self.tmp / "o" / "FAT.bin").read_bytes(), bytes(dev[0xC000:0xE000]))
        self.assertIn("FAT: size 0x2000 taken from the FAT32 boot sector", (self.tmp / "o" / "table.md").read_text())
        rc, out, _ = run("table", self.bk / "emmc_user.bin", "--scatter", self.sc)
        self.assertIn("0x2000", next(l for l in out.splitlines() if " FAT " in l))


class SyntheticWithBase(Synthetic):
    BASE = 0x10000


@unittest.skipUnless((FW / "MBR").exists(), "factory package not present")
class Factory(unittest.TestCase):
    def test_table_matches_scatter(self):
        rc, out, _ = run("table", FW / "MBR", "--scatter", FW / "MT6582_Android_scatter.txt",
                         "--ebr1", FW / "EBR1", "--ebr2", FW / "EBR2")
        self.assertEqual(rc, 0)
        self.assertNotIn("MISMATCH", out)
        self.assertNotIn("NO MATCH", out)
        for n in ("PROTECT_F", "PROTECT_S", "SEC_RO", "ANDROID", "CACHE", "USRDATA", "FAT", "EBR1", "EBR2"):
            line = next(l for l in out.splitlines() if f" {n} " in l)
            self.assertNotIn("scatter-only", line)

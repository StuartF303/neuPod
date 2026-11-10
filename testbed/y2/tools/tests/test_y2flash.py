import contextlib, importlib.util, io, json, os, random, signal, tempfile, unittest
from types import SimpleNamespace
from unittest import mock
from pathlib import Path

_spec = importlib.util.spec_from_file_location("y2flash", Path(__file__).parent.parent / "y2flash.py")
y2 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(y2)

BOOT, REC = y2.TARGETS["BOOTIMG"], y2.TARGETS["RECOVERY"]


def rnd(n, seed):
    return random.Random(seed).randbytes(n)


CANWIN = 2 * 0xB80000 + 8192     # canary window length


class FakeDa:
    """Fake device over DEVICE addresses (one bytearray covering all regions). Reads map raw-read_base and
    writes map raw-write_base (or write_base_fn(raw), to model a non-linear device), like the real DA's two
    different commands. Writes need a preceding switch_user(), like the real adapter."""

    def __init__(self, regions, read_base=y2.READ_BASE, write_base=y2.READ_BASE, write_base_fn=None,
                 user_size=y2.EXPECT_EMMC_USER_SIZE, corrupt_at=None, fail_write=False, drop_writes=False,
                 drop_nth=None, truncate_to=None, interrupt_on_write=None):
        self.lo = min(a for a, _ in regions)
        hi = max(a + len(b) for a, b in regions)
        self.mem = bytearray(hi - self.lo)
        for a, b in regions:
            self.mem[a - self.lo:a - self.lo + len(b)] = b
        self.read_base, self.true_wb, self.write_base_fn = read_base, write_base, write_base_fn
        self.emmc_user_size = user_size
        self.corrupt_at, self.fail_write, self.drop_writes = corrupt_at, fail_write, drop_writes
        self.drop_nth, self.truncate_to, self.interrupt_on_write = drop_nth, truncate_to, interrupt_on_write
        self.events, self.log, self._sw, self._nwrites = [], [], False, 0

    def dev(self, raw, n, base):
        d = raw - base - self.lo
        if d < 0 or d + n > len(self.mem):
            raise IndexError(f"fake device: raw {raw:#x}+{n:#x} (base {base:#x}) outside modelled device")
        return d

    def device_bytes(self, dev_addr, n):
        return bytes(self.mem[dev_addr - self.lo:dev_addr - self.lo + n])

    def switch_user(self):
        self.log.append(("switch",))
        self._sw = True

    def read(self, raw, n):
        d = self.dev(raw, n, self.read_base)
        self.events.append(("read", raw, n))
        return bytes(self.mem[d:d + n])

    def write(self, raw, data):
        self.log.append(("write", raw))
        if not self._sw:
            raise RuntimeError("fake device: write without switch to the user area")
        self._sw = False
        self._nwrites += 1
        base = self.write_base_fn(raw) if self.write_base_fn else self.true_wb
        d = self.dev(raw, len(data), base)
        self.events.append(("write", raw, len(data)))
        if self.interrupt_on_write == self._nwrites:
            os.kill(os.getpid(), signal.SIGTERM)
        if self.fail_write:
            raise RuntimeError("usb stall")
        if self.drop_writes or self.drop_nth == self._nwrites:
            return
        if self.truncate_to:
            data = data[:self.truncate_to]
        self.mem[d:d + len(data)] = data
        if self.corrupt_at is not None and len(data) > 4096:
            self.mem[d + self.corrupt_at] ^= 0xFF

    def writes(self):
        return [e for e in self.events if e[0] == "write"]


def main_events(da):
    """Events without the canary phase (canary reads are CANWIN long, canary writes are 4096)."""
    return [e for e in da.events if e[2] not in (4096, CANWIN)]


def quiet(fn, *a, **k):
    with contextlib.redirect_stdout(io.StringIO()) as out:
        try:
            return fn(*a, **k), out.getvalue()
        except Exception as e:
            e.output = out.getvalue()
            raise


BYSTANDER = y2.Target("BYSTANDER", 0x3D80000, 0x1000000)   # the region right after RECOVERY (SEC_RO etc.)


class Base(unittest.TestCase):
    def setUp(self):
        self.stock_rec = rnd(REC.size, 1)
        self.stock_boot = rnd(BOOT.size, 2)
        self.by = rnd(BYSTANDER.size, 7)
        self.img = rnd(0x3000, 3)   # a small 512-aligned image
        self.bases = y2.Bases(read=0xB80000, write=0xB80000)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        # never depend on the environment's real .mtkclient-commit
        p = mock.patch.object(y2, "current_commit", lambda: "abc")
        p.start()
        self.addCleanup(p.stop)

    def da(self, **k):
        return FakeDa([(BOOT.start, self.stock_boot), (REC.start, self.stock_rec), (BYSTANDER.start, self.by)], **k)

    def rec_job(self, image=None, expected=None):
        exp = [("backup", self.stock_rec)] if expected is None else expected
        return y2.Job(REC, self.img if image is None else image, exp)

    def boot_job(self, image=None):
        return y2.Job(BOOT, image or rnd(0x1200, 4), [("backup", self.stock_boot)])

    def go(self, da, jobs, **k):
        return quiet(y2.run_jobs, da, jobs, self.bases, **k)


def dev(da, t, n=None):
    return da.device_bytes(t.start, t.size if n is None else n)


class Allowlist(Base):
    def test_only_three_targets_with_exact_device_ranges(self):
        self.assertEqual({n: (t.start, t.size) for n, t in y2.TARGETS.items()},
                         {"BOOTIMG": (0x1D80000, 0x1000000), "RECOVERY": (0x2D80000, 0x1000000),
                          "USRDATA": (0x40380000, 0x32000000)})
        # the DA (read) addresses given in the task: scatter + 0xb80000
        self.assertEqual([t.start + y2.READ_BASE for t in y2.TARGETS.values()],
                         [0x2900000, 0x3900000, 0x40F00000])

    def test_unknown_targets_refused(self):
        for name in ("NVRAM", "PRELOADER", "UBOOT", "SECCFG", "ANDROID", "recovery", ""):
            with self.assertRaises(y2.Y2Error):
                y2.get_target(name)

    def test_write_range_gate_in_device_terms(self):
        for addr in (0x0, 0x80000, 0x1D80000 - 512, 0x40380000 + 0x32000000, 0x2D80000 + 0x1000000,
                     0x3900000 + 0x1000000 - 0x100000 + 0x100000):   # RECOVERY end / SEC_RO side
            with self.assertRaises(y2.Y2Error, msg=hex(addr)):
                y2.check_write_range(addr, 512)
        with self.assertRaises(y2.Y2Error):      # spills past the end of RECOVERY
            y2.check_write_range(REC.end - 512, 1024)
        with self.assertRaises(y2.Y2Error):
            y2.check_write_range(REC.start + 1, 512)
        with self.assertRaises(y2.Y2Error):
            y2.check_write_range(REC.start, 0)
        self.assertIs(y2.check_write_range(REC.start, REC.size), REC)

    def test_guarded_write_refuses_nvram_and_touches_nothing(self):
        da = self.da()
        fake = y2.Target("RECOVERY", 0x80000, 0x1000)   # not the allowlist object
        with self.assertRaises(y2.SafetyViolation):
            y2.guarded_write(da, fake, fake.start, b"\0" * 512, 0xB80000)
        with self.assertRaises(y2.SafetyViolation):      # allowlist target, address outside it
            y2.guarded_write(da, REC, 0x80000, b"\0" * 512, 0xB80000)
        with self.assertRaises(y2.SafetyViolation):      # silly base
            y2.guarded_write(da, REC, REC.start, b"\0" * 512, 0x10000000)
        self.assertEqual(da.events, [])
        self.assertEqual(da.log, [])

    def test_safety_checks_survive_python_O(self):
        """They are explicit raises, not asserts."""
        import inspect
        src = inspect.getsource(y2)
        self.assertNotIn("\n    assert ", src)

    def test_oversize_image_refused(self):
        with self.assertRaises(y2.Y2Error):
            y2.Job(REC, b"\1" * (REC.size + 1), [("b", self.stock_rec)])
        with self.assertRaises(y2.Y2Error):
            y2.Job(REC, b"", [("b", self.stock_rec)])
        y2.Job(REC, b"\1" * REC.size, [("b", self.stock_rec)])

    def test_expected_file_must_be_full_size(self):
        p = Path(self.tmp.name) / "short.bin"
        p.write_bytes(b"x" * 10)
        with self.assertRaises(y2.Y2Error):
            y2.load_expected(REC, [str(p)])


class Padding(Base):
    def test_pads_to_512_with_zeros_only(self):
        odd = rnd(0x1001, 5)
        job = self.rec_job(odd)
        self.assertEqual(len(job.data), 0x1200)
        self.assertEqual(job.data[:0x1001], odd)
        self.assertEqual(job.data[0x1001:], b"\0" * (0x1200 - 0x1001))
        self.assertEqual(len(self.rec_job(rnd(0x1000, 6)).data), 0x1000)

    def test_unaligned_write_leaves_rest_untouched_and_reads_back_padded_length(self):
        odd = rnd(0x1001, 5)
        da = self.da()
        self.go(da, [self.rec_job(odd)])
        self.assertEqual([e for e in main_events(da) if e[0] == "write"], [("write", REC.start + 0xB80000, 0x1200)])
        self.assertEqual(dev(da, REC, 0x1200), self.rec_job(odd).data)
        self.assertEqual(dev(da, REC)[0x1200:], self.stock_rec[0x1200:])
        self.assertIn(("read", REC.start + 0xB80000, 0x1200), da.events)
        c = y2.canary_addr(REC)
        self.assertEqual(da.device_bytes(c, 4096), self.stock_rec[c - REC.start:c - REC.start + 4096])  # canary undone


class Flow(Base):
    def test_success_reads_before_and_after_and_reports_sha(self):
        da = self.da()
        res, out = self.go(da, [self.rec_job()])
        ev = main_events(da)
        self.assertEqual([e[0] for e in ev], ["read", "write", "read"])
        self.assertEqual(ev[0][2], REC.size)
        self.assertEqual(ev[2][2], len(self.img))
        self.assertEqual(dev(da, REC, len(self.img)), self.img)
        self.assertEqual(res[0][1], res[0][2])
        self.assertIn(y2.sha(self.img), out)

    def test_noop_flash_with_backup_image(self):
        da = self.da()
        self.go(da, [self.rec_job(self.stock_rec)])
        self.assertEqual(dev(da, REC), self.stock_rec)
        self.assertEqual(main_events(da)[1][2], REC.size)

    def test_write_uses_scatter_plus_write_base_and_read_uses_read_base(self):
        for wb in (0, 0xB80000):
            da = self.da(write_base=wb)
            quiet(y2.run_jobs, da, [self.rec_job()], y2.Bases(read=0xB80000, write=wb))
            self.assertEqual(main_events(da), [("read", REC.start + 0xB80000, REC.size),
                                               ("write", REC.start + wb, len(self.img)),
                                               ("read", REC.start + 0xB80000, len(self.img))])
            c = y2.canary_addr(REC)    # the canary also uses scatter + write base / read base
            self.assertIn(("write", c + wb, 4096), da.events)
            self.assertEqual(dev(da, REC, len(self.img)), self.img)

    def test_preread_mismatch_aborts_without_writing(self):
        da = self.da()
        da.mem[REC.start - da.lo + 0x1234] ^= 1
        with self.assertRaises(y2.PreReadMismatch) as cm:
            self.go(da, [self.rec_job()], keep_dir=self.tmp.name)
        self.assertIn("0x1234", cm.exception.args[0])
        self.assertIn("NOTHING WAS WRITTEN", cm.exception.args[0])
        self.assertEqual(da.writes(), [])
        self.assertEqual(y2.PreReadMismatch.exit_code, 3)
        self.assertTrue(any(f.startswith("preread-RECOVERY") for f in os.listdir(self.tmp.name)))

    def test_any_of_several_expected_contents_is_accepted(self):
        da = self.da()
        da.mem[REC.start - da.lo:REC.start - da.lo + len(self.img)] = self.img
        prev = dev(da, REC)
        res, out = self.go(da, [self.rec_job(expected=[("backup", self.stock_rec), ("last", prev)])])
        self.assertIn("matches last", out)
        self.assertEqual(len([e for e in main_events(da) if e[0] == "write"]), 1)

    def test_wrong_device_size_aborts_before_any_io(self):
        da = self.da(user_size=0x1d2000000 + 0x200000)
        with self.assertRaises(y2.Y2Error):
            self.go(da, [self.rec_job()])
        self.assertEqual(da.events, [])

    def test_readback_mismatch_detected(self):
        da = self.da(corrupt_at=10)
        with self.assertRaises(y2.WriteFailed) as cm:
            self.go(da, [self.rec_job()])
        msg = cm.exception.args[0]
        self.assertIn("READBACK MISMATCH at offset 0xa", msg)
        self.assertIn("restore-stock.sh", msg)
        self.assertEqual(y2.WriteFailed.exit_code, 2)

    def test_write_exception_is_reported_as_write_failure(self):
        with self.assertRaises(y2.WriteFailed):
            self.go(self.da(fail_write=True), [self.rec_job()])

    def test_duplicate_target_refused(self):
        with self.assertRaises(y2.Y2Error):
            self.go(self.da(), [self.rec_job(), self.rec_job()])

    def test_unchecked_current_only_when_expected_is_none(self):
        da = self.da()
        da.mem[REC.start - da.lo + 5] ^= 1
        self.go(da, [y2.Job(REC, self.img, None)])
        self.assertEqual(len([e for e in main_events(da) if e[0] == "write"]), 1)


class MultiTarget(Base):
    def test_order_and_preread_of_all_before_first_write(self):
        da = self.da()
        self.go(da, [self.boot_job(), self.rec_job()])
        rb, wb = 0xB80000, 0xB80000
        self.assertEqual([(e[0], e[1]) for e in main_events(da)],
                         [("read", BOOT.start + rb), ("read", REC.start + rb),
                          ("write", BOOT.start + wb), ("read", BOOT.start + rb),
                          ("write", REC.start + wb), ("read", REC.start + rb)])
        first_big = next(i for i, e in enumerate(da.events) if e[0] == "write" and e[2] > 4096)
        canary_writes = [i for i, e in enumerate(da.events) if e[0] == "write" and e[2] == 4096]
        self.assertEqual(len(canary_writes), 4)                  # marker + restore, per target
        self.assertTrue(all(i < first_big for i in canary_writes))   # every canary before the first full write

    def test_second_target_preread_mismatch_means_nothing_written(self):
        da = self.da()
        da.mem[REC.start - da.lo] ^= 1
        with self.assertRaises(y2.PreReadMismatch):
            self.go(da, [self.boot_job(), self.rec_job()])
        self.assertEqual(da.writes(), [])

    def test_abort_on_first_write_failure(self):
        da = self.da(corrupt_at=0)
        with self.assertRaises(y2.WriteFailed):
            self.go(da, [self.boot_job(), self.rec_job()])
        self.assertEqual([w[1] for w in main_events(da) if w[0] == "write"], [BOOT.start + 0xB80000])   # RECOVERY never attempted


class StateFile(Base):
    def test_attempt_recorded_before_write_and_post_image_is_acceptable_next_time(self):
        st = y2.State(os.path.join(self.tmp.name, "flash-state.json"))
        da = self.da()
        self.go(da, [self.rec_job()], state=st)
        hist = st.load()["history"]
        self.assertEqual([(h["target"], h["status"]) for h in hist], [("RECOVERY", "ok")])
        paths = st.expected_paths("RECOVERY")
        self.assertEqual(len(paths), 1)
        self.assertEqual(Path(paths[0]).read_bytes(), dev(da, REC))
        exp = y2.load_expected(REC, paths)
        self.go(da, [y2.Job(REC, rnd(0x2000, 9), exp)], state=st)
        self.assertEqual(len(st.load()["history"]), 2)

    def test_failed_write_leaves_attempted_entry_still_acceptable(self):
        st = y2.State(os.path.join(self.tmp.name, "flash-state.json"))
        with self.assertRaises(y2.WriteFailed):
            self.go(self.da(corrupt_at=3), [self.rec_job()], state=st)
        self.assertEqual(st.load()["history"][0]["status"], "failed")
        self.assertEqual(len(st.expected_paths("RECOVERY")), 1)
        self.assertEqual(st.expected_paths("BOOTIMG"), [])


def probe_da(**k):
    ws = y2.PROBE_P - y2.PROBE_HALF
    return FakeDa([(ws, bytes(2 * y2.PROBE_HALF))], **k)


class Probe(unittest.TestCase):
    NONCE = bytes(range(1, 17))
    WHEN = "2026-11-10T20:00:00+0000"

    def run_probe(self, da, keep_dir=None, persist=None):
        return quiet(y2.run_probe, da, keep_dir=keep_dir, nonce=self.NONCE, when=self.WHEN, persist=persist)

    def test_guards_hold_for_every_base(self):
        ws, we = y2.probe_guards()
        u = y2.TARGETS["USRDATA"]
        self.assertTrue(u.start <= ws and we <= u.end)
        self.assertEqual((ws, we), (0x49500000, 0x4F500000))
        for b in (0, y2.MAX_BASE):
            self.assertTrue(ws <= y2.PROBE_P - b and y2.PROBE_P - b + 4096 <= we)
        with self.assertRaises(y2.Y2Error):
            y2.check_probe_range(u.start + 0x1000, 4096)
        with self.assertRaises(y2.Y2Error):
            y2.check_probe_range(u.end - 512, 4096)
        with self.assertRaises(y2.Y2Error):
            y2.check_probe_range(REC.start + 0xB80000, 4096)

    def test_marker_is_nonzero_4096_with_nonce(self):
        m = y2.make_marker(self.NONCE, self.WHEN)
        self.assertEqual(len(m), 4096)
        self.assertEqual(m.count(0), 0)
        self.assertTrue(m.startswith(b"Y2PROBE" + self.NONCE))
        self.assertEqual(y2.make_marker(bytes(16), self.WHEN).count(0), 0)
        self.assertTrue(y2.make_marker(self.NONCE, self.WHEN, prefix=b"Y2CANARY").startswith(b"Y2CANARY"))

    def test_finds_write_base_0xb80000_and_restores_zeros(self):
        da = probe_da(write_base=0xB80000)
        cal, _ = self.run_probe(da)
        self.assertEqual((cal["write_base"], cal["read_base"], cal["trusted"]), (0xB80000, 0xB80000, True))
        self.assertEqual(cal["device_offset_found"], y2.PROBE_P - 0xB80000)
        self.assertEqual(cal["marker_nonce"], self.NONCE.hex())
        self.assertEqual((cal["emmc_user_size"], cal["hwcode"], cal["probe_write_addr"]),
                         (y2.EXPECT_EMMC_USER_SIZE, 0x6582, y2.PROBE_P))
        self.assertEqual(da.mem.count(0), len(da.mem))
        self.assertEqual([(e[1], e[2]) for e in da.writes()], [(y2.PROBE_P, 4096)] * 2)

    def test_finds_write_base_0_and_restores_zeros(self):
        da = probe_da(write_base=0)
        cal, _ = self.run_probe(da)
        self.assertEqual((cal["write_base"], cal["trusted"]), (0, True))
        self.assertEqual(cal["device_offset_found"], y2.PROBE_P)
        self.assertEqual(da.mem.count(0), len(da.mem))

    def test_unexpected_base_recorded_untrusted_with_warning(self):
        da = probe_da(write_base=0x1400000)
        cal, out = self.run_probe(da)
        self.assertEqual((cal["write_base"], cal["trusted"]), (0x1400000, False))
        self.assertIn("stop and investigate", cal["reason"])
        self.assertIn("WARNING", out)
        self.assertEqual(da.mem.count(0), len(da.mem))

    def test_nonce_persisted_before_first_write_and_base_persisted_even_if_restore_fails(self):
        docs = []
        da = probe_da(write_base=0xB80000, drop_nth=2)     # the restore write is dropped
        def persist(doc):
            docs.append((dict(doc), len(da.writes())))
        with self.assertRaises(y2.WriteFailed) as cm:
            self.run_probe(da, persist=persist)
        self.assertEqual(docs[0][1], 0)                                  # before any write
        self.assertEqual(docs[0][0]["marker_nonce"], self.NONCE.hex())
        self.assertFalse(docs[0][0]["trusted"])
        self.assertEqual(docs[-1][0]["write_base"], 0xB80000)            # measured base kept
        self.assertFalse(docs[-1][0]["trusted"])
        self.assertIn("restore not yet verified", docs[-1][0]["reason"])
        self.assertIn("--locate", cm.exception.args[0])

    def test_dropped_write_exit_4_window_saved_no_cleanup_needed(self):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        da = probe_da(drop_writes=True)
        docs = []
        with self.assertRaises(y2.ProbeFailed) as cm:
            self.run_probe(da, keep_dir=d.name, persist=docs.append)
        self.assertEqual(y2.ProbeFailed.exit_code, 4)
        self.assertEqual(len(da.writes()), 1)
        self.assertTrue(any(f.startswith("probe-window-") for f in os.listdir(d.name)))
        self.assertIn("--locate --nonce " + self.NONCE.hex(), cm.exception.args[0])
        self.assertIn("byte-addressed", cm.exception.args[0])
        self.assertIn("reason", docs[-1])

    def test_partial_marker_is_cleaned_up_when_it_matches_the_pattern(self):
        da = probe_da(write_base=0, truncate_to=1024)
        with self.assertRaises(y2.ProbeFailed) as cm:
            self.run_probe(da)
        self.assertIn("zeroed", cm.exception.args[0])
        self.assertEqual(da.mem.count(0), len(da.mem))
        self.assertEqual(len(da.writes()), 2)                     # marker + one cleanup write at the marker offset

    def test_garbage_in_window_is_not_cleaned(self):
        class Garbage(FakeDa):
            def write(self, raw, data):
                super().write(raw, bytes(b ^ 0x55 for b in data) if len(self.writes()) == 0 else data)
        da = Garbage([(y2.PROBE_P - y2.PROBE_HALF, bytes(2 * y2.PROBE_HALF))], write_base=0)
        with self.assertRaises(y2.ProbeFailed) as cm:
            self.run_probe(da)
        self.assertIn("NOT cleaning", cm.exception.args[0])
        self.assertEqual(len(da.writes()), 1)

    def test_marker_found_twice_is_exit_4_and_flags_remaining_copy(self):
        class Twice(FakeDa):
            def write(self, raw, data):
                super().write(raw, data)
                if len(self.writes()) == 1:
                    d = self.dev(raw, len(data), self.true_wb)
                    self.mem[d + 8192:d + 8192 + len(data)] = data
        da = Twice([(y2.PROBE_P - y2.PROBE_HALF, bytes(2 * y2.PROBE_HALF))], write_base=0)
        with self.assertRaises(y2.ProbeFailed) as cm:
            self.run_probe(da)
        self.assertIn("STILL not all zero", cm.exception.args[0])
        self.assertIn("--locate", cm.exception.args[0])

    def test_dirty_window_aborts_before_any_write(self):
        da = probe_da()
        da.mem[1234] = 1
        with self.assertRaises(y2.PreReadMismatch):
            self.run_probe(da)
        self.assertEqual(da.writes(), [])

    def test_wrong_device_aborts_before_any_io(self):
        da = probe_da(user_size=1)
        with self.assertRaises(y2.Y2Error):
            self.run_probe(da)
        self.assertEqual(da.events, [])

    def test_probe_writes_are_switched_to_user_area(self):
        da = probe_da(write_base=0)
        self.run_probe(da)
        self.assertEqual(da.log, [("switch",), ("write", y2.PROBE_P), ("switch",), ("write", y2.PROBE_P)])


class Locate(unittest.TestCase):
    NONCE = bytes(range(1, 17))

    def small_usrdata(self):
        small = y2.Target("USRDATA", y2.PROBE_P - 0x2000000, 0x4000000)
        p = mock.patch.dict(y2.TARGETS, {"USRDATA": small})
        p.start()
        self.addCleanup(p.stop)
        return small

    def test_locate_finds_and_zeroes_the_stray_marker(self):
        small = self.small_usrdata()
        da = FakeDa([(small.start, bytes(small.size))], write_base=0xB80000)
        d = y2.PROBE_P - 0xB80000
        da.mem[d - da.lo:d - da.lo + 4096] = y2.make_marker(self.NONCE, "2026-11-10T20:00:00+0000")
        found, out = quiet(y2.run_locate, da, self.NONCE)
        self.assertEqual(found, [d])
        self.assertEqual(da.mem.count(0), len(da.mem))
        self.assertEqual([(e[1], e[2]) for e in da.writes()], [(y2.PROBE_P, 4096)])

    def test_locate_leaves_a_marker_alone_if_it_is_not_consistent_with_the_probe_address(self):
        small = self.small_usrdata()
        da = FakeDa([(small.start, bytes(small.size))], write_base=0)
        d = y2.PROBE_P + 0x100000            # above P: no base in [0, MAX] explains it
        da.mem[d - da.lo:d - da.lo + 4096] = y2.make_marker(self.NONCE, "t")
        found, out = quiet(y2.run_locate, da, self.NONCE)
        self.assertEqual(found, [d])
        self.assertEqual(da.writes(), [])
        self.assertIn("NOT touching", out)

    def test_locate_nothing_found(self):
        small = self.small_usrdata()
        da = FakeDa([(small.start, bytes(small.size))])
        found, _ = quiet(y2.run_locate, da, self.NONCE)
        self.assertEqual(found, [])
        self.assertEqual(da.writes(), [])


class Canary(Base):
    def test_guards_for_every_target(self):
        self.assertEqual(y2.canary_addr(REC), 0x3D7F000)
        self.assertEqual(y2.canary_addr(BOOT), 0x2D7F000)
        self.assertEqual(y2.canary_addr(y2.TARGETS["USRDATA"]), 0x41380000)
        for t in y2.TARGETS.values():
            y2.canary_guards(t)
        # blast radius when the base is off by +-0xb80000 (documented in the code)
        self.assertEqual([hex(0x3D7F000 - 0xB80000), hex(0x3D7F000 + 0xB80000)], ["0x31ff000", "0x48ff000"])
        self.assertEqual([hex(0x2D7F000 - 0xB80000), hex(0x2D7F000 + 0xB80000)], ["0x21ff000", "0x38ff000"])
        self.assertTrue(y2.EXPDB[0] <= 0x48FF000 < y2.EXPDB[1])
        self.assertNotIn("EXPDB", y2.TARGETS)        # canary-landing only, never a write target

    def test_good_mapping_canary_is_written_found_and_undone(self):
        da = self.da(write_base=0xB80000)
        self.go(da, [self.rec_job()])
        c = y2.canary_addr(REC)
        marker_writes = [e for e in da.events if e[0] == "write" and e[2] == 4096]
        self.assertEqual([e[1] for e in marker_writes], [c + 0xB80000] * 2)
        self.assertEqual(da.device_bytes(c, 4096), self.stock_rec[c - REC.start:c - REC.start + 4096])

    def test_every_write_is_preceded_by_a_switch_to_user(self):
        da = self.da()
        self.go(da, [self.boot_job(), self.rec_job()])
        writes = [i for i, e in enumerate(da.log) if e[0] == "write"]
        self.assertEqual(len(writes), 6)
        for i in writes:
            self.assertEqual(da.log[i - 1], ("switch",))

    def test_wrong_base_canary_exit_6_restores_original_at_the_wrong_place_and_does_no_full_write(self):
        da = self.da(write_base=0)           # device is linear with base 0, calibration says 0xb80000
        before = bytes(da.mem)
        with self.assertRaises(y2.CanaryFailed) as cm:
            self.go(da, [self.rec_job()], keep_dir=self.tmp.name)
        self.assertEqual(y2.CanaryFailed.exit_code, 6)
        self.assertIn(f"found at {y2.canary_addr(REC) + 0xB80000:#x}", cm.exception.args[0])
        self.assertEqual(bytes(da.mem), before)                         # stray marker restored with ITS original bytes
        self.assertEqual([e for e in da.writes() if e[2] > 4096], [])   # never a full write
        self.assertEqual(len(da.writes()), 2)
        self.assertEqual(len([f for f in os.listdir(self.tmp.name) if f.startswith("canary-RECOVERY")]), 2)

    def test_non_linear_mapping_is_caught_per_target(self):
        """BOOTIMG maps with 0xb80000, RECOVERY region with 0: a base measured on one target is not trusted
        for the other."""
        fn = lambda raw: 0xB80000 if raw < REC.start + 0xB80000 else 0
        da = self.da(write_base_fn=fn)
        before = bytes(da.mem)
        with self.assertRaises(y2.CanaryFailed):
            self.go(da, [self.boot_job(), self.rec_job()])
        self.assertEqual(bytes(da.mem), before)
        self.assertEqual([e for e in da.writes() if e[2] > 4096], [])
        self.assertEqual(len(da.writes()), 4)       # BOOT canary + restore, RECOVERY canary + stray restore

    def test_dropped_canary_write_changes_nothing_and_fails(self):
        da = self.da(drop_nth=1)
        before = bytes(da.mem)
        with self.assertRaises(y2.CanaryFailed) as cm:
            self.go(da, [self.rec_job()])
        self.assertIn("did not change at all", cm.exception.args[0])
        self.assertEqual(bytes(da.mem), before)
        self.assertEqual(len(da.writes()), 1)

    def test_failed_canary_restore_is_reported(self):
        da = self.da(drop_nth=2)
        with self.assertRaises(y2.CanaryFailed) as cm:
            self.go(da, [self.rec_job()])
        self.assertIn("canary restore FAILED", cm.exception.args[0])
        self.assertEqual([e for e in da.writes() if e[2] > 4096], [])

    def test_window_changed_since_preread_aborts_before_writing(self):
        da = self.da()
        c = y2.canary_addr(REC)
        real_read = da.read
        calls = []
        def flaky(raw, n):
            data = real_read(raw, n)
            calls.append(n)
            if n == CANWIN:
                b = bytearray(data)
                b[CANWIN // 2 - 5000] ^= 1       # inside the canary sector's neighbourhood: change the canary bytes
                b[(c - (c - 0xB80000 - 4096))] ^= 1
                return bytes(b)
            return data
        da.read = flaky
        with self.assertRaises(y2.Y2Error):
            self.go(da, [self.rec_job()])
        self.assertEqual(da.writes(), [])


class DaAdapter(unittest.TestCase):
    """The real adapter with a stub mtkclient: switch before every write, minimum raw address, no bypass."""

    def adapter(self, switch_ok=True):
        calls = []
        da_stub = SimpleNamespace(sdmmc_switch_part=lambda part: calls.append(("switch", part)) or switch_ok)
        mtk = SimpleNamespace(daloader=SimpleNamespace(
            da=da_stub, writeflash=lambda **k: calls.append(("write", k["addr"], k["parttype"], len(k["wdata"]))) or True))
        a = y2.MtkDa.__new__(y2.MtkDa)
        a.mtk, a.workdir = mtk, "/nonexistent"
        a._switched = False
        a.write_base = 0xB80000
        return a, calls

    def test_switch_then_write_with_user_parttype(self):
        a, calls = self.adapter()
        a.switch_user()
        a.write(REC.start + 0xB80000, bytes(512))
        self.assertEqual(calls, [("switch", 8), ("write", REC.start + 0xB80000, "user", 512)])

    def test_write_without_switch_refused(self):
        a, calls = self.adapter()
        with self.assertRaises(y2.Y2Error):
            a.write(REC.start + 0xB80000, bytes(512))
        a.switch_user()
        a.write(REC.start + 0xB80000, bytes(512))
        with self.assertRaises(y2.Y2Error):        # the switch is consumed by one write
            a.write(REC.start + 0xB80000, bytes(512))
        self.assertEqual(len([c for c in calls if c[0] == "write"]), 1)

    def test_failed_switch_refused(self):
        a, calls = self.adapter(switch_ok=False)
        with self.assertRaises(y2.Y2Error):
            a.switch_user()
        with self.assertRaises(y2.Y2Error):
            a.write(REC.start + 0xB80000, bytes(512))
        self.assertEqual(calls, [("switch", 8)])

    def test_low_raw_addresses_and_non_targets_refused(self):
        a, calls = self.adapter()
        for raw in (0x0, 0x200, 0x3FFE00, 0x80000 + 0xB80000, REC.end + 0xB80000):
            a.switch_user()
            with self.assertRaises(y2.Y2Error, msg=hex(raw)):
                a.write(raw, bytes(512))
        self.assertEqual([c for c in calls if c[0] == "write"], [])

    def test_probe_mode_confined_to_usrdata(self):
        a, calls = self.adapter()
        a.write_base = None
        a.switch_user()
        with self.assertRaises(y2.Y2Error):
            a.write(REC.start + 0xB80000, bytes(512))
        a.switch_user()
        a.write(y2.PROBE_P, bytes(4096))
        self.assertEqual(calls[-1], ("write", y2.PROBE_P, "user", 4096))


def good_cal(**over):
    d = dict(write_base=0, read_base=0xB80000, trusted=True, emmc_user_size=y2.EXPECT_EMMC_USER_SIZE,
             hwcode=0x6582, probe_write_addr=y2.PROBE_P, mtkclient_commit="abc")
    d.update(over)
    return d


class Calibration(Base):
    def cal(self, **doc):
        p = os.path.join(self.tmp.name, "cal.json")
        with open(p, "w") as f:
            json.dump(doc, f)
        return p

    def test_missing_calibration_refuses(self):
        with self.assertRaises(y2.Y2Error) as cm:
            y2.load_calibration(os.path.join(self.tmp.name, "nope.json"))
        self.assertIn("probe-write.sh", str(cm.exception))

    def test_trusted_calibration_loads(self):
        for wb in (0, 0xB80000):
            self.assertEqual(y2.load_calibration(self.cal(**good_cal(write_base=wb)), "abc"),
                             y2.Bases(read=0xB80000, write=wb))

    def test_only_known_bases_are_ever_trusted_even_if_marked_trusted(self):
        for wb in (0x700000, 0x1400000, 0x200, 0xB80200):
            with self.assertRaises(y2.Y2Error, msg=hex(wb)) as cm:
                y2.load_calibration(self.cal(**good_cal(write_base=wb)))
            self.assertIn("no override", str(cm.exception))

    def test_untrusted_or_odd_or_mismatched_calibration_refuses(self):
        for doc in (good_cal(trusted=False), good_cal(trusted="yes"), good_cal(read_base=0), good_cal(write_base=-1),
                    good_cal(write_base="0"), good_cal(write_base=True), good_cal(emmc_user_size=1),
                    good_cal(hwcode=0x6580), good_cal(probe_write_addr=1)):
            with self.assertRaises(y2.Y2Error, msg=str(doc)):
                y2.load_calibration(self.cal(**doc))
        bad = good_cal()
        del bad["hwcode"]
        with self.assertRaises(y2.Y2Error):
            y2.load_calibration(self.cal(**bad))

    def test_mtkclient_commit_must_match_when_known(self):
        with self.assertRaises(y2.Y2Error):
            y2.load_calibration(self.cal(**good_cal()), "different")
        y2.load_calibration(self.cal(**good_cal()), "")          # unknown current commit: not checked

    def test_cli_write_refuses_without_calibration_even_dry_run(self):
        img, exp = Path(self.tmp.name) / "i.img", Path(self.tmp.name) / "e.bin"
        img.write_bytes(self.img)
        exp.write_bytes(self.stock_rec)
        rc, out = quiet(y2.main, ["write", "--target", "RECOVERY", "--image", str(img), "--expect-current", str(exp),
                                  "--calibration", os.path.join(self.tmp.name, "none.json"), "--dry-run"])
        self.assertEqual(rc, 1)
        self.assertIn("UNCALIBRATED", out)

    def test_regression_old_bug_readback_cannot_see_misaddressed_write_but_canary_does(self):
        """Device writes at scatter+0 but the old code assumed scatter+0xb80000, so the write lands 0xb80000
        too high. The readback uses the READ command, i.e. the right place, so for a short no-op image it
        reports success while the blocks at RECOVERY+0xb80000 were silently overwritten. The canary (run_jobs)
        now stops this after 4 KiB of harm, which it undoes."""
        wrong = y2.Bases(read=0xB80000, write=0xB80000)         # the old assumption
        head = self.stock_rec[:0x3000]
        da = self.da(write_base=0)
        with contextlib.redirect_stdout(io.StringIO()):
            res = y2.write_and_verify(da, self.rec_job(head), wrong)   # the old flow, no canary
        self.assertEqual(res[0], res[1])                        # "passed"...
        self.assertEqual(dev(da, REC, 0x3000), head)            # ...the start of RECOVERY is untouched, proving nothing
        self.assertEqual(da.device_bytes(REC.start + 0xB80000, 0x3000), head)   # the data went here instead
        da = self.da(write_base=0)
        before = bytes(da.mem)
        with self.assertRaises(y2.CanaryFailed):                # the new flow
            quiet(y2.run_jobs, da, [self.rec_job(head)], wrong)
        self.assertEqual(bytes(da.mem), before)
        self.assertEqual([e for e in da.writes() if e[2] > 4096], [])

    def test_calibration_from_probe_prevents_it(self):
        pd = probe_da(write_base=0)
        cal, _ = quiet(y2.run_probe, pd, nonce=bytes(range(1, 17)), when="t", mtk_commit="abc")
        bases = y2.load_calibration(self.cal(**cal), "abc")
        self.assertEqual(bases.write, 0)
        da = self.da(write_base=0)
        quiet(y2.run_jobs, da, [self.rec_job()], bases)
        self.assertEqual(dev(da, REC, len(self.img)), self.img)
        self.assertEqual(da.device_bytes(BYSTANDER.start, BYSTANDER.size), self.by)


class StateRobustness(Base):
    class BrokenFinish(y2.State):
        def finish(self, idx, status):
            raise OSError("disk full")

    def test_finish_failure_after_success_is_a_warning(self):
        st = self.BrokenFinish(os.path.join(self.tmp.name, "s.json"))
        res, out = quiet(y2.run_jobs, self.da(), [self.rec_job()], self.bases, state=st)
        self.assertEqual(len(res), 1)
        self.assertIn("WARNING: could not record status 'ok'", out)

    def test_finish_failure_does_not_mask_the_real_error(self):
        st = self.BrokenFinish(os.path.join(self.tmp.name, "s.json"))
        with self.assertRaises(y2.WriteFailed) as cm:
            quiet(y2.run_jobs, self.da(corrupt_at=2), [self.rec_job()], self.bases, state=st)
        self.assertIn("READBACK MISMATCH", cm.exception.args[0])


class CliSession(Base):
    """main() with the real mtkclient session replaced by a fake DA."""

    def setUp(self):
        super().setUp()
        d = Path(self.tmp.name)
        self.img_p, self.exp_p, self.cal_p, self.log_p = d / "i.img", d / "e.bin", d / "cal.json", d / "run.log"
        self.img_p.write_bytes(self.img)
        self.exp_p.write_bytes(self.stock_rec)
        self.cal_p.write_text(json.dumps(good_cal(write_base=0xB80000)))
        self.argv = ["write", "--target", "RECOVERY", "--image", str(self.img_p), "--expect-current", str(self.exp_p),
                     "--calibration", str(self.cal_p), "--preloader", "x", "--log", str(self.log_p)]

    def run_main(self, da):
        @contextlib.contextmanager
        def fake_session(preloader, wd):
            yield da
        with mock.patch.object(y2, "open_session", fake_session), mock.patch.object(y2, "current_commit", lambda: "abc"):
            return quiet(y2.main, self.argv)

    def test_success_logs_write_started_and_done(self):
        rc, out = self.run_main(self.da())
        self.assertEqual(rc, 0)
        self.assertEqual(out.count("WRITE STARTED"), 3)       # canary marker, canary restore, the image
        self.assertEqual(out.count("WRITE DONE"), 3)
        self.assertIn("WRITE STARTED", self.log_p.read_text())

    def test_canary_failure_is_exit_6(self):
        rc, out = self.run_main(self.da(write_base=0))
        self.assertEqual(rc, 6)
        self.assertIn("CANARY FAILED", out)

    def test_sigterm_mid_write_becomes_interrupt_with_partial_warning(self):
        before = signal.getsignal(signal.SIGTERM)
        rc, out = self.run_main(self.da(interrupt_on_write=3))      # during the full write
        self.assertEqual(rc, 130)
        self.assertIn("DEVICE MAY BE PARTIALLY WRITTEN", out)
        self.assertIn("--force-unknown-current", out)
        self.assertEqual(out.count("WRITE STARTED"), out.count("WRITE DONE") + 1)
        self.assertEqual(signal.getsignal(signal.SIGTERM), before)  # handler restored

    def test_restore_hint_mentions_force(self):
        self.assertIn("--force-unknown-current", y2.RESTORE_HINT)


class FailClosedAndOrdering(Base):
    def cal(self, **over):
        p = os.path.join(self.tmp.name, "cal.json")
        with open(p, "w") as f:
            json.dump(good_cal(**over), f)
        return p

    def test_strict_requires_known_current_commit(self):
        with self.assertRaises(y2.Y2Error):
            y2.load_calibration(self.cal(), "", strict=True)
        with self.assertRaises(y2.Y2Error):
            y2.load_calibration(self.cal(), None, strict=True)

    def test_strict_requires_commit_in_calibration(self):
        for c in ("", None):
            with self.assertRaises(y2.Y2Error):
                y2.load_calibration(self.cal(mtkclient_commit=c), "abc", strict=True)
        d = json.load(open(self.cal()))
        del d["mtkclient_commit"]
        p = os.path.join(self.tmp.name, "nocommit.json")
        json.dump(d, open(p, "w"))
        with self.assertRaises(y2.Y2Error):
            y2.load_calibration(p, "abc", strict=True)
        self.assertEqual(y2.load_calibration(self.cal(), "abc", strict=True).write, 0)

    def run_cli(self, argv, commit):
        with mock.patch.object(y2, "current_commit", lambda: commit):
            return quiet(y2.main, argv)

    def test_real_write_run_refuses_without_current_commit_but_dry_run_does_not(self):
        d = Path(self.tmp.name)
        (d / "i.img").write_bytes(self.img)
        (d / "e.bin").write_bytes(self.stock_rec)
        argv = ["write", "--target", "RECOVERY", "--image", str(d / "i.img"), "--expect-current", str(d / "e.bin"),
                "--calibration", self.cal(), "--preloader", "x"]
        rc, out = self.run_cli(argv + ["--dry-run"], "")
        self.assertEqual(rc, 0)
        with mock.patch.object(y2, "open_session", side_effect=AssertionError("must not connect")):
            rc, out = self.run_cli(argv, "")
        self.assertEqual(rc, 1)
        self.assertIn("mtkclient commit", out)

    def test_probe_refuses_without_current_commit(self):
        with mock.patch.object(y2, "open_session", side_effect=AssertionError("must not connect")):
            rc, out = self.run_cli(["probe", "--calibration", os.path.join(self.tmp.name, "none.json"),
                                    "--preloader", "x"], "")
        self.assertEqual(rc, 1)
        self.assertIn("Refusing to probe", out)

    def test_failed_switch_prints_no_write_started_and_counts_nothing(self):
        class NoSwitch(FakeDa):
            def switch_user(self):
                raise RuntimeError("switch failed")
        da = NoSwitch([(REC.start, self.stock_rec)])
        y2._WRITES.update(started=0, done=0)
        with self.assertRaises(y2.WriteFailed) as cm:
            quiet(y2.guarded_write, da, REC, REC.start, bytes(512), 0xB80000)
        self.assertNotIn("WRITE STARTED", cm.exception.output)
        self.assertEqual({k: v for k, v in y2._WRITES.items() if k != "target"}, {"started": 0, "done": 0, "canary": None})
        self.assertEqual(da.events, [])

    def test_interrupt_during_canary_names_the_canary_address(self):
        d = Path(self.tmp.name)
        (d / "i.img").write_bytes(self.img)
        (d / "e.bin").write_bytes(self.stock_rec)
        argv = ["write", "--target", "RECOVERY", "--image", str(d / "i.img"), "--expect-current", str(d / "e.bin"),
                "--calibration", self.cal(write_base=0xB80000), "--preloader", "x"]

        @contextlib.contextmanager
        def fake_session(preloader, wd):
            yield self.da(interrupt_on_write=1)         # SIGTERM during the canary marker write
        with mock.patch.object(y2, "open_session", fake_session):
            rc, out = quiet(y2.main, argv)
        self.assertEqual(rc, 130)
        self.assertIn("INTERRUPTED DURING THE CANARY", out)
        self.assertIn("0x3d7f000", out)
        self.assertIn("restore-stock.sh --target RECOVERY --force-unknown-current", out)

    def test_limits_are_documented(self):
        self.assertIn("exactly +-0xb80000", y2.__doc__)
        self.assertIn("SEC_RO", y2.__doc__)


class Cli(Base):
    def test_dry_run_connects_to_nothing(self):
        img, exp = Path(self.tmp.name) / "i.img", Path(self.tmp.name) / "e.bin"
        img.write_bytes(self.img)
        exp.write_bytes(self.stock_rec)
        cal = Path(self.tmp.name) / "cal.json"
        cal.write_text(json.dumps(good_cal()))
        rc, out = quiet(y2.main, ["write", "--target", "RECOVERY", "--image", str(img),
                                  "--expect-current", str(exp), "--calibration", str(cal), "--dry-run"])
        self.assertEqual(rc, 0)
        self.assertIn("0x3900000", out)
        self.assertIn("raw write addr 0x2d80000", out)
        self.assertIn("canary RECOVERY: device 0x3d7f000", out)
        self.assertIn(y2.sha(self.img), out)
        self.assertIn(y2.sha(self.stock_rec), out)

    def test_cli_refuses_nvram_target(self):
        rc, out = quiet(y2.main, ["write", "--target", "NVRAM", "--image", "/dev/null",
                                  "--expect-current", "/dev/null", "--dry-run"])
        self.assertEqual(rc, 1)
        self.assertIn("refusing target", out)


USR = y2.TARGETS["USRDATA"]


class DiscardCurrent(Base):
    """--discard-current (USRDATA only), evidence, size guard, reference state entries, USRDATA restore."""

    def setUp(self):
        super().setUp()
        self.usr = rnd(0x2000000, 11)          # first 32 MiB of "stock user data": covers the canary window
        self.fimg = rnd(0x40000, 12)           # rootfs stand-in

    def uda(self, **k):
        return FakeDa([(USR.start, self.usr), (BOOT.start, self.stock_boot), (REC.start, self.stock_rec)], **k)

    def ujob(self, image=None):
        return y2.Job(USR, self.fimg if image is None else image, None, discard=True)

    def test_refused_for_bootimg_and_recovery_with_no_writes(self):
        for t, stock in ((BOOT, self.stock_boot), (REC, self.stock_rec)):
            with self.assertRaises(y2.Y2Error):
                y2.Job(t, self.img, None, discard=True)
            da = self.uda()
            img, cal = Path(self.tmp.name) / "i.img", Path(self.tmp.name) / "cal.json"
            img.write_bytes(self.img)
            cal.write_text(json.dumps(good_cal()))
            for extra in ([], ["--expect-current", str(img)]):
                @contextlib.contextmanager
                def fake_session(preloader, wd):
                    yield da
                with mock.patch.object(y2, "open_session", fake_session):
                    rc, out = quiet(y2.main, ["write", "--target", t.name, "--image", str(img), "--discard-current",
                                              "--calibration", str(cal), "--preloader", "x"] + extra)
                self.assertEqual(rc, 1)
                self.assertIn("USRDATA only", out)
            self.assertEqual(da.events, [])
            self.assertEqual(da.log, [])

    def test_restore_discard_refused_for_other_targets(self):
        img = Path(self.tmp.name) / "i.img"
        img.write_bytes(self.img)
        for t in ("BOOTIMG", "RECOVERY"):
            rc, out = quiet(y2.main, ["restore", "--job", f"{t}={img}", "--expect", f"{t}={img}",
                                      "--discard-current", t, "--dry-run"])
            self.assertEqual(rc, 1)
            self.assertIn("USRDATA only", out)

    def test_discard_flow_evidence_canary_write_readback_no_full_preread(self):
        da = self.uda()
        res, out = self.go(da, [self.ujob()], keep_dir=self.tmp.name)
        self.assertEqual(res[0][1], res[0][2])
        self.assertEqual(da.device_bytes(USR.start, len(self.fimg)), self.fimg)
        self.assertEqual(da.device_bytes(USR.start + len(self.fimg), 0x1000), self.usr[len(self.fimg):][:0x1000])
        reads = [e[2] for e in da.events if e[0] == "read"]
        self.assertNotIn(USR.size, reads)
        self.assertLessEqual(max(reads), CANWIN)
        self.assertEqual(reads[:2], [y2.EVIDENCE_LEN, CANWIN])          # evidence first
        self.assertEqual(reads[-1], len(self.fimg))                     # full written length read back
        c = y2.canary_addr(USR)
        self.assertEqual([e[1] for e in da.events if e[0] == "write" and e[2] == 4096], [c + 0xB80000] * 2)
        self.assertEqual(da.device_bytes(c, 4096), self.usr[c - USR.start:c - USR.start + 4096])   # canary undone
        first_write = next(i for i, e in enumerate(da.events) if e[0] == "write")
        self.assertTrue(all(e[0] == "read" for e in da.events[:first_write]))
        self.assertIn("NOT compared", out)

    def test_evidence_files_saved_private_and_correct(self):
        da = self.uda()
        self.go(da, [self.ujob()], keep_dir=self.tmp.name)
        files = {f.split("-")[2]: f for f in os.listdir(self.tmp.name) if f.startswith("evidence-USRDATA")}
        first = Path(self.tmp.name, files["first1MiB"])
        win = Path(self.tmp.name, files["canarywindow"])
        self.assertEqual(first.read_bytes(), self.usr[:y2.EVIDENCE_LEN])
        c = y2.canary_addr(USR)
        self.assertEqual(win.read_bytes(), self.usr[c - 0xB80000 - 4096 - USR.start:c + 0xB80000 + 4096 - USR.start])
        self.assertEqual(os.stat(first).st_mode & 0o777, 0o600)

    def test_evidence_needs_a_log_dir_and_nothing_is_written_without_it(self):
        da = self.uda()
        with self.assertRaises(y2.Y2Error):
            self.go(da, [self.ujob()])
        self.assertEqual(da.writes(), [])

    def test_bad_canary_still_stops_a_discard_flash(self):
        da = self.uda(write_base=0)
        with self.assertRaises(y2.CanaryFailed):
            self.go(da, [self.ujob()], keep_dir=self.tmp.name)
        self.assertEqual([e for e in da.events if e[0] == "write" and e[2] == len(self.fimg)], [])

    def test_readback_mismatch_detected_for_usrdata(self):
        with self.assertRaises(y2.WriteFailed):
            self.go(self.uda(corrupt_at=100), [self.ujob()], keep_dir=self.tmp.name)

    def test_oversize_image_refused(self):
        self.assertEqual(y2.USRDATA_USABLE, 0x31F00000)
        with self.assertRaises(y2.Y2Error) as cm:
            y2.Job(USR, b"\1" * (y2.USRDATA_USABLE + 512), None, discard=True)
        self.assertIn("0x31f00000", cm.exception.args[0])
        y2.Job(USR, b"\1" * y2.USRDATA_USABLE, None, discard=True)           # exactly the LK size is fine
        with self.assertRaises(y2.Y2Error):
            y2.Job(USR, b"\1" * (USR.size), None, discard=True)
        y2.Job(USR, b"\1" * USR.size, None, discard=True, restore=True)      # the full-size backup (restore only)

    def test_oversize_refused_by_cli_with_no_io(self):
        big = Path(self.tmp.name) / "big.ext4"
        with open(big, "wb") as f:
            f.truncate(y2.USRDATA_USABLE + 512)
        cal = Path(self.tmp.name) / "cal.json"
        cal.write_text(json.dumps(good_cal()))
        rc, out = quiet(y2.main, ["write", "--target", "USRDATA", "--image", str(big), "--discard-current",
                                  "--calibration", str(cal), "--dry-run"])
        self.assertEqual(rc, 1)
        self.assertIn("larger than", out)

    def test_discard_and_expected_are_mutually_exclusive(self):
        with self.assertRaises(y2.Y2Error):
            y2.Job(USR, self.fimg, [("x", b"")], discard=True)

    def test_state_holds_a_reference_not_a_copy(self):
        st = y2.State(os.path.join(self.tmp.name, "flash-state.json"))
        img = Path(self.tmp.name) / "rootfs.ext4"
        img.write_bytes(self.fimg)
        job = y2.Job(USR, self.fimg, None, label=str(img), discard=True)
        self.go(self.uda(), [job], keep_dir=self.tmp.name, state=st)
        h = st.load()["history"]
        self.assertEqual(len(h), 1)
        e = h[0]
        self.assertEqual((e["target"], e["status"], e["post_kind"], e["discard_current"]), ("USRDATA", "ok", "reference", True))
        self.assertEqual((e["image_sha256"], e["image_len"], e["image_path"]), (y2.sha(self.fimg), len(self.fimg), str(img)))
        self.assertIsNone(e["post_image"])
        self.assertIn("first", e["evidence"])
        self.assertFalse(os.path.exists(os.path.join(self.tmp.name, "flashed")))   # no 800 MiB (or any) copy
        self.assertEqual(st.expected_paths("USRDATA"), [])
        self.assertEqual(st.expected_paths("USRDATA", True), [])
        self.assertEqual(st.last_entry("USRDATA")["image_sha256"], y2.sha(self.fimg))

    def test_expected_paths_survive_reference_entries_among_normal_ones(self):
        st = y2.State(os.path.join(self.tmp.name, "flash-state.json"))
        self.go(self.uda(), [self.rec_job()], state=st)
        self.go(self.uda(), [self.ujob()], keep_dir=self.tmp.name, state=st)
        self.assertEqual(len(st.expected_paths("RECOVERY")), 1)
        self.assertEqual(st.expected_paths("USRDATA", True), [])

    def test_state_last_cli(self):
        st = Path(self.tmp.name) / "s.json"
        rc, out = quiet(y2.main, ["state-last", "--state", str(st), "--target", "BOOTIMG"])
        self.assertEqual((rc, out.strip()), (0, "none"))
        self.go(self.uda(), [self.boot_job()], state=y2.State(str(st)))
        rc, out = quiet(y2.main, ["state-last", "--state", str(st), "--target", "BOOTIMG"])
        self.assertTrue(out.startswith("ok ") and len(out.split()[1]) == 64)

    def test_bootimg_flash_through_the_cli_uses_the_bootimg_canary(self):
        d = Path(self.tmp.name)
        img, exp, cal, log = d / "b.img", d / "stock.bin", d / "cal.json", d / "run.log"
        img.write_bytes(rnd(0x5000, 13))
        exp.write_bytes(self.stock_boot)
        cal.write_text(json.dumps(good_cal(write_base=0xB80000)))
        da = self.uda()

        @contextlib.contextmanager
        def fake_session(preloader, wd):
            yield da
        with mock.patch.object(y2, "open_session", fake_session):
            rc, out = quiet(y2.main, ["write", "--target", "BOOTIMG", "--image", str(img), "--expect-current", str(exp),
                                      "--calibration", str(cal), "--preloader", "x", "--log", str(log)])
        self.assertEqual(rc, 0)
        c = y2.canary_addr(BOOT)
        self.assertEqual(c, 0x2D7F000)
        self.assertEqual([e[1] for e in da.events if e[0] == "write" and e[2] == 4096], [c + 0xB80000] * 2)
        self.assertEqual(da.device_bytes(BOOT.start, 0x5000), img.read_bytes())
        self.assertEqual(dev(da, REC), self.stock_rec)

    def test_restore_usrdata_writes_backup_with_discard_and_all_targets_in_order(self):
        # backup stand-in sized to the fake: restore-mode jobs may be any size up to the full target
        bak = rnd(0x30000, 14)
        da = self.uda()
        res, out = self.go(da, [y2.Job(BOOT, self.stock_boot, [("b", self.stock_boot)]),
                                y2.Job(USR, bak, None, discard=True, restore=True)], keep_dir=self.tmp.name)
        self.assertEqual([r[0] for r in res], ["BOOTIMG", "USRDATA"])
        self.assertEqual(da.device_bytes(USR.start, len(bak)), bak)
        self.assertEqual(len([e for e in da.events if e[0] == "read" and e[2] == BOOT.size]), 2)   # BOOTIMG pre-read + readback, in full
        self.assertNotIn(USR.size, [e[2] for e in da.events if e[0] == "read"])

    def test_restore_cli_force_on_usrdata_never_full_preread(self):
        d = Path(self.tmp.name)
        bak, cal, log = d / "USRDATA.bin", d / "cal.json", d / "r.log"
        bak.write_bytes(rnd(0x20000, 15))
        cal.write_text(json.dumps(good_cal(write_base=0xB80000)))
        da = self.uda()

        @contextlib.contextmanager
        def fake_session(preloader, wd):
            yield da
        with mock.patch.object(y2, "open_session", fake_session):
            rc, out = quiet(y2.main, ["restore", "--job", f"USRDATA={bak}", "--discard-current", "USRDATA",
                                      "--calibration", str(cal), "--preloader", "x", "--log", str(log)])
        self.assertEqual(rc, 0, out)
        self.assertEqual(da.device_bytes(USR.start, 0x20000), bak.read_bytes())
        self.assertNotIn(USR.size, [e[2] for e in da.events if e[0] == "read"])
        self.assertTrue(any(f.startswith("evidence-USRDATA") for f in os.listdir(d)))

    def test_dry_run_plan_shows_discard_and_eta(self):
        d = Path(self.tmp.name)
        img, cal = d / "r.ext4", d / "cal.json"
        with open(img, "wb") as f:
            f.truncate(512 << 20)
        cal.write_text(json.dumps(good_cal()))
        rc, out = quiet(y2.main, ["write", "--target", "USRDATA", "--image", str(img), "--discard-current",
                                  "--calibration", str(cal), "--dry-run"])
        self.assertEqual(rc, 0)
        self.assertIn("DISCARDED", out)
        self.assertIn("write ~112 s", out)
        self.assertIn("canary USRDATA: device 0x41380000", out)


    def three_jobs(self):
        return [y2.Job(BOOT, self.stock_boot, [("b", self.stock_boot)]),
                y2.Job(REC, self.stock_rec, [("r", self.stock_rec)]), self.ujob()]

    def test_all_data_evidence_read_failure_means_zero_writes(self):
        da = self.uda()
        real = da.read

        def read(raw, n):
            if n == y2.EVIDENCE_LEN:
                raise y2.Y2Error("usb read failed")
            return real(raw, n)
        da.read = read
        with self.assertRaises(y2.Y2Error):
            self.go(da, self.three_jobs(), keep_dir=self.tmp.name)
        self.assertEqual(da.writes(), [])
        self.assertEqual(da.log, [])
        self.assertIn(("read", BOOT.start + 0xB80000, BOOT.size), da.events)      # pre-reads had happened

    def test_all_data_usrdata_canary_failure_means_no_full_write_and_canaries_undone(self):
        da = self.uda(write_base_fn=lambda raw: 0 if raw >= USR.start + 0xB80000 else 0xB80000)
        with self.assertRaises(y2.CanaryFailed):
            self.go(da, self.three_jobs(), keep_dir=self.tmp.name)
        self.assertEqual([w for w in da.writes() if w[2] > 4096], [])
        self.assertEqual(dev(da, BOOT), self.stock_boot)
        self.assertEqual(dev(da, REC), self.stock_rec)
        self.assertEqual(da.device_bytes(USR.start, 0x2000000), self.usr)

    def test_recovery_advice_depends_on_target(self):
        u = y2.restore_hint("USRDATA")
        self.assertIn("flash-rootfs.sh", u)
        self.assertIn("--target USRDATA", u)
        self.assertIn("Do NOT run restore-stock.sh with no target", u)
        self.assertNotIn("refuse at the pre-read", u)
        self.assertIn("restore-stock.sh --target BOOTIMG", y2.restore_hint("BOOTIMG"))
        self.assertIn("--force-unknown-current", y2.restore_hint("RECOVERY"))
        self.assertNotIn("Do NOT run restore-stock.sh", y2.restore_hint("RECOVERY"))
        with self.assertRaises(y2.WriteFailed) as cm:
            self.go(self.uda(corrupt_at=100), [self.ujob()], keep_dir=self.tmp.name)
        self.assertIn("flash-rootfs.sh", cm.exception.args[0])
        with self.assertRaises(y2.WriteFailed) as cm:
            self.go(self.uda(corrupt_at=100), [self.boot_job()])
        self.assertIn("--target BOOTIMG", cm.exception.args[0])

    def test_interrupt_texts_depend_on_target(self):
        d = Path(self.tmp.name)
        img, cal, log = d / "r.img", d / "cal.json", d / "r.log"
        img.write_bytes(self.fimg)
        cal.write_text(json.dumps(good_cal(write_base=0xB80000)))

        def run(da, extra):
            @contextlib.contextmanager
            def fake_session(preloader, wd):
                yield da
            with mock.patch.object(y2, "open_session", fake_session):
                return quiet(y2.main, ["write", "--target", "USRDATA", "--image", str(img), "--discard-current",
                                       "--calibration", str(cal), "--preloader", "x", "--log", str(log)])
        rc, out = run(self.uda(interrupt_on_write=3), None)         # during the image write
        self.assertEqual(rc, 130)
        self.assertIn("flash-rootfs.sh", out)
        self.assertIn("Do NOT run restore-stock.sh with no target", out)
        rc, out = run(self.uda(interrupt_on_write=1), None)         # during the canary
        self.assertEqual(rc, 130)
        self.assertNotIn("refuse at the pre-read", out)
        self.assertIn("flash-rootfs.sh", out)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Guarded flash driver for the Innioasis Y2 (MT6582, eMMC, mtkclient legacy DA), ONE DA session.

  y2flash.py probe   [--preloader P] [--calibration FILE] [--log FILE]      (measure the WRITE address base)
  y2flash.py write   --target RECOVERY --image IMG --expect-current FILE [--expect-current FILE ...]
                     [--calibration FILE] [--preloader P] [--state FILE] [--dry-run] [--log FILE]
  y2flash.py restore --job BOOTIMG=IMG [--job RECOVERY=IMG] --expect BOOTIMG=FILE [--expect ...]
                     [--force-unknown-current] [--calibration FILE] [--preloader P] [--state FILE] [--dry-run]
  y2flash.py read    --target X --out FILE [--preloader P] [--log FILE]
  y2flash.py state-expect --state FILE --target X [--all]     (prints post-image paths, no device)

Addressing. Targets are stored DEVICE-RELATIVE (the scatter-file address). mtkclient's legacy DA uses
different commands for read (0xD6, no partition field) and write (SDMMC_WRITE_DATA_CMD, explicit parttype),
and they need not share an address offset:
  * every READ  uses  scatter + READ_BASE (0xb80000, measured on a full read);
  * every WRITE uses  scatter + WRITE_BASE, which is UNKNOWN until `probe` has measured it on a block of
    USRDATA that is all zero in the backup. write/restore refuse without ~/y2-backup/write-calibration.json.
A readback goes through the READ command, so it cannot detect a write that landed at the wrong place;
that is why the probe exists.

Hard rules enforced here (and again in the one function that calls mtkclient's write):
  * only BOOTIMG, RECOVERY, USRDATA may be written, and only inside their exact device range;
  * mtkclient's partition-name commands (`w <name>`, needs a GPT; there is none) are never used;
  * the target is read first and must equal an acceptable known content, otherwise nothing is written;
  * after writing, the same range is read back and must equal what was written.

mtkclient is imported lazily (only when a session is opened) so the tests run without it.
Exit codes: 0 ok; 1 refused/error before any write; 3 pre-read mismatch (nothing written);
2 write or readback failed (DEVICE MAY HAVE BEEN MODIFIED: restore from the backup);
4 probe could not find its marker; 5 probe measured an unexpected base (needs human review);
6 canary write did not land where the calibration says (nothing else written; stray canary restored).

Limits of the canary. It protects against a write base that is wrong by exactly +-0xb80000 (the only other
base the probe trusts). Any other mapping error is outside this model. A non-linearity in the middle of a
partition is only detected by the post-write readback, after the damage. The canary window files saved in the
log directory (canary-*-pre/post-*.bin, probe-window-*.bin) contain SEC_RO / EXPDB / user bytes: keep them private.

Every write is announced with a "WRITE STARTED ..." line and closed with "WRITE DONE ..."; a log that has more
STARTED than DONE lines means a write was interrupted and the device may be partially written.
"""
import argparse
import contextlib
import hashlib
import json
import os
import sys
import signal
import tempfile
import time
from typing import NamedTuple

SECTOR = 512
EXPECT_EMMC_USER_SIZE = 0x1D2000000   # m_emmc_ua_size reported by this device's DA, 2026-11-10
EXPECT_HWCODE = 0x6582
READ_BASE = 0xB80000                  # read address = scatter address + READ_BASE (measured on a full read)
KNOWN_WRITE_BASES = (0, READ_BASE)    # the only values the probe result is auto-trusted for
MAX_BASE = 0x1400000                  # probe guards hold for any base in [0, MAX_BASE]
EMMC_PART_USER = 8                    # EmmcPartitionType.MTK_DA_EMMC_PART_USER (mtkclient Library/DA/storage.py:39)
MIN_RAW_WRITE = 0x400000              # no raw write address below this is ever sent (low addresses = preloader area)


class Target(NamedTuple):
    name: str
    start: int   # DEVICE-relative (scatter) start
    size: int

    @property
    def end(self):
        return self.start + self.size


class Bases(NamedTuple):
    read: int
    write: int


# The ONLY writable regions. Everything else (PRELOADER/boot1, UBOOT, SECCFG, NVRAM, PRO_INFO,
# PROTECT_F/S, SEC_RO, MBR/EBR, ANDROID, ...) is deliberately absent.
TARGETS = {t.name: t for t in (
    Target("BOOTIMG", 0x1D80000, 0x1000000),
    Target("RECOVERY", 0x2D80000, 0x1000000),
    Target("USRDATA", 0x40380000, 0x32000000),
)}

# Canary-landing-only region: crash-dump scratch (EXPDB). Never a write target, never in TARGETS.
EXPDB = (0x4780000, 0x5180000)
CANARY_LEN = 4096


class Y2Error(Exception):
    exit_code = 1


class SafetyViolation(Y2Error):
    """A safety invariant failed (an explicit raise, so `python -O` cannot strip it)."""


def require(cond, msg):
    if not cond:
        raise SafetyViolation(f"safety check failed: {msg}")


class PreReadMismatch(Y2Error):
    exit_code = 3


class WriteFailed(Y2Error):
    exit_code = 2


class CanaryFailed(Y2Error):
    exit_code = 6


_WRITES = {"started": 0, "done": 0, "canary": None}


def say(msg=""):
    print(msg, flush=True)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def pad_sector(data):
    """Zero-pad to a whole number of 512-byte sectors, nothing more."""
    rem = len(data) % SECTOR
    return bytes(data) if rem == 0 else bytes(data) + b"\0" * (SECTOR - rem)


def first_diff(a, b):
    """Offset of the first differing byte (a length difference counts), or None if equal."""
    if a == b:
        return None
    n = min(len(a), len(b))
    lo = 0
    # chunked search: cheap even for an 800 MiB range
    step = 1 << 20
    while lo < n and a[lo:lo + step] == b[lo:lo + step]:
        lo += step
    for i in range(lo, min(lo + step, n)):
        if a[i] != b[i]:
            return i
    return n


# ---------------------------------------------------------------- allowlist

def get_target(name):
    t = TARGETS.get(name)
    if t is None:
        raise Y2Error(f"refusing target {name!r}: only {', '.join(TARGETS)} may be written or read here")
    return t


def check_write_range(dev_addr, length):
    """The allowlist gate, in DEVICE terms: returns the target containing the range or raises."""
    if length <= 0 or dev_addr % SECTOR or length % SECTOR:
        raise Y2Error(f"refusing write at device {dev_addr:#x} len {length:#x}: must be non-empty and "
                      "512-byte aligned")
    for t in TARGETS.values():
        if t.start <= dev_addr and dev_addr + length <= t.end:
            return t
    raise Y2Error(f"refusing write at device {dev_addr:#x} len {length:#x}: not inside an allowed target "
                  f"({', '.join(f'{t.name} {t.start:#x}+{t.size:#x}' for t in TARGETS.values())})")


def check_probe_range(raw_addr, length):
    """For the probe's raw write address (base unknown): the write must land inside USRDATA for EVERY
    base in [0, MAX_BASE]."""
    u = TARGETS["USRDATA"]
    if length <= 0 or raw_addr % SECTOR or length % SECTOR:
        raise Y2Error("probe write must be non-empty and 512-byte aligned")
    if not (u.start <= raw_addr - MAX_BASE and raw_addr + length <= u.end):
        raise Y2Error(f"refusing probe write at raw {raw_addr:#x} len {length:#x}: not inside USRDATA "
                      f"for every base in [0, {MAX_BASE:#x}]")


def announced_write(da, label, raw, data):
    """Every write to the device goes through here: announced, user area selected, then written."""
    da.switch_user()       # the legacy write command does not switch partitions itself; a failure here
    #                        means nothing was written, so it must not look like a started write
    say(f"WRITE STARTED {label} raw {raw:#x} len {len(data):#x}")
    _WRITES["started"] += 1
    da.write(raw, data)
    _WRITES["done"] += 1
    say(f"WRITE DONE {label}")


def guarded_write(da, target, dev_addr, data, write_base):
    """The single place a normal write reaches the device (the real adapter re-checks it as well).
    dev_addr is device-relative; the raw address sent to the DA is dev_addr + write_base."""
    require(target is TARGETS.get(target.name), "target is not from the allowlist")
    require(target.start <= dev_addr and dev_addr + len(data) <= target.end, "write outside target range")
    require(0 <= write_base <= MAX_BASE, "write base out of range")
    check_write_range(dev_addr, len(data))
    try:
        announced_write(da, f"{target.name} device {dev_addr:#x}", dev_addr + write_base, data)
    except Exception as e:
        raise WriteFailed(f"[{target.name}] write FAILED part-way ({e}). The device may be half-written. "
                          f"{RESTORE_HINT}")


# ---------------------------------------------------------------- planning

def plan_image(target, image):
    """Padded bytes to write: len(image) rounded up to 512, nothing else padded."""
    if len(image) == 0:
        raise Y2Error("image is empty")
    if len(image) > target.size:
        raise Y2Error(f"image is {len(image):#x} bytes, larger than {target.name} ({target.size:#x})")
    data = pad_sector(image)
    if len(data) != len(image):
        say(f"note: image is {len(image):#x} bytes, not a multiple of 512; zero-padding {len(data) - len(image)} "
            f"bytes to {len(data):#x} (the rest of {target.name} is left untouched)")
    return data


class Job:
    """One target: image to write, plus acceptable current contents [(label, bytes of target.size)]."""

    def __init__(self, target, image, expected, label=""):
        self.target = target
        self.image = image
        self.expected = expected    # None = unchecked (only via --force-unknown-current)
        self.label = label or target.name
        self.data = plan_image(target, image)


def load_expected(target, paths):
    out, seen = [], set()
    for p in paths:
        try:
            with open(p, "rb") as f:
                b = f.read()
        except OSError as e:
            raise Y2Error(f"cannot read expected-current file {p}: {e}")
        if len(b) != target.size:
            raise Y2Error(f"expected-current file {p} is {len(b):#x} bytes, must be exactly "
                          f"{target.name}'s size {target.size:#x}")
        if sha(b) not in seen:
            seen.add(sha(b))
            out.append((p, b))
    if not out:
        raise Y2Error(f"no expected-current file for {target.name}")
    return out


# ---------------------------------------------------------------- session steps (da = read/write seam)

def check_device(da):
    if da.emmc_user_size != EXPECT_EMMC_USER_SIZE:
        raise Y2Error(f"device reports eMMC user size {da.emmc_user_size:#x}, expected "
                      f"{EXPECT_EMMC_USER_SIZE:#x}: this is not the Y2 these addresses were proven on; aborting")
    say(f"device check ok: eMMC user size {da.emmc_user_size:#x}")


def verify_current(da, job, bases, keep_dir=None):
    """Pre-read the whole target and require it to equal an acceptable content. Never writes."""
    t = job.target
    say(f"[{t.name}] pre-read {t.size:#x} bytes at device {t.start:#x} (raw read addr {t.start + bases.read:#x})")
    cur = da.read(t.start + bases.read, t.size)
    if len(cur) != t.size:
        raise Y2Error(f"[{t.name}] pre-read returned {len(cur):#x} bytes, wanted {t.size:#x}")
    say(f"[{t.name}] current sha256 {sha(cur)}")
    if job.expected is None:
        say(f"[{t.name}] WARNING: current content NOT checked (--force-unknown-current)")
        return cur, "unchecked"
    for label, b in job.expected:
        if b == cur:
            say(f"[{t.name}] current content matches {label}")
            return cur, label
    keep = None
    if keep_dir:
        keep = os.path.join(keep_dir, f"preread-{t.name}-{time.strftime('%Y%m%d-%H%M%S')}.bin")
        with open(keep, "wb") as f:
            f.write(cur)
    lines = [f"[{t.name}] PRE-READ MISMATCH: the device does not hold any expected content. NOTHING WAS WRITTEN."]
    for label, b in job.expected:
        off = first_diff(cur, b)
        lines.append(f"  vs {label} (sha256 {sha(b)}): first difference at offset {off:#x} "
                     f"(device {cur[off]:#04x}, expected {b[off]:#04x})")
    if keep:
        lines.append(f"  pre-read kept at {keep}")
    raise PreReadMismatch("\n".join(lines))


def write_and_verify(da, job, bases):
    """Write job.data, read it back, compare. Returns (image_sha, readback_sha)."""
    t, data = job.target, job.data
    say(f"[{t.name}] WRITE {len(data):#x} bytes at device {t.start:#x} (raw write addr {t.start + bases.write:#x}); "
        f"image sha256 {sha(data)}")
    guarded_write(da, t, t.start, data, bases.write)
    say(f"[{t.name}] read back {len(data):#x} bytes (raw read addr {t.start + bases.read:#x})")
    try:
        back = da.read(t.start + bases.read, len(data))
    except Exception as e:
        raise WriteFailed(f"[{t.name}] readback FAILED ({e}); the write is UNVERIFIED. {RESTORE_HINT}")
    say(f"[{t.name}] image    sha256 {sha(data)}")
    say(f"[{t.name}] readback sha256 {sha(back)}")
    if back != data:
        off = first_diff(back, data)
        raise WriteFailed(f"[{t.name}] READBACK MISMATCH at offset {off:#x}. THE DEVICE HOLDS UNVERIFIED DATA. "
                          f"{RESTORE_HINT}")
    say(f"[{t.name}] readback matches")
    return sha(data), sha(back)


RESTORE_HINT = ("DO NOT power-cycle into it blindly: re-enter BROM (hold Vol-, plug USB) and run "
                "scripts/restore-stock.sh to write the backup back (add --force-unknown-current if it refuses "
                "because the device content matches nothing known).")


def post_image(cur, job):
    """What the target will hold after the write: current content with the image over its start."""
    return job.data + cur[len(job.data):]


# ---------------------------------------------------------------- canary (per target, before any full write)
#
# The calibration was measured on USRDATA; a canary proves the mapping holds at EACH target before 16 MiB goes
# out. One 4 KiB no-zero marker is written at raw C+write_base (C = last sector of the target; USRDATA: 16 MiB
# in), the surrounding +-0xb80000 window is read back (READ command, safe) and the marker must be there exactly
# once, exactly at C, with nothing else in the window changed. Then C's original bytes are written back.
# Blast radius if the base is wrong by +-0xb80000 (asserted in canary_guards for every target):
#   RECOVERY (C=0x3D7F000): lands inside RECOVERY (0x31FF000) or in EXPDB (0x48FF000, crash-dump scratch)
#   BOOTIMG  (C=0x2D7F000): lands inside BOOTIMG (0x21FF000) or inside RECOVERY (0x38FF000)
#   USRDATA  (C=start+16 MiB): lands inside USRDATA. Never in a protected partition.

def canary_addr(t):
    return t.start + 0x1000000 if t.name == "USRDATA" else t.end - CANARY_LEN


def canary_guards(t):
    c = canary_addr(t)
    require(t.start <= c and c + CANARY_LEN <= t.end and c % SECTOR == 0, f"canary for {t.name} outside the target")
    landing = [(x.start, x.end) for x in TARGETS.values()] + [EXPDB]
    for shift in (-READ_BASE, READ_BASE):
        a = c + shift
        require(any(lo <= a and a + CANARY_LEN <= hi for lo, hi in landing),
                f"canary for {t.name} could land outside BOOTIMG/RECOVERY/EXPDB/USRDATA at {a:#x}")
    return c


def find_all(hay, needle):
    out, i = [], hay.find(needle)
    while i != -1:
        out.append(i)
        i = hay.find(needle, i + 1)
    return out


def run_canary(da, job, cur, bases, keep_dir=None, nonce=None, when=None):
    t = job.target
    c = canary_guards(t)
    ws, we = c - READ_BASE - CANARY_LEN, c + READ_BASE + CANARY_LEN
    n, off = we - ws, c - ws
    orig = cur[c - t.start:c - t.start + CANARY_LEN]
    nonce = os.urandom(16) if nonce is None else nonce
    when = time.strftime("%Y-%m-%dT%H:%M:%S%z") if when is None else when
    marker = make_marker(nonce, when, prefix=b"Y2CANARY")
    say(f"[{t.name}] canary at device {c:#x} (raw write {c + bases.write:#x}); window {ws:#x}..{we:#x}")
    pre = da.read(ws + bases.read, n)
    if len(pre) != n or pre[off:off + CANARY_LEN] != orig:
        raise Y2Error(f"[{t.name}] canary window changed since the pre-read (or short read). NOTHING WAS WRITTEN.")
    _WRITES["canary"] = (t.name, c, c + bases.write)    # for the interrupt message; cleared once resolved
    guarded_write(da, t, c, marker, bases.write)
    try:
        post = da.read(ws + bases.read, n)
    except Exception as e:
        raise CanaryFailed(f"[{t.name}] canary readback failed ({e}); the canary marker (Y2CANARY nonce "
                           f"{nonce.hex()}) may be on the device. {RESTORE_HINT}")
    hits = find_all(post, marker)
    clean = hits == [off] and post[:off] == pre[:off] and post[off + CANARY_LEN:] == pre[off + CANARY_LEN:]
    if clean:
        guarded_write(da, t, c, orig, bases.write)
        back = da.read(c + bases.read, CANARY_LEN)
        if back != orig:
            raise CanaryFailed(f"[{t.name}] canary restore FAILED: device {c:#x} does not hold its original bytes. "
                               f"{RESTORE_HINT}")
        say(f"[{t.name}] canary landed exactly at {c:#x}; original bytes restored")
        _WRITES["canary"] = None
        return

    def keep(name, data):
        if not keep_dir:
            return ""
        p = os.path.join(keep_dir, f"canary-{t.name}-{name}-{time.strftime('%Y%m%d-%H%M%S')}.bin")
        with open(p, "wb") as f:
            f.write(data)
        return f" {name} window saved to {p}."

    saved = keep("pre", pre) + keep("post", post)
    where = ", ".join(f"{ws + h:#x}" for h in hits) or "nowhere in the window"
    head = (f"[{t.name}] CANARY FAILED: expected the marker at device {c:#x}; found at {where}. NO FULL WRITE WAS "
            "DONE.")
    if len(hits) == 1 and post[:hits[0]] == pre[:hits[0]] and post[hits[0] + CANARY_LEN:] == pre[hits[0] + CANARY_LEN:]:
        h = hits[0]
        say(head + f" Restoring the original bytes of device {ws + h:#x} at the same raw address.")
        guarded_write(da, t, c, pre[h:h + CANARY_LEN], bases.write)
        after = da.read(ws + bases.read, n)
        if after == pre:
            raise CanaryFailed(head + " The stray canary was restored (window identical to before). The write base "
                               f"does not hold at {t.name}: do not flash; investigate." + saved)
        raise CanaryFailed(head + " The restore did NOT bring the window back: the device holds stray data."
                           + saved + " " + RESTORE_HINT)
    changed = post != pre
    raise CanaryFailed(head + (" The window changed in a way that is not one intact marker; NOT touching it. "
                               "Compare the saved windows. " + RESTORE_HINT if changed else
                               " The window did not change at all (the write was dropped or landed outside it).")
                       + saved)


def _finish(state, entry, status):
    """Update the flash-state entry; a failure here is a warning, never a reason to hide the real outcome."""
    try:
        state.finish(entry, status)
        return True
    except Exception as e:
        say(f"WARNING: could not record status {status!r} in the flash-state file ({e}). The device operation's "
            "outcome is as printed above; fix the state file by hand before the next flash.")
        return False


def run_jobs(da, jobs, bases, keep_dir=None, state=None):
    """Phase 1: pre-read + compare EVERY target (no writes). Phase 2: a canary per target (restored at once).
    Phase 3: per target in the given order, write then read back; stop at the first failure."""
    if not jobs:
        raise Y2Error("nothing to do")
    names = [j.target.name for j in jobs]
    if len(set(names)) != len(names):
        raise Y2Error("a target may appear only once per session")
    check_device(da)
    say(f"bases: read {bases.read:#x}, write {bases.write:#x}")
    curs = [verify_current(da, j, bases, keep_dir) for j in jobs]
    for job, (cur, _) in zip(jobs, curs):
        run_canary(da, job, cur, bases, keep_dir)
    results = []
    for job, (cur, matched) in zip(jobs, curs):
        entry = state.begin(job, post_image(cur, job)) if state else None
        try:
            img_sha, back_sha = write_and_verify(da, job, bases)
        except Y2Error:
            if state:
                _finish(state, entry, "failed")
            raise
        if state:
            _finish(state, entry, "ok")
        results.append((job.target.name, img_sha, back_sha, matched))
    return results


# ---------------------------------------------------------------- write-base calibration + probe

PROBE_P = 0x4C500000                   # device address, middle of the all-zero run in the backup
PROBE_HALF = 48 << 20
PROBE_LEN = 4096
ZERO_RUN = (0x48680000, 0x50380000)    # all-zero in ~/y2-backup/2026-11-10 (checked 2026-11-10)


class ProbeFailed(Y2Error):
    exit_code = 4


class ProbeUnexpectedBase(Y2Error):
    exit_code = 5


def load_calibration(path, current_commit=None, strict=False):
    """Bases for a real write, or refuse. Fail-closed: missing/untrusted/odd file -> no write.
    Only write bases in KNOWN_WRITE_BASES are ever trusted; there is no manual override: an unexpected base
    means stop and investigate."""
    try:
        with open(path) as f:
            doc = json.load(f)
    except FileNotFoundError:
        raise Y2Error(f"no write calibration at {path}: the write address base is UNKNOWN. Run "
                      "scripts/probe-write.sh first. Refusing to write.")
    except (OSError, ValueError) as e:
        raise Y2Error(f"write calibration {path} unreadable: {e}")
    wb, rb = doc.get("write_base"), doc.get("read_base")
    if not isinstance(wb, int) or isinstance(wb, bool) or rb != READ_BASE:
        raise Y2Error(f"write calibration {path} is invalid (write_base {wb!r}, read_base {rb!r}); "
                      f"reason recorded: {doc.get('reason')!r}")
    if wb not in KNOWN_WRITE_BASES:
        raise Y2Error(f"write calibration {path}: write base {wb:#x} is not one of "
                      f"{[hex(b) for b in KNOWN_WRITE_BASES]}. Refusing, and no override exists: stop and investigate.")
    if doc.get("trusted") is not True:
        raise Y2Error(f"write calibration {path} is not trusted (reason: {doc.get('reason')!r}). Refusing.")
    for key, want in (("emmc_user_size", EXPECT_EMMC_USER_SIZE), ("hwcode", EXPECT_HWCODE),
                      ("probe_write_addr", PROBE_P)):
        if doc.get(key) != want:
            raise Y2Error(f"write calibration {path}: {key} is {doc.get(key)!r}, expected {want:#x}. "
                          "Re-run the probe. Refusing.")
    cc = doc.get("mtkclient_commit")
    if strict and not current_commit:
        raise Y2Error("cannot determine the installed mtkclient commit (.venv/.mtkclient-commit missing): "
                      "the calibration cannot be bound to it. Refusing.")
    if strict and not cc:
        raise Y2Error(f"write calibration {path} records no mtkclient commit: re-run the probe. Refusing.")
    if current_commit and cc and cc != current_commit:
        raise Y2Error(f"write calibration was measured with mtkclient {cc}, this is {current_commit}: re-run the "
                      "probe. Refusing.")
    return Bases(read=rb, write=wb)


def make_marker(nonce, when, prefix=b"Y2PROBE"):
    """4096 bytes, no zero byte anywhere: prefix + nonce + time, repeated."""
    unit = prefix + bytes(b or 1 for b in nonce) + when.encode("ascii").replace(b"\0", b"_") + b"|"
    return (unit * (PROBE_LEN // len(unit) + 1))[:PROBE_LEN]


def probe_guards():
    """Checked on every probe: P, window and marker are inside USRDATA (and the known-zero run) under
    ANY base in [0, MAX_BASE]."""
    u = TARGETS["USRDATA"]
    ws, we = PROBE_P - PROBE_HALF, PROBE_P + PROBE_HALF
    require(u.start <= ws and we <= u.end, "probe window outside USRDATA")
    require(ZERO_RUN[0] <= ws and we <= ZERO_RUN[1], "probe window outside the known all-zero run")
    require(ws <= PROBE_P - MAX_BASE and PROBE_P + PROBE_LEN <= we, "marker may leave the window")
    require(u.start <= PROBE_P - MAX_BASE and PROBE_P + PROBE_LEN <= u.end, "marker may leave USRDATA")
    check_probe_range(PROBE_P, PROBE_LEN)
    return ws, we


def probe_write(da, raw, data, what):
    """The only way the probe writes: raw address confined to USRDATA for every base, announced, user area."""
    check_probe_range(raw, len(data))
    require(raw >= MIN_RAW_WRITE, "raw write address below the minimum")
    announced_write(da, f"probe-{what} USRDATA", raw, data)


def nonzero_runs(buf):
    """Maximal runs of 512-byte sectors that contain a non-zero byte: [(start, end)]."""
    runs, pos, n = [], 0, len(buf)
    zero = bytes(SECTOR)
    while True:
        rest = buf[pos:]
        skip = len(rest) - len(rest.lstrip(b"\0"))
        if skip == len(rest):
            return runs
        s = pos + skip
        s -= s % SECTOR
        e = s
        while e < n and buf[e:e + SECTOR] != zero:
            e += SECTOR
        runs.append((s, e))
        pos = e


def probe_cleanup(da, post, ws, marker, read_base):
    """After a failed probe: zero stray marker data in the window, but only if EVERY non-zero byte is
    sector-aligned, inside ZERO_RUN and equal to the marker pattern at some sector offset. Returns a status
    sentence (never raises for 'nothing to do')."""
    n = len(post)
    runs = nonzero_runs(post)
    if not runs:
        return "The window is still all zero (the write was dropped or landed outside it)."
    ks = set()
    for s, e in runs:
        ln = e - s
        if ln > PROBE_LEN or not (ZERO_RUN[0] <= ws + s and ws + e <= ZERO_RUN[1]):
            return f"Non-zero data at {ws + s:#x}..{ws + e:#x} is not marker-sized: NOT cleaning. "
        ok = [k for k in range(0, PROBE_LEN - ln + 1, SECTOR) if marker[k:k + ln] == post[s:e]]
        if not ok:
            return f"Non-zero data at {ws + s:#x}..{ws + e:#x} does not match the marker: NOT cleaning. "
        ks.add((ok[0], ln))
    for k, ln in sorted(ks):
        # a marker slice starting at offset k of the block written at raw PROBE_P is written at PROBE_P + k
        probe_write(da, PROBE_P + k, bytes(ln), "cleanup")
    after = da.read(ws + read_base, n)
    if after.count(0) == n:
        return "Stray marker data was zeroed and the window is all zero again."
    return "Cleanup ran but the window is STILL not all zero: run probe --locate before anything else."


def locate_marker(da, nonce, read_base=READ_BASE):
    """Read all of USRDATA (READ command) and return the device offsets of intact marker blocks with this nonce."""
    u = TARGETS["USRDATA"]
    needle = b"Y2PROBE" + bytes(b or 1 for b in nonce)
    found, pos, step, overlap = set(), u.start, 128 << 20, 2 * PROBE_LEN
    while pos < u.end:
        n = min(step + overlap, u.end - pos)
        data = da.read(pos + read_base, n)
        i = data.find(needle)
        while i != -1:
            nxt = data.find(needle, i + 1)
            if nxt != -1 and (pos + i) % SECTOR == 0:
                unit = data[i:nxt]
                blk = data[i:i + PROBE_LEN]
                if len(blk) == PROBE_LEN and blk == (unit * (PROBE_LEN // len(unit) + 1))[:PROBE_LEN]:
                    found.add(pos + i)
            i = nxt
        pos += step
    return sorted(found)


def run_locate(da, nonce, read_base=READ_BASE):
    """probe --locate: find the marker, and zero it only if it is inside USRDATA, matches the marker and its
    position is consistent with a base in [0, MAX_BASE] for the original probe write (raw PROBE_P)."""
    check_device(da)
    u = TARGETS["USRDATA"]
    found = locate_marker(da, nonce, read_base)
    if not found:
        say("locate: marker not found anywhere in USRDATA")
        return []
    say("locate: marker blocks at device " + ", ".join(f"{d:#x}" for d in found))
    todo = [d for d in found if u.start <= d and d + PROBE_LEN <= u.end and PROBE_P - MAX_BASE <= d <= PROBE_P]
    if not todo:
        say("locate: none of them is consistent with the probe write address; NOT touching anything")
        return found
    d = todo[0]
    probe_write(da, PROBE_P, bytes(PROBE_LEN), "locate-cleanup")
    back = da.read(d + read_base, PROBE_LEN)
    if back.count(0) != PROBE_LEN:
        raise WriteFailed(f"locate: block at {d:#x} is still not zero after the cleanup write; investigate")
    say(f"locate: zeroed the block at {d:#x}" + ("; other blocks remain, run --locate again" if len(found) > 1 else ""))
    return found


def run_probe(da, keep_dir=None, nonce=None, when=None, read_base=READ_BASE, mtk_commit="", persist=None):
    """Measure WRITE_BASE with one marker write confined to USRDATA. Returns the calibration dict.
    persist(doc) is called BEFORE the first write (nonce on record), when the base is known, and at the end,
    so a crash or failed restore still leaves the measurement and a reason behind (trusted false)."""
    persist = persist or (lambda doc: None)
    ws, we = probe_guards()
    nonce = os.urandom(16) if nonce is None else nonce
    when = time.strftime("%Y-%m-%dT%H:%M:%S%z") if when is None else when
    marker = make_marker(nonce, when)
    check_device(da)
    n = we - ws
    doc = {"read_base": read_base, "trusted": False, "marker_nonce": nonce.hex(), "marker_when": when,
           "probe_write_addr": PROBE_P, "emmc_user_size": da.emmc_user_size, "hwcode": EXPECT_HWCODE,
           "mtkclient_commit": mtk_commit}
    say(f"probe: window device {ws:#x}..{we:#x} (raw read addr {ws + read_base:#x}), marker at raw write addr {PROBE_P:#x}")
    pre = da.read(ws + read_base, n)
    if len(pre) != n or pre.count(0) != n:
        raise PreReadMismatch("PROBE PRE-READ: the window is not all zero (or short). NOTHING WAS WRITTEN.")
    say(f"probe: window is all zero; nonce {nonce.hex()} (recorded before the write); writing marker once")
    persist({**doc, "reason": "probe started: marker may be on the device; run probe --locate --nonce "
                              f"{nonce.hex()} if the probe did not finish"})
    try:
        probe_write(da, PROBE_P, marker, "marker")
        post = da.read(ws + read_base, n)
    except Exception as e:
        raise WriteFailed(f"probe write/readback failed ({e}); the marker may be on the device in USRDATA "
                          f"(nonce {nonce.hex()}). Do not flash. Run probe --locate --nonce {nonce.hex()} first.")

    def save_window():
        if not keep_dir:
            return ""
        p = os.path.join(keep_dir, f"probe-window-{time.strftime('%Y%m%d-%H%M%S')}.bin")
        with open(p, "wb") as f:
            f.write(post)
        return f" Window read saved to {p}."

    first = len(post) - len(post.lstrip(b"\0"))
    last = len(post.rstrip(b"\0"))
    if first >= n or last - first != PROBE_LEN or post[first:last] != marker or (ws + first) % SECTOR:
        try:
            cleaned = probe_cleanup(da, post, ws, marker, read_base)
        except Exception as e:
            cleaned = f"Cleanup itself failed ({e}). "
        persist({**doc, "reason": "marker not found as one intact block: " + cleaned})
        raise ProbeFailed(
            "PROBE FAILED: the marker was not found exactly once as one intact 4096-byte block in the window "
            f"(non-zero span {'none' if first >= n else f'{first:#x}..{last:#x}'}). {cleaned}"
            f"This assumes byte-addressed writes and a base within [0, {MAX_BASE:#x}]; a write that went "
            f"elsewhere is not excluded. Run `probe --locate --nonce {nonce.hex()}` (scripts/probe-write.sh "
            f"--locate {nonce.hex()}) or a full-device diff against the backup BEFORE anything else. "
            "Do not flash." + save_window())
    d = ws + first
    wb = PROBE_P - d
    say(f"probe: marker found at device {d:#x} => WRITE_BASE = {PROBE_P:#x} - {d:#x} = {wb:#x}")
    trusted = wb in KNOWN_WRITE_BASES
    doc.update(write_base=wb, device_offset_found=d, measured_at=time.strftime("%Y-%m-%dT%H:%M:%S%z"))
    persist({**doc, "reason": "base measured; marker restore not yet verified"})
    if not trusted:
        say(f"!!!! WARNING: write base {wb:#x} is not one of {[hex(b) for b in KNOWN_WRITE_BASES]}. "
            "Recorded as NOT trusted; flashing stays refused: stop and investigate. !!!!")
    say("probe: restoring zeros at the same write address")
    try:
        probe_write(da, PROBE_P, bytes(PROBE_LEN), "restore")
        after = da.read(ws + read_base, n)
    except Exception as e:
        raise WriteFailed(f"probe restore failed ({e}): the marker block at device {d:#x} may still be there "
                          f"(nonce {nonce.hex()}); run probe --locate --nonce {nonce.hex()}.")
    if after.count(0) != n:
        raise WriteFailed(f"PROBE RESTORE FAILED: the window is not all zero again (first non-zero at offset "
                          f"{len(after) - len(after.lstrip(bytes(1))):#x}). The all-zero run in USRDATA is dirty; "
                          f"run probe --locate --nonce {nonce.hex()}.")
    say("probe: window all zero again")
    final = {**doc, "trusted": trusted}
    if trusted:
        final.pop("reason", None)
    else:
        final["reason"] = f"unexpected write base {wb:#x}: stop and investigate"
    return final


# ---------------------------------------------------------------- flash-state (what the device may hold)

class State:
    """JSON history of writes. Before each write an `attempted` entry is stored together with a full
    copy of what the target will hold afterwards, so it stays an acceptable pre-read content even if
    the session dies mid-write or the later steps fail."""

    def __init__(self, path):
        self.path = path
        self.dir = os.path.join(os.path.dirname(os.path.abspath(path)), "flashed")

    def load(self):
        try:
            with open(self.path) as f:
                return json.load(f)
        except FileNotFoundError:
            return {"history": []}
        except (OSError, ValueError) as e:
            raise Y2Error(f"flash-state file {self.path} unreadable: {e}")

    def save(self, doc):
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(doc, f, indent=1)
        os.replace(tmp, self.path)

    def begin(self, job, post):
        os.makedirs(self.dir, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        path = os.path.join(self.dir, f"{job.target.name}-{stamp}-{sha(job.data)[:12]}.post")
        with open(path, "wb") as f:
            f.write(post)
        doc = self.load()
        doc["history"].append({"time": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "target": job.target.name,
                               "image_sha256": sha(job.data), "image_len": len(job.data),
                               "post_image": path, "post_sha256": sha(post), "status": "attempted"})
        self.save(doc)
        return len(doc["history"]) - 1

    def finish(self, idx, status):
        doc = self.load()
        doc["history"][idx]["status"] = status
        self.save(doc)

    def expected_paths(self, target, all_entries=False):
        """Post-images that the target may hold: the last ok write and everything attempted after it
        (all entries with all_entries=True)."""
        hist = [e for e in self.load()["history"] if e["target"] == target and os.path.exists(e["post_image"])]
        if not all_entries:
            last_ok = max((i for i, e in enumerate(hist) if e["status"] == "ok"), default=-1)
            hist = hist[last_ok:] if last_ok >= 0 else hist
        return [e["post_image"] for e in hist]


# ---------------------------------------------------------------- real device adapter (mtkclient, lazy)

class MtkDa:
    """read(addr, len) / write(addr, data) over one mtkclient legacy-DA session.

    Gotchas handled here (mtkclient 2.1.4.1, Library/DA/legacy/dalegacy_lib.py):
      * addresses are BYTES (raw = device + base, see module doc); parttype "user" selects the user area (names like RECOVERY mean nothing);
      * readflash(filename="") only returns the first 1 MiB packet (bug: bytestoread = len(buffer)-length),
        so reads always go through a file and the size is checked afterwards;
      * readflash calls exit(1) on a failed command -> SystemExit is converted to an error;
      * writeflash(wdata=bytes) pads/clamps itself, we pass an already 512-aligned buffer and it returns
        True/False.
    """

    write_base = None   # set by the caller once calibrated; None = probe mode (USRDATA-only confinement)
    _switched = False

    def __init__(self, mtk, workdir):
        self.mtk = mtk
        self.workdir = workdir
        dc = mtk.daloader.daconfig
        from mtkclient.config.brom_config import DAmodes
        if mtk.daloader.flashmode != DAmodes.LEGACY or dc.storage.flashtype != "emmc":
            raise Y2Error("device did not come up as legacy DA + eMMC; refusing")
        if mtk.config.hwcode != EXPECT_HWCODE:
            raise Y2Error(f"hwcode {mtk.config.hwcode:#x}, expected {EXPECT_HWCODE:#x} (MT6582); refusing")
        self.emmc_user_size = dc.legacy_storage.emmc.m_emmc_ua_size

    def read(self, addr, length):
        path = os.path.join(self.workdir, "read.tmp")
        try:
            ok = self.mtk.daloader.readflash(addr=addr, length=length, filename=path, parttype="user", display=True)
        except SystemExit:
            raise Y2Error("mtkclient aborted the read (exit)")
        if ok is not True or not os.path.exists(path) or os.path.getsize(path) != length:
            raise Y2Error(f"read at {addr:#x} len {length:#x} failed or came back short")
        with open(path, "rb") as f:
            data = f.read()
        os.unlink(path)
        return data

    def switch_user(self):
        """Select the eMMC user area right before a write: DALegacy.sdmmc_switch_part (dalegacy_lib.py:961);
        the legacy write command (sdmmc_write_data) never switches partitions, only readflash does."""
        try:
            ok = self.mtk.daloader.da.sdmmc_switch_part(EMMC_PART_USER)
        except SystemExit:
            raise Y2Error("mtkclient aborted the partition switch (exit)")
        if ok is not True:
            raise Y2Error("could not switch the eMMC to the user area; refusing to write")
        self._switched = True

    def write(self, addr, data):
        # defence in depth, same allowlist as guarded_write; addr is the RAW address
        if not self._switched:
            raise Y2Error("write without a preceding switch to the user area: refusing")
        self._switched = False
        if addr < MIN_RAW_WRITE:
            raise Y2Error(f"refusing raw write address {addr:#x} below {MIN_RAW_WRITE:#x}")
        if self.write_base is None:
            check_probe_range(addr, len(data))
        else:
            check_write_range(addr - self.write_base, len(data))
        try:
            ok = self.mtk.daloader.writeflash(addr=addr, length=len(data), filename="", offset=0,
                                              parttype="user", wdata=bytes(data), display=True)
        except SystemExit:
            raise Y2Error("mtkclient aborted the write (exit)")
        if ok is not True:
            raise Y2Error("mtkclient writeflash reported failure")


@contextlib.contextmanager
def open_session(preloader, workdir):
    """Connect the way `mtk --preloader P multi ...` does (Library/mtk_main.py Main.run)."""
    if not preloader or not os.path.isfile(preloader):
        raise Y2Error(f"preloader file not found: {preloader}")
    import logging
    from types import SimpleNamespace
    from mtkclient.Library.mtk_class import Mtk
    from mtkclient.Library.mtk_main import ArgHandler
    from mtkclient.Library.DA.mtk_da_handler import DaHandler
    from mtkclient.config.mtk_config import MtkConfig
    config = MtkConfig(loglevel=logging.INFO, gui=None, guiprogress=None)
    ArgHandler(SimpleNamespace(preloader=preloader), config)   # sets preloader_filename + preloader bytes
    config.hwparam_path = workdir   # fresh dir: a stale ./.state would make connect() skip the handshake
    mtk = Mtk(config=config, loglevel=logging.INFO, serialportname=None)
    config.set_peek(mtk.daloader.peek)
    mtk.config.preloader_filename = preloader
    try:
        handler = DaHandler(mtk, logging.INFO)
        mtk = handler.connect(mtk, workdir)
        if mtk is None:
            raise Y2Error("could not connect (replug and retry)")
        mtk = handler.configure_da(mtk)
        if mtk is None:
            raise Y2Error("DA setup failed")
    except SystemExit:
        raise Y2Error("mtkclient aborted during connect/DA setup")
    yield MtkDa(mtk, workdir)


# ---------------------------------------------------------------- CLI

class Tee:
    def __init__(self, a, b):
        self.a, self.b = a, b

    def write(self, s):
        self.a.write(s)
        self.b.write(s)
        return len(s)

    def flush(self):
        self.a.flush()
        self.b.flush()

    def isatty(self):
        return False


def current_commit():
    try:
        with open(os.path.join(sys.prefix, ".mtkclient-commit")) as f:
            return f.read().strip()
    except OSError:
        return ""


def kv(s):
    k, sep, v = s.partition("=")
    if not sep or not k or not v:
        raise Y2Error(f"expected TARGET=VALUE, got {s!r}")
    return k, v


def build_jobs(args):
    if args.cmd == "write":
        t = get_target(args.target)
        pairs, expect = [(t, args.image)], {t.name: args.expect_current}
    else:
        pairs = [(get_target(k), v) for k, v in map(kv, args.job)]
        expect = {}
        for k, v in map(kv, args.expect or []):
            expect.setdefault(get_target(k).name, []).append(v)
    jobs = []
    for t, img in pairs:
        try:
            with open(img, "rb") as f:
                image = f.read()
        except OSError as e:
            raise Y2Error(f"cannot read image {img}: {e}")
        if getattr(args, "force_unknown_current", False):
            exp = None
        else:
            if t.name not in expect:
                raise Y2Error(f"no --expect-current/--expect for {t.name}")
            exp = load_expected(t, expect[t.name])
        jobs.append(Job(t, image, exp, label=img))
    return jobs


def print_plan(jobs, state, bases):
    say("PLAN (nothing is connected or written):")
    if bases is None:
        say(f"  READ_BASE {READ_BASE:#x}; WRITE_BASE UNCALIBRATED: a real run would REFUSE (run probe-write.sh)")
    else:
        say(f"  READ_BASE {bases.read:#x}; WRITE_BASE {bases.write:#x} (from write calibration)")
    for j in jobs:
        t = j.target
        say(f"  target {t.name}: device (scatter) {t.start:#x}, size {t.size:#x}, parttype user; raw read addr "
            f"{t.start + READ_BASE:#x}; raw write addr "
            f"{'?' if bases is None else hex(t.start + bases.write)}")
        say(f"    image {j.label}: {len(j.image):#x} bytes -> write {len(j.data):#x} bytes (512-aligned), "
            f"sha256 {sha(j.image)}")
        say(f"    written bytes sha256 {sha(j.data)}")
        if j.expected is None:
            say("    expected current: UNCHECKED (--force-unknown-current)")
        for label, b in j.expected or []:
            say(f"    expected current: {sha(b)}  {label}")
    say(f"  device must report eMMC user size {EXPECT_EMMC_USER_SIZE:#x}; "
        f"order: {' then '.join(j.target.name for j in jobs)}")
    say("  steps: pre-read+compare all targets; a 4 KiB canary per target (written, located by read, restored); "
        "then per target write + read back + compare")
    for j in jobs:
        say(f"    canary {j.target.name}: device {canary_addr(j.target):#x}")
    if state:
        say(f"  state file: {state.path}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", help="append everything to this file as well")
    default_cal = os.path.expanduser("~/y2-backup/write-calibration.json")
    sub = ap.add_subparsers(dest="cmd", required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--preloader")
    common.add_argument("--log", default=argparse.SUPPRESS)
    w = sub.add_parser("write", parents=[common])
    w.add_argument("--target", required=True)
    w.add_argument("--image", required=True)
    w.add_argument("--expect-current", action="append", required=True, metavar="FILE",
                   help="full-target-size file the device may currently hold; repeatable")
    r = sub.add_parser("restore", parents=[common], help="several targets, one session")
    r.add_argument("--job", action="append", required=True, metavar="TARGET=IMAGE", help="in write order")
    r.add_argument("--expect", action="append", metavar="TARGET=FILE")
    r.add_argument("--force-unknown-current", action="store_true",
                   help="write even if the pre-read matches nothing known (recovery from a failed flash only)")
    for p in (w, r):
        p.add_argument("--calibration", default=default_cal)
        p.add_argument("--dry-run", action="store_true")
        p.add_argument("--state", help="flash-state.json to record attempts in")
    rd = sub.add_parser("read", parents=[common])
    rd.add_argument("--target", required=True)
    rd.add_argument("--out", required=True)
    pr = sub.add_parser("probe", parents=[common], help="measure the write address base (USRDATA only)")
    pr.add_argument("--calibration", default=default_cal)
    pr.add_argument("--remeasure", action="store_true", help="allow overwriting an existing calibration")
    pr.add_argument("--locate", action="store_true", help="find (and zero) a stray probe marker; needs --nonce")
    pr.add_argument("--nonce", help="marker nonce (32 hex digits) from the log / calibration file")
    se = sub.add_parser("state-expect", parents=[common])
    se.add_argument("--state", required=True)
    se.add_argument("--target", required=True)
    se.add_argument("--all", action="store_true")
    args = ap.parse_args(argv)

    def on_signal(signum, frame):
        raise KeyboardInterrupt

    old_handlers = {}
    for sig in (signal.SIGTERM, signal.SIGHUP):
        with contextlib.suppress(ValueError):   # not in the main thread
            old_handlers[sig] = signal.signal(sig, on_signal)
    _WRITES.update(started=0, done=0, canary=None)
    logf = None
    old = sys.stdout, sys.stderr
    if getattr(args, "log", None):
        logf = open(args.log, "a", buffering=1)
        logf.write(f"\n=== y2flash {args.cmd} {time.strftime('%Y-%m-%dT%H:%M:%S%z')} ===\n")
        sys.stdout, sys.stderr = Tee(sys.stdout, logf), Tee(sys.stderr, logf)
    try:
        return _run(args)
    except Y2Error as e:
        say(str(e) if e.exit_code != 1 else f"error: {e}")
        return e.exit_code
    except KeyboardInterrupt:
        if _WRITES["canary"]:
            name, c, raw = _WRITES["canary"]
            say(f"INTERRUPTED DURING THE CANARY for {name}: the 4 KiB at device {c:#x} (raw write addr {raw:#x}) "
                "may still hold the canary marker (or the landing spot, if the base is wrong). Run "
                "scripts/restore-stock.sh --force-unknown-current (re-running the flash will refuse at the "
                "pre-read, which is expected). " + RESTORE_HINT)
        elif _WRITES["started"] > _WRITES["done"]:
            say("INTERRUPTED DURING A WRITE: DEVICE MAY BE PARTIALLY WRITTEN. " + RESTORE_HINT)
        else:
            say("interrupted (no write was in progress).")
        return 130
    finally:
        for sig, h in old_handlers.items():
            signal.signal(sig, h)
        sys.stdout, sys.stderr = old
        if logf:
            logf.close()


def _run(args):
    if args.cmd == "state-expect":
        t = get_target(args.target)
        for p in State(args.state).expected_paths(t.name, args.all):
            say(p)
        return 0
    if args.cmd == "probe":
        keep = os.path.dirname(os.path.abspath(args.log)) if getattr(args, "log", None) else os.getcwd()
        commit = current_commit()
        if not args.locate and not commit:
            raise Y2Error("cannot determine the installed mtkclient commit (.venv/.mtkclient-commit missing): "
                          "the probe result could not be bound to it. Refusing to probe.")
        if args.locate:
            try:
                nonce = bytes.fromhex(args.nonce or "")
            except ValueError:
                nonce = b""
            if len(nonce) != 16:
                raise Y2Error("--locate needs --nonce with 32 hex digits")
            with tempfile.TemporaryDirectory(prefix="y2flash-") as wd, open_session(args.preloader, wd) as da:
                da.write_base = None
                run_locate(da, nonce)
            return 0
        if os.path.exists(args.calibration) and not args.remeasure:
            raise Y2Error(f"{args.calibration} already exists (it may be the record of an unfinished probe: read "
                          "its reason and nonce); pass --remeasure to measure again")

        def persist(doc):
            tmp = args.calibration + ".tmp"
            with open(tmp, "w") as f:
                json.dump(doc, f, indent=1)
            os.replace(tmp, args.calibration)
            say(f"calibration record updated: {args.calibration} (trusted {doc.get('trusted')})")

        with tempfile.TemporaryDirectory(prefix="y2flash-") as wd, open_session(args.preloader, wd) as da:
            da.write_base = None   # probe mode: raw writes confined to USRDATA for every base
            cal = run_probe(da, keep_dir=keep, mtk_commit=commit, persist=persist)
        persist(cal)
        say(f"write_base {cal['write_base']:#x}, read_base {cal['read_base']:#x}")
        if not cal["trusted"]:
            raise ProbeUnexpectedBase("UNEXPECTED WRITE BASE: recorded as NOT trusted; flashing stays refused. "
                                      "Stop and investigate.")
        return 0
    if args.cmd == "read":
        t = get_target(args.target)
        with tempfile.TemporaryDirectory(prefix="y2flash-") as wd, open_session(args.preloader, wd) as da:
            check_device(da)
            say(f"[{t.name}] reading {t.size:#x} bytes at device {t.start:#x}")
            data = da.read(t.start + READ_BASE, t.size)
        tmp = args.out + ".part"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, args.out)
        say(f"[{t.name}] wrote {args.out} sha256 {sha(data)}")
        return 0
    jobs = build_jobs(args)
    state = State(args.state) if args.state else None
    try:
        bases = load_calibration(args.calibration, current_commit(), strict=not args.dry_run)
        cal_err = None
    except Y2Error as e:
        bases, cal_err = None, e
    print_plan(jobs, state, bases)
    if cal_err:
        raise cal_err   # fail closed, also for --dry-run (a real run would refuse)
    if args.dry_run:
        return 0
    with tempfile.TemporaryDirectory(prefix="y2flash-") as wd:
        keep = os.path.dirname(os.path.abspath(args.log)) if getattr(args, "log", None) else os.getcwd()
        with open_session(args.preloader, wd) as da:
            da.write_base = bases.write
            results = run_jobs(da, jobs, bases, keep_dir=keep, state=state)
    for name, img, back, matched in results:
        say(f"OK {name}: image {img} == readback {back} (pre-read matched {matched})")
    say("Done. The device is still in download mode: unplug it before powering on.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

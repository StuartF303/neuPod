#!/usr/bin/env python3
"""Show the MBR/EBR layout of an Innioasis Y2 (MT6582) eMMC dump and carve it into per-partition files.

MTK EBR semantics (derived from the factory MBR/EBR1/EBR2, all LBAs in 512-byte sectors):
  * MBR entry 1 is type 0x05 (extended) with LBA 0x400 = 0x80000 = scatter EBR1; size 0xffffffff.
    MBR entries 2-4 are primary type-0x83 partitions (PROTECT_F, PROTECT_S, SEC_RO), absolute LBA.
  * Within an EBR, entry 1 (type 0x83) is a logical partition whose LBA is relative to THAT EBR's
    own location: EBR1 (0x80000) + 0x28800*512 = 0x5180000 = ANDROID; CACHE and USRDATA likewise;
    EBR2 (0x4700000) + 0x36e400*512 = 0x72380000 = FAT.
  * An EBR's type-0x05 link entry is relative to the START OF THE EXTENDED PARTITION (standard):
    0x80000 + 0x23400*512 = 0x4700000 = EBR2. (EBR1 sits at the extended start so both rules agree.)
  * The last logical partition (FAT) has a bogus size (0xffc6e7ff sectors); it really runs to the
    end of the device. The scatter lists it with size 0.
  * Address base: mtkclient's legacy DA reads the eMMC user area with an offset. On the real dump
    the MBR sits at dump offset 0xb80000, not 0, and everything is shifted likewise:
    dump_offset = scatter physical_start_addr + BASE (BASE = 0xb80000 here; bytes before BASE are
    zero). MBR/EBR LBAs are device-relative (add BASE for dump offsets). BASE is auto-detected by
    scanning the first 64 MiB for a 55AA sector whose MBR/EBR chain reproduces the scatter
    (`--base` overrides). Flash tools (mtkclient `wo`) must use the DA address = physical + BASE.
  * FAT's extent comes from its boot sector (FAT BPB total sectors / exFAT VolumeLength) when
    present, otherwise "to end of dump".
  * Scatter-only partitions (PRO_INFO, NVRAM, SECCFG, UBOOT, ...) are not in any table.
"""
import argparse, hashlib, os, re, shutil, sys, time
from pathlib import Path

SECTOR = 512
BUF = 4 * 1024 * 1024
MIB = 1024 * 1024
FLASHABLE = {"BOOTIMG", "RECOVERY", "USRDATA"}
BACKUP_FILES = ("emmc_user.bin", "emmc_boot1.bin", "emmc_boot2.bin")
BOOT_REGIONS = {"EMMC_BOOT_1": "emmc_boot1.bin", "EMMC_BOOT_2": "emmc_boot2.bin"}
SKIP_ALWAYS = {"BMTPOOL"}  # NAND-only placeholder


class CarveError(Exception):
    pass


# ---------------------------------------------------------------- scatter
def parse_scatter(path):
    """Return list of dicts(name, start, size, region) in file order (partition blocks only)."""
    try:
        text = Path(path).read_text(errors="replace")
    except OSError as e:
        raise CarveError(f"cannot read scatter {path}: {e.strerror}")
    parts, cur = [], None
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if line.startswith("- "):
            cur = None
            line = line[2:].strip()
        m = re.match(r"(\w+):\s*(.*)$", line)
        if not m:
            continue
        k, v = m.group(1), m.group(2).strip()
        if k == "partition_index":
            cur = {}
            parts.append(cur)
        elif cur is not None:
            cur[k] = v
    out = []
    for p in parts:
        try:
            out.append(dict(name=p["partition_name"], start=int(p["physical_start_addr"], 0),
                            size=int(p["partition_size"], 0), region=p["region"]))
        except (KeyError, ValueError):
            raise CarveError(f"scatter: malformed partition block {p.get('partition_name', '?')}")
    if not out:
        raise CarveError("scatter: no partitions found")
    return out


# ---------------------------------------------------------------- MBR / EBR
def parse_entries(sec, what):
    if len(sec) < SECTOR or sec[510:512] != b"\x55\xaa":
        raise CarveError(f"{what}: missing 55AA boot signature")
    ents = []
    for i in range(4):
        e = sec[446 + 16 * i:462 + 16 * i]
        typ = e[4]
        lba = int.from_bytes(e[8:12], "little")
        n = int.from_bytes(e[12:16], "little")
        if typ and n:
            ents.append((typ, lba, n))
    return ents


def read_layout(read_sector, ebrs=None):
    """read_sector(byte_offset)->bytes (512) or the next EBR from `ebrs`. Returns rows
    (label, type, start_bytes, size_bytes, open_ended)."""
    rows = []
    ext = None
    for n, (typ, lba, secs) in enumerate(parse_entries(read_sector(0, 0), "MBR"), 1):
        if typ in (0x05, 0x0f, 0x85):
            ext = lba * SECTOR
            rows.append((f"P{n}", typ, ext, None, True))
        else:
            rows.append((f"P{n}", typ, lba * SECTOR, secs * SECTOR, False))
    if ext is None:
        return rows
    ebr_at, idx, seen = ext, 0, set()
    while True:
        if ebr_at in seen:
            raise CarveError("EBR chain loops")
        seen.add(ebr_at)
        idx += 1
        nxt = None
        for typ, lba, secs in parse_entries(read_sector(ebr_at, idx), f"EBR{idx}"):
            if typ in (0x05, 0x0f, 0x85):
                nxt = ext + lba * SECTOR  # link: relative to extended partition start
                rows.append((f"EBR{idx}->", typ, nxt, None, True))
            else:
                rows.append((f"L{sum(r[0][0] == 'L' for r in rows) + 1}", typ, ebr_at + lba * SECTOR, secs * SECTOR, False))
        if nxt is None:
            return rows
        ebr_at = nxt


def open_reader(src, ebr_files, base=0):
    """Return (read_sector(device-relative offset, ebr index), device size or None, closer)."""
    p = Path(src)
    try:
        f = open(p, "rb")
    except OSError as e:
        raise CarveError(f"cannot open {src}: {e.strerror}")
    size = f.seek(0, 2)
    if size > SECTOR:  # whole dump
        def rd(off, idx):
            f.seek(off + base)
            d = f.read(SECTOR)
            if len(d) < SECTOR:
                raise CarveError(f"dump too short to read table at 0x{off + base:x}")
            return d
        return rd, size - base, f.close
    f.seek(0)
    mbr = f.read()
    f.close()
    blobs = []
    for e in ebr_files:
        try:
            blobs.append(Path(e).read_bytes())
        except OSError as ex:
            raise CarveError(f"cannot read {e}: {ex.strerror}")

    def rd(off, idx):
        if idx == 0:
            return mbr
        if idx > len(blobs):
            raise CarveError(f"EBR{idx} needed but not supplied (use --ebr1/--ebr2)")
        return blobs[idx - 1]
    return rd, None, lambda: None


# ---------------------------------------------------------------- base detection
SCAN = 64 * MIB


def detect_base(path, scatter):
    """Return (ok_bases, loose_bases): offsets of 55AA sectors whose chain parses, and the subset
    that reproduces the scatter exactly."""
    with open(path, "rb") as f:
        data = f.read(SCAN + SECTOR)
    oks, loose = [], []
    for off in range(0, len(data) - SECTOR + 1, SECTOR):
        if data[off + 510:off + 512] != b"\x55\xaa":
            continue
        rd, dev, close = open_reader(path, [], off)
        try:
            rows = read_layout(rd)
        except CarveError:
            continue
        finally:
            close()
        if not rows:
            continue
        loose.append(off)
        res, _ = build_table(rows, scatter, dev)
        if all(r[6] for r in res):
            oks.append(off)
    return oks, loose


def resolve_base(path, scatter, forced):
    """Return (base, found). found=False means no chain matched the scatter (base is the best
    guess for diagnostics, or 0). Raises CarveError if ambiguous."""
    if forced is not None:
        return forced, True
    oks, loose = detect_base(path, scatter)
    if len(oks) > 1:
        raise CarveError("ambiguous base: partition tables match the scatter at "
                         + ", ".join(f"0x{o:x}" for o in oks) + "; use --base")
    if oks:
        return oks[0], True
    return (loose[0] if loose else 0), False


def fat_extent(path, dump_off):
    """(size_bytes, kind) of the FAT/exFAT filesystem at dump_off from its boot sector, or None."""
    try:
        with open(path, "rb") as f:
            f.seek(dump_off)
            b = f.read(SECTOR)
    except OSError:
        return None
    if len(b) < SECTOR or b[510:512] != b"\x55\xaa":
        return None
    if b[3:11] == b"EXFAT   ":
        bps = 1 << b[108]
        return int.from_bytes(b[72:80], "little") * bps, "exFAT"
    kind = "FAT32" if b[0x52:0x5a] == b"FAT32   " else "FAT16" if b[0x36:0x3e] == b"FAT16   " else None
    if not kind:
        return None
    bps = int.from_bytes(b[11:13], "little")
    total = int.from_bytes(b[19:21], "little") or int.from_bytes(b[32:36], "little")
    if bps not in (512, 1024, 2048, 4096) or not total:
        return None
    return total * bps, kind


# ---------------------------------------------------------------- table
def build_table(rows, scatter, dev_size):
    """Return list of printable tuples and a list of (row, scatter_part|None, ok)."""
    user = [s for s in scatter if s["region"] == "EMMC_USER" and s["name"] not in SKIP_ALWAYS]
    by_start = {s["start"]: s for s in user}
    matched, res = set(), []
    for label, typ, start, size, openend in rows:
        s = by_start.get(start)
        if s:
            matched.add(s["name"])
        if s is None:
            ok = False
        elif openend:
            ok = True
        elif s["size"] == 0:  # runs to end of device
            ok = dev_size is None or start + size >= dev_size
        else:
            ok = s["size"] == size
        res.append((label, typ, start, size, openend, s, ok))
    only = [s for s in user if s["name"] not in matched]
    return res, only


def cmd_table(a):
    scatter = parse_scatter(a.scatter)
    whole = Path(a.source).stat().st_size > SECTOR if Path(a.source).exists() else False
    base, found = 0, True
    if whole:
        base, found = resolve_base(a.source, scatter, a.base)
    rd, dev, close = open_reader(a.source, [x for x in (a.ebr1, a.ebr2) if x], base)
    try:
        rows = read_layout(rd)
    finally:
        close()
    res, only = build_table(rows, scatter, dev)
    if whole:
        print(f"base: {base:#x} ({'detected' if a.base is None else 'given'}"
              f"{'' if found else ', does NOT reproduce the scatter'}); dump offset = scatter address + base")
    print(f"{'#':<7}{'type':<6}{'start':>12}  {'size':>12}  {'dump off':>12}  {'scatter':<10}{'start/size'}")
    for label, typ, start, size, openend, s, ok in res:
        if openend:
            sz, note = "(extended)", "start ok" if s else "NO MATCH"
        elif s and s["size"] == 0:
            ext = fat_extent(a.source, start + base) if whole else None
            if ext:
                sz, note = f"{min(ext[0], dev - start):#x}", f"ok ({ext[1]} extent from boot sector)"
            else:
                sz, note = (f"{dev - start:#x}" if dev else "to end"), "ok (to end of device)"
            if not ok:
                note = "SIZE MISMATCH"
        else:
            sz = f"{size:#x}"
            note = "ok" if ok else ("NO MATCH" if not s else f"SIZE MISMATCH (scatter {s['size']:#x})")
        print(f"{label:<7}0x{typ:02x}  {start:>#12x}  {sz:>12}  {start + base:>#12x}  {(s['name'] if s else '-'):<10}{note}")
    for s in only:
        print(f"{'-':<7}{'-':<6}{s['start']:>#12x}  {s['size']:>#12x}  {s['start'] + base:>#12x}  {s['name']:<10}scatter-only")
    return 0 if all(r[6] for r in res) else 2  # 2 = layout disagrees with scatter


def cmd_base(a):
    base, found = resolve_base(a.source, parse_scatter(a.scatter), None)
    if not found:
        raise CarveError("no partition table matching the scatter found in the first 64 MiB")
    print(f"{base:#x}")
    return 0


# ---------------------------------------------------------------- carve
def copy_range(src, off, length, dst):
    h = hashlib.sha256()
    src.seek(off)
    left = length
    while left:
        b = src.read(min(BUF, left))
        if not b:
            raise CarveError("unexpected end of input while copying")
        dst.write(b)
        h.update(b)
        left -= len(b)
    return h.hexdigest()


def parse_sums(path):
    """{name: hexdigest} from a sha256sum-format file (leading '*' on names stripped)."""
    try:
        text = Path(path).read_text()
    except OSError as e:
        raise CarveError(f"cannot read {path}: {e.strerror}")
    out = {}
    for line in text.splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2:
            out[parts[1].lstrip("*").strip()] = parts[0].lower()
    return out


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(BUF)
            if not b:
                return h.hexdigest()
            h.update(b)


def verify_backup(bdir):
    sums = parse_sums(bdir / "SHA256SUMS")
    for name in BACKUP_FILES:
        if name not in sums:
            raise CarveError(f"SHA256SUMS has no entry for {name}")
        print(f"verifying {name} ...", flush=True)
        try:
            got = sha256_file(bdir / name)
        except OSError as e:
            raise CarveError(f"cannot read {bdir / name}: {e.strerror}")
        if got != sums[name]:
            raise CarveError(f"SHA256 mismatch for {name}: expected {sums[name]}, got {got}")


def layout_problems(dump, scatter, base):
    rd, dev, close = open_reader(str(dump), [], base)
    try:
        res, _ = build_table(read_layout(rd), scatter, dev)
    finally:
        close()
    return [f"{label} at 0x{start:x}: " + ("no scatter partition at this start" if not s else
            f"size differs from scatter {s['name']}") for label, _, start, _, _, s, ok in res if not ok]


def safe_name(name):
    return bool(name) and Path(name).name == name and not name.startswith(".") and name not in (".", "..")


def cmd_carve(a):
    bdir = Path(a.backup_dir).expanduser()
    files = {n: bdir / n for n in BACKUP_FILES}
    out = Path(a.out).expanduser() if a.out else bdir / "partitions"
    tmpdir = out.with_name(out.name + ".tmp")
    skip = {s.upper() for s in a.skip}
    scatter = parse_scatter(a.scatter)
    for s in scatter:
        if not safe_name(s["name"]):
            raise CarveError(f"scatter partition name {s['name']!r} is not a plain filename")
    try:
        sizes = {n: p.stat().st_size for n, p in files.items()}
    except OSError as e:
        raise CarveError(f"cannot stat {e.filename}: {e.strerror}")
    usize = sizes["emmc_user.bin"]
    if usize != a.expect_user_size:
        raise CarveError(f"emmc_user.bin is {usize:#x} bytes, expected {a.expect_user_size:#x}")
    for n in ("emmc_boot1.bin", "emmc_boot2.bin"):
        if sizes[n] != a.expect_boot_size:
            raise CarveError(f"{n} is {sizes[n]:#x} bytes, expected {a.expect_boot_size:#x}")
    if a.no_verify:
        print("warning: --no-verify given, SHA256SUMS not checked", file=sys.stderr)
    else:
        verify_backup(bdir)
    base, found = resolve_base(files["emmc_user.bin"], scatter, a.base)
    if not found:
        if not a.force_layout:
            probs = layout_problems(files["emmc_user.bin"], scatter, base)
            raise CarveError("MBR/EBR layout disagrees with scatter (no matching table in the first "
                             "64 MiB; --base N or --force-layout to override): " + "; ".join(probs or ["no MBR found"]))
        print(f"warning: layout not verified, using base {base:#x}", file=sys.stderr)
    print(f"base {base:#x}: dump offset = scatter address + base")
    dev_end = usize - base
    jobs, notes = [], []  # (name, region, src_path, offset, size)
    for s in scatter:
        if s["name"] in SKIP_ALWAYS or s["name"] in skip:
            continue
        if s["region"] == "EMMC_USER":
            size = s["size"]
            if size == 0:
                if s["name"] != "FAT":
                    continue
                size = dev_end - s["start"]
                ext = fat_extent(files["emmc_user.bin"], s["start"] + base)
                if ext and ext[0] < size:
                    notes.append(f"FAT: size 0x{ext[0]:x} taken from the {ext[1]} boot sector "
                                 f"(dump continues to 0x{dev_end:x}).")
                    size = ext[0]
                else:
                    notes.append("FAT: no usable boot-sector size; carved to end of dump.")
            if s["start"] + base + size > usize or size <= 0:
                raise CarveError(f"dump too short: {s['name']} needs 0x{s['start'] + base + max(size, 0):x} "
                                 f"bytes, emmc_user.bin has 0x{usize:x}")
            jobs.append((s["name"], s["region"], files["emmc_user.bin"], s["start"] + base, size, s["start"]))
        elif s["region"] in BOOT_REGIONS:
            fn = BOOT_REGIONS[s["region"]]
            have = sizes[fn]
            if s["start"] >= have:
                raise CarveError(f"{s['name']}: start 0x{s['start']:x} is beyond {fn} (0x{have:x})")
            size = min(s["size"], have - s["start"]) if s["size"] else have - s["start"]
            if size != s["size"]:
                notes.append(f"{s['name']}: scatter size 0x{s['size']:x} clamped to 0x{size:x} "
                             f"(actual {s['region']} is 0x{have:x} on this device).")
            jobs.append((s["name"], s["region"], files[fn], s["start"], size, None))
    if not jobs:
        raise CarveError("nothing to carve")
    names = [j[0] + ".bin" for j in jobs] + ["SHA256SUMS", "table.md"]
    if not a.force:
        for n in names:
            if (out / n).exists():
                raise CarveError(f"{out / n} exists (use --force to overwrite)")
    need = sum(j[4] for j in jobs)
    try:
        out.parent.mkdir(parents=True, exist_ok=True)
        free = shutil.disk_usage(out.parent).free
    except OSError as e:
        raise CarveError(f"cannot prepare {out.parent}: {e.strerror}")
    if free < need + MIB:
        raise CarveError(f"not enough free space in {out.parent}: need {need} bytes, have {free}")
    try:
        if tmpdir.exists():
            shutil.rmtree(tmpdir)
        tmpdir.mkdir()
    except OSError as e:
        raise CarveError(f"cannot create {tmpdir}: {e.strerror}")
    sums, handles = [], {}
    try:
        try:
            for name, region, path, off, size, _phys in jobs:
                if path not in handles:
                    handles[path] = open(path, "rb")
                with open(tmpdir / f"{name}.bin", "wb") as d:
                    digest = copy_range(handles[path], off, size, d)
                sums.append(f"{digest}  {name}.bin")
                print(f"{name}.bin  {size / MIB:.2f} MiB", flush=True)
        finally:
            for h in handles.values():
                h.close()
        (tmpdir / "SHA256SUMS").write_text("\n".join(sums) + "\n")
        md = [f"**DA addr = scatter address + base (0x{base:x}).** mtkclient `wo`/`rf` offsets for the user "
              "area MUST use the DA addr column, not the scatter start; the scatter start is the "
              "device-relative address.", "",
              "| name | region | start | DA addr | size | size MiB | flashable by us |", "|---|---|---|---|---|---|---|"]
        for name, region, _, off, size, phys in jobs:
            start = phys if phys is not None else off
            da = f"0x{off:x}" if phys is not None else "n/a (boot region)"
            md.append(f"| {name} | {region} | 0x{start:x} | {da} | 0x{size:x} | {size / MIB:.2f} | "
                      f"{'yes' if name in FLASHABLE else 'no'} |")
        md.append("")
        md += notes
        if skip:
            md.append(f"Skipped: {', '.join(sorted(skip))}.")
        (tmpdir / "table.md").write_text("\n".join(md) + "\n")
        if out.exists():  # only reachable with --force: keep the old directory, never edit it in place
            old = out.with_name(f"{out.name}.old-{time.strftime('%Y%m%d-%H%M%S')}")
            os.replace(out, old)
            print(f"previous output moved to {old}")
        os.replace(tmpdir, out)
    except KeyboardInterrupt:
        shutil.rmtree(tmpdir, ignore_errors=True)
        raise
    except (OSError, CarveError) as e:
        shutil.rmtree(tmpdir, ignore_errors=True)
        if isinstance(e, CarveError):
            raise
        raise CarveError(f"I/O error: {e}")
    print(f"wrote {len(jobs)} partitions to {out}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("table", help="show MBR/EBR layout vs scatter")
    t.add_argument("source", help="emmc_user.bin or a 512-byte MBR file")
    t.add_argument("--scatter", required=True)
    t.add_argument("--base", type=lambda v: int(v, 0), help="dump offset of device address 0 (default: auto-detect)")
    t.add_argument("--ebr1")
    t.add_argument("--ebr2")
    bs = sub.add_parser("base", help="print the detected dump offset of device address 0")
    bs.add_argument("source")
    bs.add_argument("--scatter", required=True)
    c = sub.add_parser("carve", help="split emmc_user.bin/emmc_boot1.bin into partition files")
    c.add_argument("backup_dir")
    c.add_argument("--scatter", required=True)
    c.add_argument("--out")
    c.add_argument("--skip", action="append", default=[], metavar="NAME")
    c.add_argument("--force", action="store_true")
    c.add_argument("--base", type=lambda v: int(v, 0), help="dump offset of device address 0 (default: auto-detect)")
    c.add_argument("--no-verify", action="store_true", help="do not check SHA256SUMS first")
    c.add_argument("--force-layout", action="store_true", help="carve even if MBR/EBR disagree with scatter")
    c.add_argument("--expect-user-size", type=lambda v: int(v, 0), default=0x1D2000000)
    c.add_argument("--expect-boot-size", type=lambda v: int(v, 0), default=0x400000)
    a = ap.parse_args(argv)
    try:
        return {"table": cmd_table, "base": cmd_base, "carve": cmd_carve}[a.cmd](a)
    except CarveError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("error: interrupted", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

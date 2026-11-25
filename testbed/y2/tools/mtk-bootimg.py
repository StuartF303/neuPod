#!/usr/bin/env python3
"""Unpack/repack Android v0 boot images whose kernel/ramdisk carry MediaTek 512-byte headers.

MTK header: u32 magic 0x58881688, u32 payload size (excl. header), name[32], rest 0xff.
Boot header (v0): magic[8], 8*u32 (ksize, kaddr, rsize, raddr, ssize, saddr, tags, page),
u32 header_version, u32 os_version, name[16], cmdline[512], id[32], extra_cmdline[1024].
id = SHA1(kernel, u32 ksize, ramdisk, u32 rsize, second, u32 ssize) as AOSP mkbootimg
(kernel/ramdisk here include their MTK headers, as stored in the image); zero-padded to 32.
"""
import argparse, hashlib, json, os, struct, sys
from pathlib import Path

MAGIC = b"ANDROID!"
MTK_MAGIC = 0x58881688
MTK_LEN = 512
HDR = struct.Struct("<8s10I16s512s32s1024s")
ADDR_KEYS = ("kernel_addr", "ramdisk_addr", "second_addr", "tags_addr")
OUTPUT_FILES = ("kernel", "kernel.mtkhdr", "ramdisk.cpio.gz", "ramdisk.mtkhdr",
                "second", "second.mtkhdr", "trailer", "bootimg.json")


class BootError(Exception):
    pass


def pad(data, page):
    return data + b"\0" * (-len(data) % page)


def split_mtk(blob, what):
    """Return (header or b'', payload). Detects the MTK header by magic."""
    if len(blob) >= MTK_LEN and struct.unpack_from("<I", blob)[0] == MTK_MAGIC:
        size = struct.unpack_from("<I", blob, 4)[0]
        if size != len(blob) - MTK_LEN:
            raise BootError(f"{what}: MTK size {size} != payload length {len(blob) - MTK_LEN}")
        return blob[:MTK_LEN], blob[MTK_LEN:]
    return b"", blob


def compute_id(kernel, ramdisk, second):
    h = hashlib.sha1()
    for blob in (kernel, ramdisk, second):
        h.update(blob)
        h.update(struct.pack("<I", len(blob)))
    return h.digest().ljust(32, b"\0")


def parse(img):
    if len(img) < HDR.size or img[:8] != MAGIC:
        raise BootError("bad magic: not an ANDROID! boot image")
    (_, ks, ka, rs, ra, ss, sa, tags, page, hv, osv, name, cmd, bid, extra) = HDR.unpack_from(img)
    if page < 512 or page & (page - 1):
        raise BootError(f"page_size {page} is not a power of two >= 512")
    pos, parts = page, {}
    for key, size in (("kernel", ks), ("ramdisk", rs), ("second", ss)):
        if pos + size > len(img):
            raise BootError(f"{key} size {size} runs beyond end of file")
        parts[key] = img[pos:pos + size]
        pos += size + (-size % page)
    cstr = lambda b: b.split(b"\0", 1)[0].decode("latin-1")
    meta = dict(kernel_addr=ka, ramdisk_addr=ra, second_addr=sa, tags_addr=tags, page_size=page,
                header_version=hv, os_version=osv, name=cstr(name), cmdline=cstr(cmd),
                extra_cmdline=cstr(extra), id=bid.hex())
    return meta, parts, img[min(pos, len(img)):]


def text_field(meta, key, size):
    """Encode a NUL-terminated header string; refuse to truncate."""
    try:
        raw = meta[key].encode("latin-1")
    except UnicodeError:
        raise BootError(f"{key} is not latin-1 encodable")
    if len(raw) > size - 1:
        raise BootError(f"{key} is {len(raw)} bytes; max is {size - 1}")
    return raw


def build(meta, kernel, ramdisk, second, trailer):
    page = meta["page_size"]
    name, cmd, extra = (text_field(meta, "name", 16), text_field(meta, "cmdline", 512),
                        text_field(meta, "extra_cmdline", 1024))
    bid = compute_id(kernel, ramdisk, second)
    hdr = HDR.pack(MAGIC, len(kernel), meta["kernel_addr"], len(ramdisk), meta["ramdisk_addr"],
                   len(second), meta["second_addr"], meta["tags_addr"], page,
                   meta["header_version"], meta["os_version"], name, cmd, bid, extra)
    return b"".join(pad(b, page) for b in (hdr, kernel, ramdisk, second)) + trailer


def with_mtk(hdr, payload, what="payload"):
    """Re-attach an MTK header, rewriting only its size field."""
    if not hdr:
        if payload[:4] == struct.pack("<I", MTK_MAGIC):
            raise BootError(f"{what}: payload starts with MTK magic but bootimg.json says no MTK header")
        return payload
    if len(hdr) != MTK_LEN or hdr[:4] != struct.pack("<I", MTK_MAGIC):
        raise BootError(f"{what}.mtkhdr must be exactly {MTK_LEN} bytes starting with the MTK magic")
    old = struct.unpack_from("<I", hdr, 4)[0]
    if old != len(payload):
        print(f"{what}: MTK size {old} -> {len(payload)}", file=sys.stderr)
    return hdr[:4] + struct.pack("<I", len(payload)) + hdr[8:] + payload


def mtk_name(hdr):
    return hdr[8:40].split(b"\0", 1)[0].decode("latin-1")


def cmd_info(a):
    meta, parts, trailer = parse(Path(a.img).read_bytes())
    for k, v in meta.items():
        print(f"{k}: {hex(v) if k in ADDR_KEYS or k == 'os_version' else v}")
    for k, blob in parts.items():
        h, p = split_mtk(blob, k)
        mtk = f"MTK name={mtk_name(h)!r} payload={len(p)}" if h else "no MTK header"
        print(f"{k}: {len(blob)} bytes, {mtk}")
    print(f"trailer: {len(trailer)} bytes")
    comp = compute_id(parts["kernel"], parts["ramdisk"], parts["second"]).hex()
    ok = comp == meta["id"]
    print(f"sha1 stored:   {meta['id']}\nsha1 computed: {comp}\nsha1 {'OK' if ok else 'MISMATCH'}")
    return 0 if ok else 1


def cmd_unpack(a):
    meta, parts, trailer = parse(Path(a.img).read_bytes())
    out = Path(a.outdir)
    if out.exists() and any(out.iterdir()):
        if not a.force:
            raise BootError(f"{out} is not empty; use --force to overwrite")
        for f in OUTPUT_FILES:
            (out / f).unlink(missing_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    if meta["id"] != compute_id(parts["kernel"], parts["ramdisk"], parts["second"]).hex():
        print("warning: stored sha1 id does not match computed id", file=sys.stderr)
    names = {"kernel": "kernel", "ramdisk": "ramdisk.cpio.gz", "second": "second"}
    flags = {}
    for key, blob in parts.items():
        if key == "second" and not blob:
            continue
        h, p = split_mtk(blob, key)
        (out / names[key]).write_bytes(p)
        if h:
            (out / f"{key}.mtkhdr").write_bytes(h)
        flags[key] = bool(h)
    if trailer:
        (out / "trailer").write_bytes(trailer)
    j = dict(meta)
    for k in ADDR_KEYS:
        j[k] = hex(j[k])
    j["os_version"] = hex(j["os_version"])
    j["mtk_header"] = flags
    (out / "bootimg.json").write_text(json.dumps(j, indent=2) + "\n")


def load_meta(d):
    try:
        j = json.loads((d / "bootimg.json").read_text())
        meta = {k: int(j[k], 16) for k in ADDR_KEYS + ("os_version",)}
        meta.update({k: j[k] for k in ("page_size", "header_version", "name", "cmdline", "extra_cmdline")})
        flags = j["mtk_header"]
        if not isinstance(flags, dict) or not all(isinstance(meta[k], str) for k in ("name", "cmdline", "extra_cmdline")):
            raise ValueError("wrong field types")
    except (OSError, KeyError, ValueError, TypeError) as e:
        raise BootError(f"bad or missing bootimg.json: {e!r}")
    for k in ADDR_KEYS + ("os_version",):
        if not 0 <= meta[k] < 2**32:
            raise BootError(f"{k} does not fit in 32 bits")
    if meta["header_version"] != 0:
        raise BootError("header_version must be 0 (only v0 images are supported)")
    ps = meta["page_size"]
    if not isinstance(ps, int) or ps < 512 or ps & (ps - 1) or ps >= 2**32:
        raise BootError("page_size is not a power of two >= 512")
    return meta, flags


def cmd_repack(a):
    d, outp = Path(a.dir), Path(a.out)
    if outp.exists() and not a.force:
        raise BootError(f"{outp} exists; use --force to overwrite")
    meta, flags = load_meta(d)

    def load(key, fname):
        if key not in flags:
            return b""
        hdr = b""
        if flags[key]:
            if not (d / f"{key}.mtkhdr").exists():
                raise BootError(f"bootimg.json says {key} has an MTK header but {key}.mtkhdr is missing")
            hdr = (d / f"{key}.mtkhdr").read_bytes()
        return with_mtk(hdr, (d / fname).read_bytes(), key)
    trailer = (d / "trailer").read_bytes() if (d / "trailer").exists() else b""
    img = build(meta, load("kernel", "kernel"), load("ramdisk", "ramdisk.cpio.gz"),
                load("second", "second"), trailer)
    tmp = outp.with_name(outp.name + ".tmp")
    with open(tmp, "wb") as f:
        f.write(img)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, outp)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sp = p.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("info"); s.add_argument("img"); s.set_defaults(f=cmd_info)
    s = sp.add_parser("unpack"); s.add_argument("img"); s.add_argument("outdir")
    s.add_argument("--force", action="store_true"); s.set_defaults(f=cmd_unpack)
    s = sp.add_parser("repack"); s.add_argument("dir"); s.add_argument("out")
    s.add_argument("--force", action="store_true"); s.set_defaults(f=cmd_repack)
    a = p.parse_args(argv)
    try:
        return a.f(a) or 0
    except (BootError, OSError, struct.error, UnicodeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

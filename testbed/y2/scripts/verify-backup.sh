#!/usr/bin/env bash
# Read-only sanity check of a Y2 eMMC backup. Exits 1 if any check fails.
#
#   testbed/y2/scripts/verify-backup.sh [backup-dir] [--compare <other-backup-dir>] [--fsck]
#     backup-dir  default: newest ~/y2-backup/* that contains SHA256SUMS
#     --compare   a second independent read; each file's real sha256 must equal this backup's
#     --fsck      also extract ANDROID/CACHE/USRDATA to $TMPDIR and run e2fsck -fn on each
#                 (needs ~1 GiB free; CACHE/USRDATA exit 1|4 is only a warning: unreplayed journal after a hard power-off)
#   Env: Y2_SCATTER (scatter file), Y2_FIRMWARE (factory package for the informational comparison)
#
# Only writes to a temp dir under $TMPDIR, which it removes on exit.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TOOLS="$HERE/../tools"
SCATTER="${Y2_SCATTER:-$HERE/../recon/firmware-v3.2.0/MT6582_Android_scatter.txt}"
USER_SIZE=$((0x1d2000000)); BOOT_SIZE=$((0x400000)); REGION=$((0x1000000))
FILES=(emmc_user.bin emmc_boot1.bin emmc_boot2.bin)

DIR=""; OTHER=""; FSCK=0
FW="${Y2_FIRMWARE:-$HOME/y2-firmware/y2_v3.2.0_FM-20260813}"
while (($#)); do
    case $1 in
        --compare) OTHER="${2:?--compare needs a directory}"; shift 2 ;;
        --fsck) FSCK=1; shift ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) DIR="$1"; shift ;;
    esac
done
if [[ -z $DIR ]]; then
    for d in "$HOME"/y2-backup/*/; do
        [[ -f ${d}SHA256SUMS ]] && DIR="${d%/}"
    done
    [[ -n $DIR ]] || { echo "error: no backup dir with SHA256SUMS under $HOME/y2-backup" >&2; exit 1; }
fi
[[ -d $DIR ]] || { echo "error: not a directory: $DIR" >&2; exit 1; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/verify-backup.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT

fails=0
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; [[ -n ${2:-} ]] && sed 's/^/        /' <<<"$2"; fails=$((fails + 1)); }
check() { # check <label> <cmd...> : pass if cmd succeeds
    local label=$1; shift
    local out; out=$("$@" 2>&1) && pass "$label" || fail "$label" "$out"
}
hexat() { dd if="$1" bs=1M skip="$2" count="$3" iflag=skip_bytes,count_bytes status=none | xxd -p | tr -d '\n'; }

echo "backup: $DIR"
U="$DIR/emmc_user.bin"; B1="$DIR/emmc_boot1.bin"; B2="$DIR/emmc_boot2.bin"
SUMS="$DIR/SHA256SUMS"

# 1. SHA256SUMS content and hashes
if [[ -f $SUMS ]]; then
    names=$(awk 'NF>=2 {n=$2; sub(/^\*/,"",n); print n}' "$SUMS" | sort | tr '\n' ' ')
    want=$(printf '%s\n' "${FILES[@]}" | sort | tr '\n' ' ')
    if [[ $names == "$want" ]]; then pass "SHA256SUMS lists exactly the 3 image files"
    else fail "SHA256SUMS lists exactly the 3 image files" "got: $names"; fi
    check "sha256sum -c" bash -c 'cd "$1" && sha256sum -c --quiet SHA256SUMS' _ "$DIR"
else
    fail "SHA256SUMS present" "$SUMS missing"
fi

# 2. sizes
for spec in "emmc_user.bin:$USER_SIZE" "emmc_boot1.bin:$BOOT_SIZE" "emmc_boot2.bin:$BOOT_SIZE"; do
    f=${spec%%:*}; want=${spec##*:}
    got=$(stat -c %s "$DIR/$f" 2>/dev/null || echo missing)
    [[ $got == "$want" ]] && pass "size $f = $want" || fail "size $f" "got $got, want $want"
done

# 3. backup.log
if [[ -f $DIR/backup.log ]]; then
    m=$(grep -nE 'Failed to dump|Error|Traceback|Exception' "$DIR/backup.log" || true)
    [[ -z $m ]] && pass "backup.log has no errors" || fail "backup.log has no errors" "$m"
else
    fail "backup.log present" "$DIR/backup.log missing"
fi

# 3b. address base: dump offset of device address 0 (mtkclient's legacy DA shifts the whole user area)
BASE=0
uat() { hexat "$U" $((BASE + $1)) "$2"; }  # bytes at DEVICE offset $1 (dump offset = BASE + $1)
if [[ -f $U ]]; then
    if [[ ! -f $SCATTER ]]; then
        fail "address base detected" "scatter file not found: $SCATTER (set Y2_SCATTER)"
    elif out=$(python3 -I "$TOOLS/carve-partitions.py" base "$U" --scatter "$SCATTER" 2>&1); then
        BASE=$((out))
        pass "address base detected: $out (dump offset = scatter address + base)"
        if ((BASE > 0)); then
            if cmp -s -n "$BASE" "$U" /dev/zero; then echo "INFO  dump[0:$out] is all-zero"
            else echo "INFO  dump[0:$out] is NOT all-zero"; fi
        fi
    else
        fail "address base detected" "$out"
    fi
fi

if [[ -f $U && -f $B1 && -f $B2 ]]; then
    # 4. MBR / EBRs (device offsets; the table check below re-detects the base on its own)
    [[ $(uat 510 2) == 55aa ]] && pass "MBR signature 55aa" || fail "MBR signature 55aa"
    [[ $(uat $((0x80000 + 510)) 2) == 55aa ]] && pass "EBR1 (0x80000) signature" || fail "EBR1 (0x80000) signature"
    [[ $(uat $((0x4700000 + 510)) 2) == 55aa ]] && pass "EBR2 (0x4700000) signature" || fail "EBR2 (0x4700000) signature"
    if [[ ! -f $SCATTER ]]; then
        fail "partition table matches scatter" "scatter file not found: $SCATTER
set Y2_SCATTER to a MT6582_Android_scatter.txt, or fetch it from the NAS notes
(<second-root>/notes/recon/firmware-v3.2.0/, mirrored there by sync-notes.sh)"
    else
        out=$(python3 -I "$TOOLS/carve-partitions.py" table "$U" --scatter "$SCATTER" 2>&1); rc=$?
        if ((rc == 0)); then pass "partition table matches scatter"
        else fail "partition table matches scatter (exit $rc)" "$out"; fi
    fi

    # 4b. filesystems
    for spec in ANDROID:$((0x5180000)) CACHE:$((0x38580000)) USRDATA:$((0x40380000)); do
        n=${spec%%:*}; off=${spec##*:}
        [[ $(uat $((off + 0x438)) 2) == 53ef ]] && pass "$n: ext4 superblock magic 0xEF53" \
            || fail "$n: ext4 superblock magic 0xEF53"
    done
    fat=$((0x72380000))
    if [[ $(uat $((fat + 510)) 2) == 55aa ]]; then
        kind=unknown
        [[ $(uat $((fat + 0x52)) 8) == 4641543332202020 ]] && kind=FAT32
        [[ $(uat $((fat + 0x36)) 8) == 4641543136202020 ]] && kind=FAT16
        [[ $(uat $((fat + 3)) 8) == 4558464154202020 ]] && kind=exFAT
        pass "FAT: boot sector 55aa, type $kind"
    else
        fail "FAT: boot sector 55aa"
    fi

    # 5. boot partitions
    zero() { cmp -s -n "$BOOT_SIZE" "$1" /dev/zero; }
    same4() { cmp -s -n "$BOOT_SIZE" "$1" "$U"; }  # equals the first 4 MiB of the dump
    zero "$B1" && fail "boot1 not all-zero" || pass "boot1 not all-zero"
    same4 "$B1" && fail "boot1 differs from start of user area" || pass "boot1 differs from start of user area"
    if same4 "$B2"; then
        if zero "$B2"; then echo "WARN  boot2 read unverifiable (blank or switch failed): all-zero and equal to dump[0:4 MiB]"
        else fail "boot2 differs from start of user area"; fi
    else pass "boot2 differs from start of user area"; fi
    if [[ $(hexat "$B1" 0 4) == 4d4d4d01 ]]; then pass "boot1 starts with preloader magic MMM\\x01 at offset 0"
    elif [[ $(hexat "$B1" $((0x800)) 4) == 4d4d4d01 ]]; then pass "boot1 has preloader magic MMM\\x01 at offset 0x800"
    else fail "boot1 preloader magic MMM\\x01 at offset 0 or 0x800"; fi
    if LC_ALL=C grep -aq 'MTK_BLOADER_INFO' "$B1"; then pass "boot1 has MTK_BLOADER_INFO"; else fail "boot1 has MTK_BLOADER_INFO"; fi
    if cmp -s "$B1" "$B2" && ! zero "$B1"; then fail "boot1 differs from boot2"; else pass "boot1 differs from boot2 (or both empty)"; fi

    # 6. boot / recovery images
    for spec in BOOTIMG:$((0x1d80000)) RECOVERY:$((0x2d80000)); do
        n=${spec%%:*}; off=${spec##*:}
        img="$TMP/$n.img"
        dd if="$U" of="$img" bs=1M skip="$((BASE + off))" count="$REGION" iflag=skip_bytes,count_bytes status=none
        if [[ $(hexat "$img" 0 8) == 414e44524f494421 ]]; then
            out=$(python3 -I "$TOOLS/mtk-bootimg.py" info "$img" 2>&1); rc=$?
            ((rc == 0)) && grep -q '^sha1 OK' <<<"$out" && pass "$n: ANDROID! header, image sha1 OK" \
                || fail "$n image sha1" "$out"
        else
            fail "$n starts with ANDROID!"
        fi
        rm -f "$img"
    done
fi

# 6b/6c. blank-region check (hard) and factory comparison (informational)
if [[ -f $U ]]; then
    PY='
import sys
dump, fw, BASE = sys.argv[1], sys.argv[2], int(sys.argv[3])
CH = 1 << 20
def chunks(f, off, n):
    f.seek(BASE + off)
    while n:
        b = f.read(min(CH, n))
        if not b:
            return
        n -= len(b)
        yield b
def uniform(f, off, n):
    """0x00 / 0xff if the whole range is that byte, else None."""
    for v in (0, 0xff):
        if all(c.count(v) == len(c) for c in chunks(f, off, n)):
            return v
    return None
regions = {"BOOTIMG": (0x1d80000, 0x1000000), "RECOVERY": (0x2d80000, 0x1000000),
           "UBOOT": (0x1d20000, 0x60000), "LOGO": (0x4400000, 0x300000), "SEC_RO": (0x3d80000, 0x600000)}
cmp = [("BOOTIMG", "boot.img"), ("RECOVERY", "recovery.img"), ("UBOOT", "lk.bin"), ("LOGO", "logo.bin"),
       ("SEC_RO", "secro.img")]
with open(dump, "rb") as f:
    for n in ("BOOTIMG", "RECOVERY", "UBOOT", "LOGO"):
        off, size = regions[n]
        u = uniform(f, off, size)
        print(("FAIL" if u is not None else "PASS"), f"{n} region is not blank" + (f" (entirely 0x{u:02x})" if u is not None else ""))
    for n, fn in cmp:
        off, size = regions[n]
        try:
            fac = open(f"{fw}/{fn}", "rb").read()
        except OSError:
            print(f"INFO {n}: factory file {fn} not found in {fw}")
            continue
        diff, first, pos = 0, None, 0
        for c in chunks(f, off, len(fac)):
            seg = fac[pos:pos + len(c)]
            if c != seg:
                for i, (a, b) in enumerate(zip(c, seg)):
                    if a != b:
                        diff += 1
                        if first is None:
                            first = pos + i
            pos += len(c)
        res = "IDENTICAL" if not diff else f"DIFFERS ({diff} bytes differ, first at 0x{first:x})"
        u = uniform(f, off + len(fac), size - len(fac)) if len(fac) < size else 0
        tail = "n/a" if len(fac) >= size else "all-zero" if u == 0 else "all-0xFF" if u == 0xff else "not blank"
        extra = ""
        if n in ("BOOTIMG", "RECOVERY"):
            f.seek(BASE + off + 0x240)
            extra = f"; sha1 id dumped {f.read(20).hex()} factory {fac[0x240:0x254].hex()}"
        print(f"INFO {n} vs {fn} ({len(fac)} bytes): {res}; remainder {tail}{extra}")
'
    while IFS= read -r line; do
        case $line in
            PASS*) pass "${line#PASS }" ;;
            FAIL*) fail "${line#FAIL }" ;;
            *) echo "$line" ;;
        esac
    done < <(python3 -I -c "$PY" "$U" "$FW" "$BASE" 2>&1)
fi

# 6d. optional e2fsck
if ((FSCK)) && [[ -f $U ]]; then
    if ! command -v e2fsck >/dev/null; then fail "e2fsck available"; else
    for spec in ANDROID:$((0x5180000)):$((0x33400000)) CACHE:$((0x38580000)):$((0x7e00000)) USRDATA:$((0x40380000)):$((0x32000000)); do
        IFS=: read -r n off len <<<"$spec"
        free=$(df --output=avail -B1 "$TMP" | tail -1)
        if ((free < len + 64*1024*1024)); then fail "$n: e2fsck (not enough free space in $TMP: need $len, have $free)"; continue; fi
        img="$TMP/$n.fs"
        dd if="$U" of="$img" bs=1M skip="$((BASE + off))" count="$len" iflag=skip_bytes,count_bytes status=none
        out=$(e2fsck -fn "$img" 2>&1); rc=$?
        rm -f "$img"
        errs=$(grep -E 'Fix\? no' <<<"$out" | sed "s|$img|$n|")
        summary=$(printf '%s\n%s\n' "$(head -n 12 <<<"$errs")" "$(tail -n 1 <<<"$out" | sed "s|$img|$n|")")
        # Android's make_ext4fs leaves bitmap padding unset; harmless, so alone it is only a warning
        if ((rc == 4)) && [[ -n $errs ]] && ! grep -qvE 'Padding at end of (inode|block) bitmap is not set' <<<"$errs"; then
            echo "WARN  $n: e2fsck -fn only reports unset bitmap padding (cosmetic, typical of Android images)"
            sed 's/^/        /' <<<"$summary"
        elif ((rc == 0)); then pass "$n: e2fsck -fn clean"
        elif [[ $n == USRDATA || $n == CACHE ]] && ((rc == 1 || rc == 4)); then
            echo "WARN  $n: e2fsck -fn exit $rc (user data may have been dirty when dumped)"; sed 's/^/        /' <<<"$summary"
        else fail "$n: e2fsck -fn exit $rc" "$summary"; fi
    done
    fi
fi

# 7. optional second read
if [[ -n $OTHER ]]; then
    for f in "${FILES[@]}"; do
        a=$(awk -v f="$f" '{n=$2; sub(/^\*/,"",n)} n==f {print $1; exit}' "$SUMS" 2>/dev/null)
        b=$(sha256sum "$OTHER/$f" 2>/dev/null | cut -d' ' -f1)
        if [[ -n $a && $a == "$b" ]]; then pass "second read identical: $f"
        else fail "second read identical: $f" "ours ${a:-?} / other ${b:-unreadable}"; fi
    done
fi

echo
if ((fails)); then echo "RESULT: $fails check(s) FAILED for $DIR"; exit 1; fi
echo "RESULT: all checks passed for $DIR"

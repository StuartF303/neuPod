#!/usr/bin/env bash
# Full read-only backup of the Y2 eMMC over BROM/preloader (mtkclient legacy DA).
#
#   testbed/y2/scripts/backup-all.sh [--dry-run] [backup-dir]            default ~/y2-backup/<YYYY-MM-DD>
#   testbed/y2/scripts/backup-all.sh [--dry-run] --extras [backup-dir]   default newest ~/y2-backup/* with SHA256SUMS
#
# Main mode reads the whole user area plus boot1/boot2 (PRELOADER lives in EMMC_BOOT_1) in ONE
# DA session. The Y2 uses MBR/EBR, not GPT, so mtkclient's rl / r <name> cannot be used;
# partitions are carved afterwards from emmc_user.bin.
#
# Staging: everything is written to <dir>/.incoming/ first. Only after the session log is clean,
# the sizes are right and verify-backup.sh passes on the staging dir are the files moved to their
# final names (SHA256SUMS last, so its presence means "complete and verified"). Otherwise the
# staging dir is renamed <dir>/.failed-<timestamp> and the final names are never created.
# (Chosen over "move first, then verify, then move aside on failure": a failed read never
# occupies the final names, so a re-run never sees a plausible-looking partial set.)
#
# --extras: ONE extra session, read-only, never touches the main set. Writes <dir>/extras/:
#   emmc_boot2.reread.bin   boot2 again (the main boot2 read came back blank)
#   emmc_user.tail.bin      the last 0xb80000 bytes of the device. The legacy DA's address space is
#                           shifted by 0xb80000, so `rf` (clamped to the eMMC size) stops short; the
#                           tail is requested as `ro <eMMC size> 0xb80000`. mtkclient clamps only the
#                           LENGTH (min(length, size)), not the address, so this is allowed; whether the
#                           DA honours an address past the end is untested. Boot2 is read first so a
#                           failing tail read cannot cost it.
#
# Idempotent: if the dir already holds a complete set that passes verify-backup.sh it does nothing.
# Refuses to overwrite a partial set (move it aside yourself).
# Reads only: every mtkclient command below is built in one array and asserted to be rf/ro.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
Y2="$(cd "$HERE/.." && pwd)"
MTK="$Y2/.venv/bin/mtk"
VERIFY="$HERE/verify-backup.sh"
FW="${Y2_FIRMWARE:-$HOME/y2-firmware/y2_v3.2.0_FM-20260813}"
PRELOADER="$FW/preloader_eastaeon82_wet_kk.bin"   # EMI/DRAM config only; never written
FILES=(emmc_user.bin emmc_boot1.bin emmc_boot2.bin)
EXPECT_USER=$((0x1d2000000))   # m_emmc_ua_size reported by DA, 2026-11-10
EXPECT_BOOT=$((0x400000))
EXPECT_TAIL=$((0xb80000))
die() { echo "error: $*" >&2; exit 1; }

DRY=0; EXTRAS=0; ARG=""
for a in "$@"; do
    case $a in
        --dry-run) DRY=1 ;;
        --extras) EXTRAS=1 ;;
        -h|--help) sed -n '2,6p' "$0"; exit 0 ;;
        -*) die "unknown option $a" ;;
        *) [[ -z $ARG ]] || die "only one backup dir allowed"; ARG="$a" ;;
    esac
done
if ((EXTRAS)) && [[ -z $ARG ]]; then
    for d in "$HOME"/y2-backup/*/; do [[ -f ${d}SHA256SUMS ]] && ARG="${d%/}"; done
    [[ -n $ARG ]] || die "no backup dir with SHA256SUMS under $HOME/y2-backup"
fi
DEST="${ARG:-$HOME/y2-backup/$(date +%F)}"
DEST="${DEST%/}"
[[ $DEST == /* ]] || DEST="$PWD/$DEST"   # mtk runs from a scratch directory (below), so paths must be absolute
# mtkclient's `multi` splits the command string on ';' and ' ' with no quoting
[[ $DEST != *[[:space:]\;]* ]] || die "backup dir must not contain whitespace or ';' (mtkclient multi splits on them): $DEST"

if ((EXTRAS)); then
    STAGE="$DEST/extras/.incoming"; FINAL="$DEST/extras"; LOG="$STAGE/extras.log"
    CMDS=("rf $STAGE/emmc_boot2.reread.bin --parttype boot2"
          "ro $(printf 0x%x "$EXPECT_USER") $(printf 0x%x "$EXPECT_TAIL") $STAGE/emmc_user.tail.bin")
else
    STAGE="$DEST/.incoming"; FINAL="$DEST"; LOG="$STAGE/backup.log"
    CMDS=("rf $STAGE/emmc_user.bin"
          "rf $STAGE/emmc_boot1.bin --parttype boot1"
          "rf $STAGE/emmc_boot2.bin --parttype boot2")
fi
# Read-only guard: nothing but rf / ro may ever reach mtkclient.
for c in "${CMDS[@]}"; do
    [[ $c == "rf "* || $c == "ro "* ]] || die "refusing non-read mtkclient command: $c"
done
MULTI=$(IFS=';'; echo "${CMDS[*]}")

if ((DRY)); then
    echo "dry run: nothing executed"
    echo "mode:    $( ((EXTRAS)) && echo extras || echo main)"
    echo "staging: $STAGE  (final: $FINAL)"
    echo "command: $MTK --preloader $PRELOADER multi \"$MULTI\""
    exit 0
fi

[[ -x $MTK ]] || die "mtkclient missing: run tools/setup-host.sh"
[[ -f $PRELOADER ]] || die "factory preloader not found: $PRELOADER"
command -v lsusb >/dev/null || die "lsusb not found (install usbutils); it is needed to detect unplug/replug"

# ---- extras mode helpers
sha() { sha256sum "$1" | cut -d' ' -f1; }
allzero() { cmp -s -n "$(stat -c %s "$1")" "$1" /dev/zero; }

arm_and_run() {  # arm_and_run : waits for unplug, runs the session into $LOG, sets MTK_RC
    echo "Waiting for the Y2 to leave the bus (unplug it)..."
    while lsusb -d 0e8d: >/dev/null 2>&1; do sleep 0.5; done
    echo "Armed. Power the Y2 off, hold Vol-, plug in USB."
    # run mtk from a fresh scratch dir: a stale ./.state (here or in the caller's cwd) would make mtkclient
    # skip the handshake and assume a DA is already loaded
    local rundir; rundir=$(mktemp -d "${TMPDIR:-/tmp}/y2-mtk.XXXXXX")
    set +e
    {
        cd "$rundir" || exit 1
        echo "session start $(date -Is) mtkclient $(cat "$Y2/.venv/.mtkclient-commit" 2>/dev/null)"
        echo "commands: $MULTI"
        "$MTK" --preloader "$PRELOADER" multi "$MULTI"
        echo "mtk exit=$?"
        echo "session end $(date -Is)"
    } 2>&1 | tee "$LOG"
    set -e
    rm -rf "$rundir"
    MTK_RC=$(grep -o 'mtk exit=[0-9]*' "$LOG" | tail -1 | cut -d= -f2)
    MTK_RC=${MTK_RC:-unknown}
}

log_clean() {  # log_clean : fail on error strings in the session log
    local m; m=$(grep -nE 'Failed to dump|Error|Traceback|Exception' "$LOG" || true)
    [[ -z $m ]] || { echo "errors in session log:" >&2; echo "$m" >&2; return 1; }
}

fail_stage() {
    local to="$DEST/.failed-$(date +%Y%m%d-%H%M%S)"
    ((EXTRAS)) && to="$DEST/extras.failed-$(date +%Y%m%d-%H%M%S)"
    mv "$STAGE" "$to"
    echo "FAILED: $1" >&2
    echo "staged files kept in $to; the final names were not created" >&2
    exit 1
}

size_is() { [[ $(stat -c %s "$1" 2>/dev/null || echo 0) -eq $2 ]]; }

# ================================================================== extras
if ((EXTRAS)); then
    [[ -f $DEST/SHA256SUMS ]] || die "$DEST has no SHA256SUMS: --extras needs the main backup to exist"
    [[ ! -e $STAGE ]] || die "$STAGE exists from an earlier run; move it aside"
    [[ ! -e $FINAL/SHA256SUMS ]] || die "$FINAL/SHA256SUMS exists: extras already taken"
    for f in emmc_boot2.reread.bin emmc_user.tail.bin; do
        [[ ! -e $FINAL/$f ]] || die "partial extras present ($FINAL/$f); move $FINAL aside"
    done
    mkdir -p "$STAGE"
    arm_and_run
    got=()
    [[ $MTK_RC == 0 ]] || echo "warning: mtk exit=$MTK_RC" >&2
    log_clean || echo "warning: log contains errors; sizes decide what is kept" >&2
    size_is "$STAGE/emmc_boot2.reread.bin" "$EXPECT_BOOT" && got+=(emmc_boot2.reread.bin) \
        || echo "boot2 re-read missing or wrong size" >&2
    size_is "$STAGE/emmc_user.tail.bin" "$EXPECT_TAIL" && got+=(emmc_user.tail.bin) \
        || echo "user tail missing or wrong size (DA may have refused an address past the end)" >&2
    ((${#got[@]})) || fail_stage "no extra file was read correctly"
    for f in "${got[@]}"; do mv "$STAGE/$f" "$FINAL/$f"; done
    mv "$LOG" "$FINAL/extras.log"
    (cd "$FINAL" && sha256sum "${got[@]}" > SHA256SUMS.tmp && mv SHA256SUMS.tmp SHA256SUMS)
    rmdir "$STAGE" 2>/dev/null || true
    cat "$FINAL/SHA256SUMS"
    echo
    echo "--- for you to judge (extras only; the main set is untouched)"
    B2=$DEST/emmc_boot2.bin; R=$FINAL/emmc_boot2.reread.bin
    echo "main boot2:   $(sha "$B2")  $(allzero "$B2" && echo ALL-ZERO || echo has-data)"
    if [[ -f $R ]]; then
        echo "boot2 reread: $(sha "$R")  $(allzero "$R" && echo ALL-ZERO || echo has-data)  $([[ $(sha "$R") == "$(sha "$B2")" ]] && echo identical-to-main || echo DIFFERS-from-main)"
    fi
    T=$FINAL/emmc_user.tail.bin
    [[ -f $T ]] && echo "user tail:    $(sha "$T")  $(allzero "$T" && echo ALL-ZERO || echo has-data)"
    ((${#got[@]} == 2)) || { echo "only part of the extras were read (see warnings above)" >&2; exit 1; }
    exit 0
fi

# ================================================================== main set
if [[ -f $DEST/SHA256SUMS ]]; then
    echo "existing backup in $DEST: verifying"
    "$VERIFY" "$DEST" && { echo "backup complete and verified; nothing to do"; exit 0; }
    echo "SHA256SUMS present but verification FAILED: investigate, do not overwrite" >&2; exit 1
fi
for f in "${FILES[@]}"; do
    [[ ! -e $DEST/$f ]] || die "partial backup present ($DEST/$f); move $DEST aside and re-run"
done
[[ ! -e $STAGE ]] || die "$STAGE exists from an earlier run; move it aside"
mkdir -p "$STAGE"

arm_and_run
[[ $MTK_RC == 0 ]] || fail_stage "mtk exit=$MTK_RC"
log_clean || fail_stage "session log reports errors"
for spec in "emmc_user.bin:$EXPECT_USER" "emmc_boot1.bin:$EXPECT_BOOT" "emmc_boot2.bin:$EXPECT_BOOT"; do
    f=${spec%%:*}; want=${spec##*:}
    got=$(stat -c %s "$STAGE/$f" 2>/dev/null || echo 0)
    [[ $got -eq $want ]] && echo "size ok: $f $got" || fail_stage "size mismatch: $f got $got want $want"
done
(cd "$STAGE" && sha256sum "${FILES[@]}" > SHA256SUMS)
"$VERIFY" "$STAGE" || fail_stage "verify-backup.sh failed on the staged set"

for f in "${FILES[@]}" backup.log; do mv "$STAGE/$f" "$DEST/$f"; done
mv "$STAGE/SHA256SUMS" "$DEST/SHA256SUMS"   # last: its presence marks a complete, verified set
rmdir "$STAGE"
cat "$DEST/SHA256SUMS"
echo "done: $DEST (copy to the NAS next, then verify there)"

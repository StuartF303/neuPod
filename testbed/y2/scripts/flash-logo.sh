#!/usr/bin/env bash
# Flash the LOGO partition of the Y2: the pictures stock LK shows at power-on (guarded; see lib-flash.sh and tools/y2flash.py).
# LOGO may be written since 2026-11-11 (Stuart). It holds logo.bin: the innioasis power-on logo (image 0), the kernel
# boot logo (image 38) and the charging animation. Build the image with scripts/build-logo.sh. Undo: restore-stock.sh --target LOGO.
#
#   testbed/y2/scripts/flash-logo.sh [--yes] [--dry-run] [IMAGE]
#     IMAGE    default: ~/y2-build/y2-logo.bin (refused if absent; there is deliberately no no-op default)
#     --dry-run  runs the image and backup guards and prints the plan; never connects, never prompts
#     --yes      skip typing LOGO
#
# Environment overrides (Y2_BACKUP etc.) are ignored unless --dry-run.
# Needs ~/y2-backup/write-calibration.json from probe-write.sh (refuses without it).
# Order: image validation (y2logo.py info, strict) -> backup guards -> banner + typed confirmation -> wait for unplug ->
#   ONE DA session: pre-read all 3 MiB, compare with {backup LOGO, last flashed result from flash-state.json},
#   a 4 KiB canary (proves the write address base for LOGO), write only if equal, read back, compare.
#   Every attempt is appended to docs/y2-log.md and ~/y2-backup/flash-state.json (recorded BEFORE the write).
# If it ends with "READBACK MISMATCH" or a failed write, run restore-stock.sh --target LOGO.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib-flash.sh"

ASSUME_YES=0; DRY=0; IMAGE=""
for a in "$@"; do
    case $a in
        --yes) ASSUME_YES=1 ;;
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        -*) die "unknown option $a" ;;
        *) [[ -z $IMAGE ]] || die "only one image allowed"; IMAGE="$a" ;;
    esac
done
env_overrides "$DRY"
IMAGE="${IMAGE:-$HOME/y2-build/y2-logo.bin}"
[[ -f $IMAGE ]] || die "image not found: $IMAGE (build it with scripts/build-logo.sh, or pass IMAGE explicitly)"
IMAGE="$(cd "$(dirname "$IMAGE")" && pwd)/$(basename "$IMAGE")"
[[ $(stat -c %s "$IMAGE") -gt 0 ]] || die "image is empty: $IMAGE"
[[ $(stat -c %s "$IMAGE") -le $LOGO_SIZE ]] || die "image larger than LOGO (3 MiB)"
# strict container validation BEFORE the banner (bad header, counts, offsets, zlib, image sizes all refuse)
LOGOINFO=$(python3 -I "$Y2/tools/y2logo.py" info "$IMAGE" 2>&1) || { echo "$LOGOINFO" >&2; die "y2logo.py info failed for $IMAGE: refusing"; }
grep -q '^valid logo.bin: ' <<<"$LOGOINFO" || { echo "$LOGOINFO" >&2; die "$IMAGE: y2logo.py info did not report a valid logo.bin: refusing"; }

guard_backup
logo_backup_guard

EXPECT=("--expect-current" "$BACKUP/partitions/LOGO.bin")
while IFS= read -r p; do [[ -n $p ]] && EXPECT+=("--expect-current" "$p"); done \
    < <("$PY" -I "$Y2FLASH" state-expect --state "$STATE" --target LOGO)
IMG_SHA=$(sha256sum "$IMAGE" | cut -d' ' -f1)
LOG="$LOGDIR/flash-logo-$(date +%Y%m%d-%H%M%S).log"
CMD=("$Y2FLASH" write --target LOGO --image "$IMAGE" "${EXPECT[@]}" --state "$STATE" --calibration "$CAL")
INFOSHORT=$(sed -n '1p;2p;39p;40p' <<<"$LOGOINFO")   # summary line, image 0, images 37 and 38

if ((DRY)); then
    echo
    echo "image check: strict logo container (magic, name, counts, offsets, zlib, image sizes); y2logo.py info:"
    sed 's/^/  /' <<<"$INFOSHORT"
    # plan only: y2flash --dry-run never imports mtkclient, so system python is fine
    python3 -I "${CMD[@]}" --dry-run
    exit 0
fi

CALTXT=$(cal_summary)
[[ -x $PY ]] || die "mtkclient venv missing: run tools/setup-host.sh"
[[ -f $PRELOADER ]] || die "factory preloader not found: $PRELOADER"
mkdir -p "$LOGDIR"
echo
echo "=================== FLASH BANNER ==================="
echo " target        LOGO   (device/scatter 0x4400000, size 0x300000; nothing else is writable here)"
echo
echo " After this, stock LK shows THIS picture at power-on (image 0) and the kernel boot logo (image 38)."
echo " Undo: scripts/restore-stock.sh --target LOGO (device in Vol- download mode)."
echo
echo " addressing    $CALTXT"
echo "               raw read addr 0x4f80000; raw write addr = 0x4400000 + write base"
echo " image         $IMAGE"
echo " image sha256  $IMG_SHA"
echo " image check   strict logo container; y2logo.py info:"
sed 's/^/                 /' <<<"$INFOSHORT"
echo " device must currently hold one of:"
for ((i = 1; i < ${#EXPECT[@]}; i += 2)); do echo "   $(sha256sum "${EXPECT[i]}" | cut -d' ' -f1)  ${EXPECT[i]}"; done
echo " canary limits: it only catches a base error of exactly +-0xb80000; any other mapping error is outside the"
echo " model, and mid-partition non-linearity is only caught by the readback. Saved window files hold private data."
echo " before the image: a 4 KiB canary write at device 0x45ff000 (inside LOGO, 2 MiB in; restored at once) proves the"
echo " write address base for LOGO; otherwise nothing is written. Log: $LOG"
echo "====================================================="
confirm LOGO

arm_wait
set +e
"$PY" -I "${CMD[@]}" --log "$LOG" --preloader "$PRELOADER"
RC=$?
set -e
case $RC in
    0) RES="OK: pre-read matched, readback identical" ;;
    3) RES="REFUSED: pre-read mismatch, nothing written" ;;
    2) RES="FAILED: device may be modified, run restore-stock.sh --target LOGO (--force-unknown-current if it refuses)" ;;
    6) RES="CANARY FAILED: the write address base does not hold for LOGO; no full write was done; see $LOG" ;;
    *) RES="ERROR (exit $RC), see $LOG" ;;
esac
partial_warning "$LOG" "$RC" LOGO
log_row "flash LOGO, image sha256 \`$IMG_SHA\` ($(basename "$IMAGE"))" "$RES"
echo "$RES"
exit "$RC"

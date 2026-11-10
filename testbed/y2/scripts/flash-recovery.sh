#!/usr/bin/env bash
# Flash the RECOVERY partition of the Y2 (guarded; see lib-flash.sh and tools/y2flash.py).
#
#   testbed/y2/scripts/flash-recovery.sh [--yes] [--dry-run] [IMAGE]
#     IMAGE    default: the backup's own RECOVERY.bin (the G0 no-op test: writes back what is already there)
#     --dry-run  runs the backup guards and prints the plan; never connects, never prompts
#     --yes      skip typing RECOVERY
#
# Environment overrides (Y2_BACKUP etc.) are ignored unless --dry-run.
# Needs ~/y2-backup/write-calibration.json from probe-write.sh (refuses without it).
# Order: backup guards -> banner + typed confirmation -> wait for unplug -> ONE DA session:
#   pre-read all 16 MiB, compare with {backup RECOVERY, last flashed result from flash-state.json},
#   write only if equal, read back, compare. Every attempt is appended to docs/y2-log.md and
#   ~/y2-backup/flash-state.json (attempts are recorded BEFORE the write, so a crash is still accounted for).
# If it ends with "READBACK MISMATCH" or a failed write, run restore-stock.sh.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib-flash.sh"

ASSUME_YES=0; DRY=0; IMAGE=""
for a in "$@"; do
    case $a in
        --yes) ASSUME_YES=1 ;;
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        -*) die "unknown option $a" ;;
        *) [[ -z $IMAGE ]] || die "only one image allowed"; IMAGE="$a" ;;
    esac
done
env_overrides "$DRY"
IMAGE="${IMAGE:-$BACKUP/partitions/RECOVERY.bin}"
[[ -f $IMAGE ]] || die "image not found: $IMAGE"
IMAGE="$(cd "$(dirname "$IMAGE")" && pwd)/$(basename "$IMAGE")"
[[ $(stat -c %s "$IMAGE") -le $PART_SIZE ]] || die "image larger than RECOVERY (16 MiB)"

guard_backup

EXPECT=("--expect-current" "$BACKUP/partitions/RECOVERY.bin")
while IFS= read -r p; do [[ -n $p ]] && EXPECT+=("--expect-current" "$p"); done \
    < <("$PY" -I "$Y2FLASH" state-expect --state "$STATE" --target RECOVERY)
IMG_SHA=$(sha256sum "$IMAGE" | cut -d' ' -f1)
LOG="$LOGDIR/flash-recovery-$(date +%Y%m%d-%H%M%S).log"
CMD=("$Y2FLASH" write --target RECOVERY --image "$IMAGE" "${EXPECT[@]}" --state "$STATE" --calibration "$CAL")

if ((DRY)); then
    echo
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
echo " target        RECOVERY   (device/scatter 0x2d80000, size 0x1000000; nothing else is writable)"
echo " addressing    $CALTXT"
echo "               raw read addr 0x3900000; raw write addr = 0x2d80000 + write base"
echo " image         $IMAGE"
echo " image sha256  $IMG_SHA"
echo " device must currently hold one of:"
for ((i = 1; i < ${#EXPECT[@]}; i += 2)); do echo "   $(sha256sum "${EXPECT[i]}" | cut -d' ' -f1)  ${EXPECT[i]}"; done
echo " canary limits: it only catches a base error of exactly +-0xb80000; any other mapping error is outside the"
echo " model, and mid-partition non-linearity is only caught by the readback. Saved window files hold private data."
echo " before the image: a 4 KiB canary write (restored at once) proves the write address base for RECOVERY;"
echo " otherwise nothing is written. Log: $LOG"
echo "====================================================="
confirm RECOVERY

arm_wait
set +e
"$PY" -I "${CMD[@]}" --log "$LOG" --preloader "$PRELOADER"
RC=$?
set -e
case $RC in
    0) RES="OK: pre-read matched, readback identical" ;;
    3) RES="REFUSED: pre-read mismatch, nothing written" ;;
    2) RES="FAILED: device may be modified, run restore-stock.sh" ;;
    6) RES="CANARY FAILED: the write address base does not hold for RECOVERY; no full write was done; see $LOG" ;;
    *) RES="ERROR (exit $RC), see $LOG" ;;
esac
partial_warning "$LOG" "$RC"
log_row "flash RECOVERY, image sha256 \`$IMG_SHA\` ($(basename "$IMAGE"))" "$RES"
echo "$RES"
exit "$RC"

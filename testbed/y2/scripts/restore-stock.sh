#!/usr/bin/env bash
# Write the backed-up stock BOOTIMG and/or RECOVERY back to the Y2 (guarded; see lib-flash.sh).
#
#   testbed/y2/scripts/restore-stock.sh [--target BOOTIMG|RECOVERY|all] [--yes] [--dry-run] [--force-unknown-current]
#     default target: all (BOOTIMG then RECOVERY, ONE DA session)
#     --force-unknown-current  only for recovering from a failed flash: skip the "device must hold a known
#                              image" check (the pre-read sha256 is still logged). Needs typing FORCE.
#
# Same guards as flash-recovery.sh, incl. the write calibration from probe-write.sh. Expected current content per target = the backup partition OR any
# result recorded in flash-state.json. All targets are pre-read and checked before the first write.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib-flash.sh"

ASSUME_YES=0; DRY=0; FORCE=0; WHICH=all
while (($#)); do
    case $1 in
        --yes) ASSUME_YES=1 ;;
        --dry-run) DRY=1 ;;
        --force-unknown-current) FORCE=1 ;;
        --target) WHICH="${2:?--target needs a value}"; shift ;;
        -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
        *) die "unknown option $1" ;;
    esac
    shift
done
env_overrides "$DRY"
((FORCE && ASSUME_YES)) && die "--yes cannot be combined with --force-unknown-current: the FORCE confirmation must be typed"
case $WHICH in
    all) TARGETS=(BOOTIMG RECOVERY) ;;
    BOOTIMG|RECOVERY) TARGETS=("$WHICH") ;;
    *) die "--target must be BOOTIMG, RECOVERY or all (got $WHICH)" ;;
esac

guard_backup

ARGS=()
for t in "${TARGETS[@]}"; do
    ARGS+=(--job "$t=$BACKUP/partitions/$t.bin" --expect "$t=$BACKUP/partitions/$t.bin")
    while IFS= read -r p; do [[ -n $p ]] && ARGS+=(--expect "$t=$p"); done \
        < <("$PY" -I "$Y2FLASH" state-expect --state "$STATE" --target "$t" --all)
done
((FORCE)) && ARGS+=(--force-unknown-current)
LOG="$LOGDIR/restore-stock-$(date +%Y%m%d-%H%M%S).log"
CMD=("$Y2FLASH" restore "${ARGS[@]}" --state "$STATE" --calibration "$CAL")

if ((DRY)); then
    echo
    python3 -I "${CMD[@]}" --dry-run
    exit 0
fi

CALTXT=$(cal_summary)
[[ -x $PY ]] || die "mtkclient venv missing: run tools/setup-host.sh"
[[ -f $PRELOADER ]] || die "factory preloader not found: $PRELOADER"
mkdir -p "$LOGDIR"
echo
echo "=================== RESTORE BANNER ==================="
echo " addressing: $CALTXT"
echo " writes the STOCK backup back to: ${TARGETS[*]}  (in that order, one session)"
for t in "${TARGETS[@]}"; do echo "   $t  sha256 $(sha256sum "$BACKUP/partitions/$t.bin" | cut -d' ' -f1)  from $BACKUP/partitions/$t.bin"; done
((FORCE)) && echo " *** --force-unknown-current: the current device content will NOT be checked ***"
echo " canary limits: it only catches a base error of exactly +-0xb80000; any other mapping error is outside the"
echo " model, and mid-partition non-linearity is only caught by the readback. Saved window files hold private data."
echo " Log: $LOG"
echo "======================================================="
if ((FORCE)); then confirm FORCE; else confirm RESTORE; fi

arm_wait
set +e
"$PY" -I "${CMD[@]}" --log "$LOG" --preloader "$PRELOADER"
RC=$?
set -e
case $RC in
    0) RES="OK: readback identical to backup" ;;
    3) RES="REFUSED: pre-read mismatch, nothing written (see $LOG; --force-unknown-current if you accept it)" ;;
    2) RES="FAILED: device may be modified, retry the restore" ;;
    6) RES="CANARY FAILED: the write address base does not hold for a target; no full write was done; see $LOG" ;;
    *) RES="ERROR (exit $RC), see $LOG" ;;
esac
partial_warning "$LOG" "$RC"
log_row "restore stock ${TARGETS[*]}" "$RES"
echo "$RES"
exit "$RC"

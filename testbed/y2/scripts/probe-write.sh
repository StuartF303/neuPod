#!/usr/bin/env bash
# Measure the Y2's WRITE address base (one DA session, confined to a zero block inside USRDATA).
#
#   testbed/y2/scripts/probe-write.sh [--yes] [--dry-run] [--remeasure]
#   testbed/y2/scripts/probe-write.sh --locate NONCE     find and zero a stray marker after a failed probe
#
# mtkclient's legacy READ and WRITE are different DA commands; the 0xb80000 shift was only measured on READ.
# This writes one 4 KiB marker at raw address 0x4C500000 (inside an all-zero 125 MiB run of USRDATA in the
# backup), finds where it landed by reading a 96 MiB window, derives WRITE_BASE, then zeroes the marker again
# and requires the window to be all zero. Result: ~/y2-backup/write-calibration.json, required by
# flash-recovery.sh and restore-stock.sh. Exit 4 = marker not found, 5 = unexpected base (needs review).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib-flash.sh"

ASSUME_YES=0; DRY=0; RE=(); LOCATE=""
while (($#)); do
    case $1 in
        --yes) ASSUME_YES=1 ;;
        --dry-run) DRY=1 ;;
        --remeasure) RE=(--remeasure) ;;
        --locate) LOCATE="${2:?--locate needs the 32-hex-digit nonce from the log or calibration file}"; shift ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) die "unknown option $1" ;;
    esac
    shift
done
[[ -z $LOCATE || $LOCATE =~ ^[0-9a-fA-F]{32}$ ]] || die "--locate needs a 32-hex-digit nonce"
env_overrides "$DRY"
guard_backup

# the probe window (device 0x49500000..0x4F500000 = 96 MiB around 0x4C500000) must be all zero in the backup;
# dump offset = device + 0xb80000
WIN_OFF=$((0x49500000 + 0xb80000)); WIN_LEN=$((96 * 1024 * 1024))
echo "== guard: probe window all-zero in the backup"
dd if="$BACKUP/emmc_user.bin" bs=1M skip="$WIN_OFF" count="$WIN_LEN" iflag=skip_bytes,count_bytes status=none | cmp -s -n "$WIN_LEN" - /dev/zero \
    || die "the probe window is not all zero in the backup: refusing"
echo "== guard: ok"

LOG="$LOGDIR/probe-write-$(date +%Y%m%d-%H%M%S).log"
CMD=("$Y2FLASH" probe --calibration "$CAL" "${RE[@]}")
[[ -z $LOCATE ]] || CMD=("$Y2FLASH" probe --locate --nonce "$LOCATE" --calibration "$CAL")
if ((DRY)); then
    echo
    [[ -z $LOCATE ]] || { echo "LOCATE mode: reads all of USRDATA looking for nonce $LOCATE; zeroes a matching marker block only."; exit 0; }
    echo "PLAN (nothing is connected or written): marker 4096 bytes at raw write addr 0x4c500000 (USRDATA),"
    echo "  read window device 0x49500000..0x4f500000 before/after (raw read addr +0xb80000), then zero the marker."
    echo "  calibration -> $CAL ($([[ -f $CAL ]] && echo 'exists: needs --remeasure' || echo 'absent'))"
    echo "  log -> $LOG"
    exit 0
fi
[[ -x $PY ]] || die "mtkclient venv missing: run tools/setup-host.sh"
[[ -f $PRELOADER ]] || die "factory preloader not found: $PRELOADER"
mkdir -p "$LOGDIR"
echo
echo "=================== PROBE BANNER ==================="
if [[ -n $LOCATE ]]; then echo " LOCATE: reads ALL of USRDATA (slow), then zeroes a stray marker block (nonce $LOCATE) if found."
else echo " WRITES one 4 KiB marker into USRDATA (an all-zero area), then zeroes it again."; fi
echo " Nothing else is written. Result goes to $CAL"
echo " canary limits: it only catches a base error of exactly +-0xb80000; any other mapping error is outside the"
echo " model, and mid-partition non-linearity is only caught by the readback. Saved window files hold private data."
echo " Log: $LOG"
echo "====================================================="
confirm PROBE

arm_wait
set +e
"$PY" -I "${CMD[@]}" --log "$LOG" --preloader "$PRELOADER"
RC=$?
set -e
case $RC in
    0) RES="OK: write base measured and trusted ($(cal_summary 2>/dev/null || echo see "$CAL"))" ;;
    3) RES="REFUSED: probe window not all zero on the device, nothing written" ;;
    4) RES="MARKER NOT FOUND: see $LOG; run probe-write.sh --locate <nonce from the log/calibration record> BEFORE anything else" ;;
    5) RES="UNEXPECTED BASE: saved but untrusted; needs human review ($CAL)" ;;
    2) RES="FAILED: probe write/restore problem; USRDATA zero run may be dirty, see $LOG" ;;
    *) RES="ERROR (exit $RC), see $LOG" ;;
esac
partial_warning "$LOG" "$RC"
log_row "probe write base (USRDATA marker)" "$RES"
echo "$RES"
exit "$RC"

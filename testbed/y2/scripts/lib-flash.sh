# Shared guards for the Y2 flash scripts. Sourced by flash-recovery.sh and restore-stock.sh; not run directly.
#
# Hard rules enforced here before anything can reach the device:
#   * the verified backup exists locally AND on the NAS, and its checksum files are identical in both places;
#   * writes go through tools/y2flash.py only (allowlist BOOTIMG/RECOVERY/USRDATA, pre-read compare, readback).
# The full NAS verify (all 3 images re-read via gio and compared to SHA256SUMS) was done 2026-11-10 19:45
# (docs/y2-log.md); here we only compare the checksum files, which is cheap.

HERE="$(cd "$(dirname "${BASH_SOURCE[1]:-${BASH_SOURCE[0]}}")" && pwd)"
Y2="$(cd "$HERE/.." && pwd)"
PY="$Y2/.venv/bin/python"
Y2FLASH="$Y2/tools/y2flash.py"
# Fixed locations. The Y2_* environment overrides below are IGNORED for real runs (a stray variable must never
# redirect a flash to another backup, calibration or state file); they are honoured only with --dry-run.
BACKUP="$HOME/y2-backup/2026-11-10"
NAS_URL="smb://warrender2.local/public/y2-backup/$(basename "$BACKUP")"
CAL="$HOME/y2-backup/write-calibration.json"      # written only by probe-write.sh
STATE="$HOME/y2-backup/flash-state.json"
LOGDIR="$HOME/y2-backup/flash-logs"
Y2LOG="$Y2/docs/y2-log.md"
FW="$HOME/y2-firmware/y2_v3.2.0_FM-20260813"
PRELOADER="$FW/preloader_eastaeon82_wet_kk.bin"   # EMI/DRAM config only; never written
PART_SIZE=$((0x1000000))                           # BOOTIMG and RECOVERY
die() { echo "error: $*" >&2; exit 1; }

# env_overrides <dry 0|1> : call right after argument parsing
env_overrides() {
    local v set=0
    for v in Y2_BACKUP Y2_NAS_URL Y2_WRITE_CAL Y2_FLASH_STATE Y2_FLASH_LOGS Y2_FIRMWARE; do
        [[ -n ${!v:-} ]] || continue
        if (($1)); then echo "(--dry-run: honouring $v=${!v})"; set=1
        else echo "note: ignoring environment override $v (only honoured with --dry-run)" >&2; fi
    done
    ((set)) || return 0
    BACKUP="${Y2_BACKUP:-$BACKUP}"; BACKUP="${BACKUP%/}"
    NAS_URL="${Y2_NAS_URL:-smb://warrender2.local/public/y2-backup/$(basename "$BACKUP")}"
    CAL="${Y2_WRITE_CAL:-$CAL}"; STATE="${Y2_FLASH_STATE:-$STATE}"; LOGDIR="${Y2_FLASH_LOGS:-$LOGDIR}"
    FW="${Y2_FIRMWARE:-$FW}"; PRELOADER="$FW/preloader_eastaeon82_wet_kk.bin"
}

guard_backup() {
    [[ -f $BACKUP/SHA256SUMS && -f $BACKUP/partitions/SHA256SUMS ]] \
        || die "no backup with checksum files at $BACKUP (need SHA256SUMS and partitions/SHA256SUMS)"
    echo "== guard: verifying local backup $BACKUP"
    env -u Y2_SCATTER -u Y2_FIRMWARE "$HERE/verify-backup.sh" "$BACKUP" >"${TMPDIR:-/tmp}/y2-verify.$$.log" 2>&1 \
        || { cat "${TMPDIR:-/tmp}/y2-verify.$$.log" >&2; rm -f "${TMPDIR:-/tmp}/y2-verify.$$.log"; die "verify-backup.sh failed: refusing"; }
    tail -1 "${TMPDIR:-/tmp}/y2-verify.$$.log"; rm -f "${TMPDIR:-/tmp}/y2-verify.$$.log"
    (cd "$BACKUP/partitions" && sha256sum -c --quiet SHA256SUMS) || die "partition checksums in $BACKUP/partitions do not match: refusing"
    local t
    for t in BOOTIMG RECOVERY; do
        [[ $(stat -c %s "$BACKUP/partitions/$t.bin" 2>/dev/null || echo 0) -eq $PART_SIZE ]] \
            || die "$BACKUP/partitions/$t.bin missing or not 16 MiB"
    done
    echo "== guard: NAS copy $NAS_URL"
    command -v gio >/dev/null || die "gio not found: cannot check the NAS copy, refusing"
    # only the top-level SHA256SUMS is on the NAS: partitions/ is carved from emmc_user.bin locally
    gio cat "$NAS_URL/SHA256SUMS" 2>/dev/null | cmp -s - "$BACKUP/SHA256SUMS" \
        || die "NAS copy missing or its SHA256SUMS differs from the local one ($NAS_URL; is the share mounted?): refusing"
    echo "== guard: backup verified locally; NAS checksum files identical"
}

# confirm <word> : require the operator to type <word> unless --yes was given (ASSUME_YES=1)
confirm() {
    ((${ASSUME_YES:-0})) && { echo "(--yes given: not asking)"; return 0; }
    local ans; read -r -p "Type $1 to proceed: " ans
    [[ $ans == "$1" ]] || die "not confirmed: nothing was done"
}

arm_wait() {  # wait for the Y2 to leave the bus, then tell the human what to do
    command -v lsusb >/dev/null || die "lsusb not found (install usbutils)"
    echo "Waiting for the Y2 to leave the bus (unplug it)..."
    while lsusb -d 0e8d: >/dev/null 2>&1; do sleep 0.5; done
    echo "Armed. Power the Y2 off, hold Vol-, plug in USB."
}

# log_row <action> <result> : append to docs/y2-log.md (local notes, not in git)
log_row() {
    [[ -f $Y2LOG ]] || return 0
    printf '| %s | %s | %s |\n' "$(date -u +'%F %H:%M')" "$1" "$2" >>"$Y2LOG"
}

# cal_summary : print "read 0x.. write 0x.." from the calibration, or fail closed (also validated by y2flash)
cal_summary() {
    [[ -f $CAL ]] || die "no write calibration ($CAL): the WRITE address base is unknown. Run scripts/probe-write.sh first. Refusing."
    python3 -I - "$CAL" <<'PY' || die "write calibration unusable (see above): refusing"
import json, sys
d = json.load(open(sys.argv[1]))
if d.get("trusted") is not True or d.get("write_base") not in (0, 0xB80000):
    sys.exit(f"calibration is not usable (write_base {d.get('write_base')!r}, trusted {d.get('trusted')!r}, "
             f"reason {d.get('reason')!r}): stop and investigate")
print(f"read base {d['read_base']:#x}, write base {d['write_base']:#x}  (measured {d.get('measured_at')})")
PY
}

# partial_warning <log> <rc> : after a failed/interrupted run, say loudly if a write was started but not finished
partial_warning() {
    local st dn
    st=$(grep -c 'WRITE STARTED' "$1" 2>/dev/null || true); dn=$(grep -c 'WRITE DONE' "$1" 2>/dev/null || true)
    if (( $2 != 0 )) && (( ${st:-0} > ${dn:-0} )); then
        echo "!!! DEVICE MAY BE PARTIALLY WRITTEN (a write was started and never finished) - see $1;" >&2
        echo "!!! restore with: scripts/restore-stock.sh --force-unknown-current" >&2
        return 0
    fi
    return 0
}

# Shared guards for the Y2 flash scripts. Sourced by flash-recovery.sh, flash-boot.sh, flash-rootfs.sh, flash-logo.sh and restore-stock.sh; not run directly.
#
# Hard rules enforced here before anything can reach the device:
#   * the verified backup exists locally AND on the NAS, and its checksum files are identical in both places;
#   * writes go through tools/y2flash.py only (allowlist BOOTIMG/RECOVERY/USRDATA/LOGO, pre-read compare, readback).
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
LOGO_SIZE=$((0x300000))                            # LOGO (scatter physical 0x4400000)
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

# validate_bootimg <image> <ROOTFS|RECOVERY> : refuse anything that is not a sane MTK Android boot image BEFORE the
# banner: "ANDROID!" at offset 0, mtk-bootimg.py info exits 0 with "sha1 OK", kernel MTK header named KERNEL and a
# ramdisk MTK header. A ramdisk name other than the target's stock one is only a warning. Sets BOOTINFO.
validate_bootimg() {
    local img=$1 want=$2 rc=0 kl rl name
    [[ $(head -c 8 "$img") == "ANDROID!" ]] || die "$img does not start with ANDROID! (not an Android boot image): refusing"
    BOOTINFO=$(python3 -I "$Y2/tools/mtk-bootimg.py" info "$img" 2>&1) || rc=$?
    ((rc == 0)) || { echo "$BOOTINFO" >&2; die "mtk-bootimg.py info failed (exit $rc) for $img: refusing"; }
    grep -q '^sha1 OK' <<<"$BOOTINFO" || { echo "$BOOTINFO" >&2; die "$img: sha1 check is not OK: refusing"; }
    kl=$(grep '^kernel:' <<<"$BOOTINFO" || true); rl=$(grep '^ramdisk:' <<<"$BOOTINFO" || true)
    [[ $kl == *"MTK name='KERNEL'"* ]] || { echo "$BOOTINFO" >&2; die "$img: kernel has no MTK KERNEL header: refusing"; }
    [[ $rl == *"MTK name='"* ]] || { echo "$BOOTINFO" >&2; die "$img: ramdisk has no MTK header: refusing"; }
    name=${rl#*MTK name=\'}; name=${name%%\'*}
    BOOTWARN=""
    if [[ $name != "$want" ]]; then
        BOOTWARN="WARNING: ramdisk MTK name is '$name', the stock name for this target is '$want'"
        echo "$BOOTWARN" >&2
    fi
}

# confirm_always <word> : like confirm, but --yes does NOT skip it (destructive, irrecoverable-by-this-script steps)
confirm_always() {
    local ans
    ((${ASSUME_YES:-0})) && echo "(--yes given: this confirmation is still required)"
    read -r -p "Type $1 to proceed: " ans
    [[ $ans == "$1" ]] || die "not confirmed: nothing was done"
}

# usrdata_backup_guard : the stock USRDATA backup must be present and full size (restore and rootfs flash rely on it)
usrdata_backup_guard() {
    [[ $(stat -c %s "$BACKUP/partitions/USRDATA.bin" 2>/dev/null || echo 0) -eq $((0x32000000)) ]] \
        || die "$BACKUP/partitions/USRDATA.bin missing or not 800 MiB: refusing"
}

# logo_backup_guard : the stock LOGO backup must be present, exactly the partition size, and a valid logo container
# (y2logo.py info --partition); the restore path and the pre-read compare rely on it
logo_backup_guard() {
    [[ $(stat -c %s "$BACKUP/partitions/LOGO.bin" 2>/dev/null || echo 0) -eq $LOGO_SIZE ]] \
        || die "$BACKUP/partitions/LOGO.bin missing or not 3 MiB: refusing"
    python3 -I "$Y2/tools/y2logo.py" info --partition "$BACKUP/partitions/LOGO.bin" >/dev/null 2>&1 \
        || die "$BACKUP/partitions/LOGO.bin is not a valid logo container (y2logo.py info --partition): refusing"
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

# partial_warning <log> <rc> [target] : after a failed/interrupted run, say loudly if a write was started but not
# finished. The advice depends on the target (USRDATA must never be "fixed" by restoring stock BOOTIMG).
partial_warning() {
    local st dn t="${3:-}"
    st=$(grep -c 'WRITE STARTED' "$1" 2>/dev/null || true); dn=$(grep -c 'WRITE DONE' "$1" 2>/dev/null || true)
    if (( $2 != 0 )) && (( ${st:-0} > ${dn:-0} )); then
        echo "!!! DEVICE MAY BE PARTIALLY WRITTEN (a write was started and never finished) - see $1;" >&2
        case $t in
            USRDATA)
                echo "!!! re-run scripts/flash-rootfs.sh, or scripts/restore-stock.sh --target USRDATA" >&2
                echo "!!! do NOT run restore-stock.sh with no target / all: that restores stock BOOTIMG, and stock Android" >&2
                echo "!!! would then mount the damaged ext4 as /data" >&2 ;;
            BOOTIMG|RECOVERY)
                echo "!!! restore with: scripts/restore-stock.sh --target $t --force-unknown-current" >&2 ;;
            LOGO)
                echo "!!! restore with: scripts/restore-stock.sh --target LOGO --force-unknown-current" >&2
                echo "!!! LOGO holds only the power-on/charging pictures: BOOTIMG, RECOVERY and USRDATA are not involved," >&2
                echo "!!! so do not restore anything else for this (re-enter download mode with Vol-)" >&2 ;;
            *) echo "!!! restore with: scripts/restore-stock.sh --target <BOOTIMG|RECOVERY|USRDATA|LOGO> --force-unknown-current (see $1)" >&2 ;;
        esac
        return 0
    fi
    return 0
}

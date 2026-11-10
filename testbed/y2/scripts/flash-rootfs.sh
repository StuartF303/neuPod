#!/usr/bin/env bash
# Flash the y2-rescue Buildroot ext4 rootfs into USRDATA of the Y2 (guarded; see lib-flash.sh, tools/y2flash.py).
# THIS DESTROYS THE STOCK ANDROID USER DATA (it is in the backup; restore-stock.sh --target USRDATA brings it back).
#
#   testbed/y2/scripts/flash-rootfs.sh [--yes] [--dry-run] [IMAGE]
#     IMAGE    default: ~/y2-build/y2-rootfs.ext4 (refused if absent; must be an ext4 image <= 0x31f00000 bytes,
#              the userdata size LK reports; the scatter's 0x32000000 is NOT usable)
#     --dry-run  runs the backup guards and prints the plan; never connects, never prompts
#     --yes      does NOT skip the confirmations: DESTROY-USRDATA is always typed (and I-UNDERSTAND, see below)
#
# Always uses y2flash --discard-current: USRDATA holds stock user data that changes whenever stock Android runs,
# and, once flashed, the rootfs is mounted read-write by the running system, so the pre-read would NEVER equal
# the backup or the previously flashed image. Instead of comparing, the current first 1 MiB of USRDATA and the
# canary window are saved to the log dir (evidence), then canary, write, full readback compare. No full
# pre-read. flash-state.json records a reference (path+sha256+length) of the image, not an 800 MiB copy.
# Environment overrides (Y2_BACKUP etc.) are ignored unless --dry-run. Needs the write calibration.
# If BOOTIMG may still hold stock Android (per flash-state.json), a stock boot would mount the rootfs as /data:
# you must additionally type I-UNDERSTAND (flash-boot.sh first is the intended order).
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
USR_MAX=$((0x31f00000))
IMAGE="${IMAGE:-$HOME/y2-build/y2-rootfs.ext4}"
[[ -f $IMAGE ]] || die "image not found: $IMAGE (build the rootfs first, or pass IMAGE explicitly)"
IMAGE="$(cd "$(dirname "$IMAGE")" && pwd)/$(basename "$IMAGE")"
SIZE=$(stat -c %s "$IMAGE")
((SIZE > 0)) || die "image is empty: $IMAGE"
((SIZE <= USR_MAX)) || die "image is $SIZE bytes, larger than the $USR_MAX (0x31f00000) userdata size LK reports"
# ext4 superblock magic 0xEF53 at 0x438 (little endian: 53 ef): catches flashing the wrong file / a sparse image
[[ $(dd if="$IMAGE" bs=1 skip=$((0x438)) count=2 2>/dev/null | od -An -tx1 | tr -d ' \n') == 53ef ]] \
    || die "$IMAGE does not look like a raw ext4 image (no ext4 magic at 0x438; sparse images are not accepted)"

guard_backup
usrdata_backup_guard

# Does flash-state.json show BOOTIMG holding something other than stock? (the state file is all we know offline)
STOCK_BOOT_SHA=$(sha256sum "$BACKUP/partitions/BOOTIMG.bin" | cut -d' ' -f1)
BOOT_LAST=$(python3 -I "$Y2FLASH" state-last --state "$STATE" --target BOOTIMG)   # no device, no mtkclient
BOOT_STOCK=1
if [[ $BOOT_LAST == "ok "* && ${BOOT_LAST#ok } != "$STOCK_BOOT_SHA" ]]; then BOOT_STOCK=0; fi

IMG_SHA=$(sha256sum "$IMAGE" | cut -d' ' -f1)
LOG="$LOGDIR/flash-rootfs-$(date +%Y%m%d-%H%M%S).log"
CMD=("$Y2FLASH" write --target USRDATA --image "$IMAGE" --discard-current --state "$STATE" --calibration "$CAL")

if ((DRY)); then
    echo
    echo "boot-image state: ${BOOT_LAST} -> $( ((BOOT_STOCK)) && echo 'BOOTIMG may still hold STOCK (a real run would demand I-UNDERSTAND)' || echo 'BOOTIMG holds a non-stock image per the state file')"
    python3 -I "${CMD[@]}" --dry-run
    exit 0
fi

CALTXT=$(cal_summary)
[[ -x $PY ]] || die "mtkclient venv missing: run tools/setup-host.sh"
[[ -f $PRELOADER ]] || die "factory preloader not found: $PRELOADER"
mkdir -p "$LOGDIR"
echo
echo "=================== ROOTFS FLASH BANNER ==================="
echo " target        USRDATA   (device/scatter 0x40380000; size 0x32000000, but only the first 0x31f00000 are usable)"
echo " addressing    $CALTXT"
echo "               raw read addr 0x40f00000; raw write addr = 0x40380000 + write base"
echo " image         $IMAGE  ($SIZE bytes)"
echo " image sha256  $IMG_SHA"
echo
echo " !!! THIS DESTROYS THE CURRENT USRDATA = the stock Android user data (apps, settings, music, ...). !!!"
echo " !!! It is NOT compared with anything first. The stock copy is in the backup and can be put back  !!!"
echo " !!! with: scripts/restore-stock.sh --target USRDATA                                              !!!"
echo
echo " Evidence first: the current first 1 MiB of USRDATA and the canary window are saved (private data) to $LOGDIR."
echo " No full pre-read. Then a 4 KiB canary write (restored at once) proves the write base for USRDATA; then the image"
echo " is written and the whole written length is read back and compared."
# measured: write ~4.8 MB/s, readback ~2 MB/s
echo " Estimated time: write ~$((SIZE * 10 / 48000000)) s, readback ~$((SIZE / 2000000)) s (plus canary/evidence reads)."
echo " canary limits: it only catches a base error of exactly +-0xb80000; any other mapping error is outside the"
echo " model, and mid-partition non-linearity is only caught by the readback."
if ((BOOT_STOCK)); then
    echo
    echo " *** WARNING: flash-state.json does not show a non-stock BOOTIMG (last BOOTIMG entry: $BOOT_LAST). ***"
    echo " *** If BOOTIMG still holds stock Android, booting it normally will MOUNT THIS ROOTFS AS /data:   ***"
    echo " *** Android will see an unknown filesystem and may offer/perform a FACTORY RESET, which would    ***"
    echo " *** wipe the rootfs again. Flash BOOTIMG (scripts/flash-boot.sh) before booting normally.        ***"
fi
echo " Log: $LOG"
echo "============================================================"
confirm_always DESTROY-USRDATA
((BOOT_STOCK)) && confirm_always I-UNDERSTAND

arm_wait
set +e
"$PY" -I "${CMD[@]}" --log "$LOG" --preloader "$PRELOADER"
RC=$?
set -e
case $RC in
    0) RES="OK: canary landed, readback identical to the image (stock user data replaced; evidence in $LOGDIR)" ;;
    3) RES="REFUSED: pre-read mismatch, nothing written" ;;
    2) RES="FAILED: USRDATA may be partly written; re-run flash-rootfs.sh, or restore-stock.sh --target USRDATA (NOT restore-stock.sh with no target/all: stock BOOTIMG would mount the damaged ext4 as /data)" ;;
    6) RES="CANARY FAILED: the write address base does not hold for USRDATA; no full write was done; see $LOG" ;;
    *) RES="ERROR (exit $RC), see $LOG" ;;
esac
partial_warning "$LOG" "$RC" USRDATA
log_row "flash USRDATA (discard-current), image sha256 \`$IMG_SHA\` ($(basename "$IMAGE"))" "$RES"
echo "$RES"
exit "$RC"

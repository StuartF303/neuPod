#!/usr/bin/env bash
# Copy a verified Y2 backup to the second location (NAS) and verify it there.
#
#   testbed/y2/scripts/copy-backup.sh [backup-dir] [second-root]
#     backup-dir   default: newest ~/y2-backup/* containing SHA256SUMS
#     second-root  default $Y2_BACKUP2 or the warrender2 'public' share via gvfs; it must already
#                  exist and be a network/FUSE mount on a different filesystem from the backup
#   Y2_BACKUP2_URI  gio URI of the same folder (MUST be set together with Y2_BACKUP2 / arg 2, or neither);
#                   used to re-read the copy from the NAS (default smb://warrender2.local/public/y2-backup)
#
# Mount the share first (as your user): gio mount smb://warrender2.local/public
# Idempotent: files already present with matching sha256 are not re-copied.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
die() { echo "error: $*" >&2; exit 1; }

SRC="${1:-}"
if [[ -z $SRC ]]; then
    for d in "$HOME"/y2-backup/*/; do
        [[ -f ${d}SHA256SUMS ]] && SRC="${d%/}"
    done
    [[ -n $SRC ]] || die "no backup dir with SHA256SUMS under $HOME/y2-backup"
fi
SRC="${SRC%/}"
# The mount path and the URI name the same folder; overriding one without the other would
# verify a different place from the one copied to, so they must be overridden together.
if [[ -n ${2:-${Y2_BACKUP2:-}} && -z ${Y2_BACKUP2_URI:-} ]] || [[ -z ${2:-${Y2_BACKUP2:-}} && -n ${Y2_BACKUP2_URI:-} ]]; then
    die "set the second root (arg 2 / Y2_BACKUP2) and Y2_BACKUP2_URI together, or neither"
fi
ROOT2="${2:-${Y2_BACKUP2:-/run/user/$(id -u)/gvfs/smb-share:server=warrender2.local,share=public/y2-backup}}"
URI="${Y2_BACKUP2_URI:-smb://warrender2.local/public/y2-backup}"
NAME=$(basename "$SRC")
DST="$ROOT2/$NAME"

[[ -f $SRC/SHA256SUMS ]] || die "no SHA256SUMS in $SRC (run backup-all.sh first)"
command -v gio >/dev/null || die "gio not found; it is needed to re-read the copy from the NAS"

echo "== verifying source $SRC"
"$HERE/verify-backup.sh" "$SRC" || die "source backup failed verification; not copying"

[[ -d $ROOT2 ]] || die "second location not reachable: $ROOT2 (share mounted?)"
fstype=$(findmnt -n -o FSTYPE -T "$ROOT2") || die "cannot determine filesystem of $ROOT2"
case $fstype in
    fuse.gvfsd-fuse|cifs|smb3|nfs|nfs4) ;;
    *) die "$ROOT2 is on '$fstype', not a network mount; refusing (is the share mounted?)" ;;
esac
[[ $(stat -c %d "$ROOT2") != "$(stat -c %d "$SRC")" ]] || die "$ROOT2 is on the same device as the source"

mkdir -p "$DST"
want_of() { awk -v f="$1" '{n=$2; sub(/^\*/,"",n)} n==f {print $1; exit}' "$SRC/SHA256SUMS"; }

mapfile -t files < <(awk '{n=$2; sub(/^\*/,"",n); print n}' "$SRC/SHA256SUMS")
for f in "${files[@]}" SHA256SUMS backup.log; do
    [[ -e $SRC/$f ]] || continue
    want=$(want_of "$f")
    if [[ -n $want && -f $DST/$f ]] && [[ $(sha256sum "$DST/$f" | cut -d' ' -f1) == "$want" ]]; then
        echo "present: $f"; continue
    fi
    echo "copying: $f"
    cp -f "$SRC/$f" "$DST/$f.part" || { echo "error: copy of $f failed" >&2; exit 1; }
    rm -f "${DST:?}/${f:?}"
    mv -f "$DST/$f.part" "$DST/$f" || die "could not move $f into place"
done

nas_sha() { gio cat "$URI/$NAME/$1" | sha256sum | cut -d' ' -f1; }  # pipefail makes a gio failure fail this

# Re-read from the NAS over SMB (not the local mount's page cache) and compare to the SOURCE sums.
echo "== verifying copy via $URI/$NAME (reads from the NAS)"
for f in "${files[@]}"; do
    want=$(want_of "$f")
    [[ -n $want ]] || die "no hash for $f in source SHA256SUMS"
    got=$(nas_sha "$f") || die "gio cat failed for $f"
    [[ $got == "$want" ]] || die "NAS copy of $f does not match source (got $got, want $want)"
    echo "ok: $f"
done
for f in SHA256SUMS backup.log; do
    [[ -e $SRC/$f ]] || continue
    got=$(nas_sha "$f") || die "gio cat failed for $f"
    [[ $got == "$(sha256sum "$SRC/$f" | cut -d' ' -f1)" ]] || die "NAS copy of $f differs from source"
done
echo "second copy verified: $DST"

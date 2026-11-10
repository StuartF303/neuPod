#!/usr/bin/env bash
# Mirror the git-ignored Y2 notes (docs/, recon/) to the NAS. Idempotent; never deletes on the NAS.
#   testbed/y2/scripts/sync-notes.sh [dest]   default: warrender2 public share, y2-backup/notes
# Mount first: gio mount smb://warrender2.local/public
set -euo pipefail
Y2="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-/run/user/$(id -u)/gvfs/smb-share:server=warrender2.local,share=public/y2-backup/notes}"
ROOT=$(dirname "$DEST")
[[ -d $ROOT ]] || { echo "error: $ROOT not reachable (share mounted?)" >&2; exit 1; }
case $(findmnt -n -o FSTYPE -T "$ROOT") in fuse.gvfsd-fuse|cifs|smb3|nfs|nfs4) ;; *) echo "error: $ROOT is not a network mount" >&2; exit 1;; esac
mkdir -p "$DEST"
# gvfs-fuse has no owners/perms/times semantics worth preserving: compare by size+checksum
for d in docs recon; do
    [[ -d $Y2/$d ]] || continue
    rsync -r --checksum --no-perms --no-owner --no-group --omit-dir-times --exclude '*.img' --exclude '*.bin' "$Y2/$d/" "$DEST/$d/"
done
echo "notes mirrored to $DEST"

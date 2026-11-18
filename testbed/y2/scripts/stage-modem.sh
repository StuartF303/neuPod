#!/usr/bin/env bash
# Stage the stock modem (MD1) files from the device backup onto the Y2 for S78mtk-md. Nothing here is ever committed: the files are
# MediaTek/vendor files and carry device-specific modem NVRAM.
#
#   stage-modem.sh [--dry-run]
#
# Env: Y2_HOST (default root@192.168.7.2); Y2_BACKUP (default ~/y2-backup/2026-11-10/partitions).
# Reads the backup images read-only with debugfs (no mount, no root). Extracts, into a throw-away host directory:
#   ANDROID.bin   /bin/{ccci_fsd,ccci_mdinit,gsm0710muxd,linker}, the Bionic libs they need under /lib, and
#                 /etc/firmware/{modem_1_2g_n.img,S_ANDRO_SFL.ini}                      -> /opt/mtk-wmt/system  (/system)
#   USRDATA.bin   /nvram/md (the modem's NVRAM copy)                                    -> /opt/mtk-wmt/data    (/data)
#   PROTECT_F.bin /md, PROTECT_S.bin /md                                                -> /protect_f/md, /protect_s/md
# and copies them over ssh (tar, no sftp). Files under system/ are replaced only if they differ (a running daemon's binary is
# replaced by rename, never rewritten in place). Files under data/ and protect_*/ are only ADDED when missing, never overwritten:
# the modem daemons update them on the device. Touches nothing else: no /dev/mmcblk0*, no /data/nvram/APCFG, no /data/BT_Addr.
set -euo pipefail

HOST="${Y2_HOST:-root@192.168.7.2}"
BACKUP="${Y2_BACKUP:-$HOME/y2-backup/2026-11-10/partitions}"
DRY=0
for a in "$@"; do
	case "$a" in
		--dry-run) DRY=1 ;;
		-h|--help) sed -n '2,/^set -euo/{/^set -euo/!p}' "$0"; exit 0 ;;
		*) echo "unknown option: $a" >&2; exit 2 ;;
	esac
done
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")

for f in ANDROID USRDATA PROTECT_F PROTECT_S; do
	[ -r "$BACKUP/$f.bin" ] || { echo "error: $BACKUP/$f.bin not readable" >&2; exit 1; }
done
command -v debugfs >/dev/null || { echo "error: debugfs (e2fsprogs) not installed" >&2; exit 1; }

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/system/bin" "$W/system/lib" "$W/system/etc/firmware" "$W/data/nvram" "$W/protect_f" "$W/protect_s"
# debugfs exits 0 even for a missing file, so the output is checked too and every extracted file must be non-empty.
dbg() { # <debugfs request> <image> <expected host path (file or directory)>
	debugfs -R "$1" "$BACKUP/$2.bin" >"$W/err" 2>&1 || { cat "$W/err" >&2; exit 1; }
	if grep -qiE 'not found|no such|error|bad ' "$W/err"; then echo "error: debugfs '$1' on $2.bin: $(grep -iE 'not found|no such|error|bad ' "$W/err" | head -1)" >&2; exit 1; fi
	if [ -d "$3" ]; then
		[ -n "$(find "$3" -type f -size +0c | head -1)" ] || { echo "error: '$1' on $2.bin extracted nothing" >&2; exit 1; }
	else
		[ -s "$3" ] || { echo "error: '$1' on $2.bin gave an empty or missing $3" >&2; exit 1; }
	fi
}

for b in ccci_fsd ccci_mdinit gsm0710muxd linker; do dbg "dump /bin/$b $W/system/bin/$b" ANDROID "$W/system/bin/$b"; done
for l in libc libm libdl libstdc++ libcutils liblog libutils libz libnvram libnvram_sec libnvram_platform libcustom_nvram libaed \
	libhardware_legacy libnetutils libwpa_client libgccdemangle libcorkscrew libmtk_drvb; do
	dbg "dump /lib/$l.so $W/system/lib/$l.so" ANDROID "$W/system/lib/$l.so"
done
for f in modem_1_2g_n.img S_ANDRO_SFL.ini; do dbg "dump /etc/firmware/$f $W/system/etc/firmware/$f" ANDROID "$W/system/etc/firmware/$f"; done
dbg "rdump /nvram/md $W/data/nvram" USRDATA "$W/data/nvram/md"
# /data/ccci_cfg does not exist in the backup; S78mtk-md makes it (empty) as the stock init does
dbg "rdump /md $W/protect_f" PROTECT_F "$W/protect_f/md"
dbg "rdump /md $W/protect_s" PROTECT_S "$W/protect_s/md"
rm -f "$W/err"
chmod 755 "$W"/system/bin/*
find "$W" -type d -exec chmod 775 {} +

echo "==> extracted: $(find "$W" -type f | wc -l) files, $(du -sk "$W" | cut -f1) KiB (host dir removed on exit)"
if [ "$DRY" = 1 ]; then
	echo "DRY: would stage on $HOST:/opt/mtk-wmt (system and data) and /protect_{f,s}:"
	(cd "$W" && find system -type f | sort | sed 's/^/DRY:   /')
	echo "DRY: plus $(find "$W/data" -type f | wc -l) data files and $(find "$W/protect_f" "$W/protect_s" -type f | wc -l) protect files (added only if missing)"
	exit 0
fi

# One remote script: unpack to /opt/mtk-wmt.modem-in, merge, remove. Runs on busybox sh.
tar -C "$W" --owner=0 --group=0 -cf - . | "${SSH[@]}" '
set -e
IN=/opt/mtk-wmt.modem-in
rm -rf $IN; mkdir -p $IN; tar -C $IN -xf -
mkdir -p /opt/mtk-wmt
# system: replace when different, by rename
cd $IN/system
find . -type f >$IN/system.list
while read -r f; do
	d=/opt/mtk-wmt/system/$f
	mkdir -p "$(dirname "$d")" || exit 1
	if ! cmp -s "$f" "$d" 2>/dev/null; then
		cp "$f" "$d.new" || exit 1
		mv "$d.new" "$d" || exit 1
	fi
done <$IN/system.list
# data and protect: add what is missing, never overwrite
merge() { # <src dir> <dst dir>
	mkdir -p "$2" || return 1
	cd "$1" || return 1
	find . -type f >$IN/merge.list
	while read -r f; do
		if [ ! -e "$2/$f" ]; then
			mkdir -p "$(dirname "$2/$f")" || return 1
			cp "$f" "$2/$f" || return 1
		fi
	done <$IN/merge.list
}
merge $IN/data /opt/mtk-wmt/data || exit 1
# /system and /data are symlinks into /opt/mtk-wmt (created when the Bluetooth files were staged); only make them if absent
[ -e /system ] || ln -s /opt/mtk-wmt/system /system
[ -e /data ] || ln -s /opt/mtk-wmt/data /data
for p in protect_f protect_s; do
	[ -e /$p ] || { mkdir -p /opt/mtk-wmt/$p; ln -s /opt/mtk-wmt/$p /$p; }
	merge $IN/$p /$p || exit 1
done
cd /; rm -rf $IN; sync
echo staged
'

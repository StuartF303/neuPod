#!/bin/sh
# diag.sh [--no-capture|--no-input]: collect Y2 hardware facts into /tmp/diag.txt (and onto the microSD
# if it is writable). Read-only towards block devices: USRDATA and the SD are mounted ro;
# the SD is remounted rw only to save the report, and only if that works.
#   --no-capture   skip the 10 s per-input-device event capture (alias: --no-input)
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
OUT=/tmp/diag.txt
CAPTURE=1
case $1 in --no-capture | --no-input) CAPTURE=0 ;; esac

sec() { printf '\n===== %s =====\n' "$*"; }
cat_f() { for f in "$@"; do if [ -r "$f" ]; then printf '%s: ' "$f"; cat "$f"; fi; done; }

# Try each filesystem type name the stock kernel might know (exFAT is a vendor driver).
try_mount_ro() { # dev mountpoint
	for t in ext4 vfat exfat texfat sdfat; do
		o=ro
		[ "$t" = ext4 ] && o=ro,noload
		if mount -t "$t" -o "$o" "$1" "$2" 2>/dev/null; then
			echo "mounted $1 on $2 ro as $t"
			return 0
		fi
	done
	echo "could not mount $1 read-only (tried ext4 vfat exfat texfat sdfat)"
	return 1
}

{
	echo "y2-rescue diag, $(date 2>/dev/null) (uptime $(cut -d' ' -f1 /proc/uptime) s)"

	sec "framebuffer (fbtool info)"
	fbtool info 2>&1
	cat_f /sys/class/graphics/fb0/bits_per_pixel /sys/class/graphics/fb0/virtual_size \
		/sys/class/graphics/fb0/stride /sys/class/graphics/fb0/name

	sec "input devices (/proc/bus/input/devices)"
	cat /proc/bus/input/devices 2>&1
	sec "input device names (/sys/class/input)"
	for d in /sys/class/input/input* /sys/class/input/event*; do
		[ -r "$d/name" ] && printf '%s: %s\n' "$d" "$(cat "$d/name")"
	done

	if [ $CAPTURE = 1 ]; then
		sec "input capture, 10 s per device (touch / press / turn the wheel now)"
		# evdev reads must be multiples of 16 bytes: busybox hexdump on the node fails with EINVAL,
		# so capture with cat into a regular file and decode that. struct input_event is
		# {sec, usec, type:16, code:16, value:32} on this 32-bit kernel.
		for ev in /dev/input/event*; do
			[ -c "$ev" ] || continue
			echo "--- $ev ---"
			timeout 10 cat "$ev" >"/tmp/${ev##*/}.bin" 2>/dev/null
			echo "$(wc -c <"/tmp/${ev##*/}.bin") bytes captured"
			od -An -tu4 -w16 -v "/tmp/${ev##*/}.bin" 2>/dev/null | awk '
				{ t = $3 % 65536; c = int($3 / 65536); v = $4; if (v > 2147483647) v -= 4294967296
				  if (t == 0) next  # EV_SYN
				  n++; if (n <= 40) printf "type=%d code=%d value=%d\n", t, c, v }
				END { printf "%d non-SYN events\n", n }'
		done
	fi

	sec "block devices"
	cat /proc/partitions
	for d in /sys/class/block/*; do
		printf '%s start=%s size=%s\n' "${d##*/}" "$(cat "$d/start" 2>/dev/null)" "$(cat "$d/size" 2>/dev/null)"
	done
	blkid 2>&1
	sec "filesystems the kernel knows"
	cat /proc/filesystems

	sec "mount USRDATA read-only"
	mkdir -p /mnt/ud
	for d in /sys/class/block/mmcblk0p*; do
		if [ "$(cat "$d/size" 2>/dev/null)" = 1638400 ] && [ "$(cat "$d/start" 2>/dev/null)" = 2104320 ]; then
			UD=/dev/${d##*/}
		fi
	done
	if [ -n "$UD" ]; then
		echo "USRDATA = $UD"
		if mount -t ext4 -o ro,noload "$UD" /mnt/ud 2>/dev/null; then
			echo "mounted ro at /mnt/ud"
			ls -la /mnt/ud
			[ -e /mnt/ud/sbin/init ] && echo "has /sbin/init" || echo "no /sbin/init"
			umount /mnt/ud
		else
			echo "ext4 ro mount failed"
		fi
	else
		echo "USRDATA (start 2104320, size 1638400 sectors) not found"
	fi

	sec "mount microSD read-only"
	mkdir -p /mnt/sd
	SDDEV=
	if grep -q " /mnt/sd " /proc/mounts; then
		echo "already mounted:"
		grep " /mnt/sd " /proc/mounts
		SDMOUNTED=1
	else
		for dev in /dev/mmcblk1p1 /dev/mmcblk1; do
			[ -b "$dev" ] || continue
			SDDEV=$dev
			try_mount_ro "$dev" /mnt/sd && { SDMOUNTED=1; break; }
		done
		[ -n "$SDDEV" ] || echo "no /dev/mmcblk1* (no card?)"
	fi
	[ -n "$SDMOUNTED" ] && ls -la /mnt/sd | head -30

	sec "power supply"
	for p in /sys/class/power_supply/*; do
		[ -d "$p" ] || continue
		echo "--- $p ---"
		cat "$p/uevent" 2>&1
	done

	sec "backlight / leds"
	for l in /sys/class/leds/* /sys/class/backlight/*; do
		[ -d "$l" ] || continue
		echo "--- $l ---"
		cat_f "$l/brightness" "$l/max_brightness" "$l/trigger"
	done
	echo "(expected: /sys/class/leds/lcd-backlight/brightness, max 255, boots at 128)"

	sec "headphone jack (h2w: 0 none, 2 headphones)"
	cat_f /sys/class/switch/h2w/state

	sec "/proc/version";  cat /proc/version
	sec "/proc/cmdline";  cat /proc/cmdline
	sec "/proc/cpuinfo";  cat /proc/cpuinfo
	sec "/proc/meminfo";  cat /proc/meminfo

	sec "gadget (android_usb)"
	G=/sys/class/android_usb/android0
	for f in enable state functions idVendor idProduct iManufacturer iProduct iSerial \
		bDeviceClass f_acm/instances f_rndis/ethaddr f_rndis/wceis; do
		[ -r "$G/$f" ] && printf '%s: %s\n' "$f" "$(cat "$G/$f")"
	done
	ifconfig -a 2>&1

	sec "dmesg (last 150 lines)"
	dmesg | tail -150

	sec "rescue.log"
	cat /tmp/rescue.log 2>&1

	sec "ls /dev"
	ls -la /dev /dev/input /dev/graphics 2>&1
} >"$OUT" 2>&1

echo "diag written to $OUT ($(wc -c <"$OUT") bytes)"

# Save on the SD only if it can be remounted rw; revert to ro afterwards.
if [ -n "$SDMOUNTED" ] && mount -o remount,rw /mnt/sd 2>/dev/null; then
	STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo undated)
	if cp "$OUT" "/mnt/sd/y2-diag-$STAMP.txt" 2>/dev/null && sync; then
		echo "also saved to /mnt/sd/y2-diag-$STAMP.txt"
	else
		echo "copy to SD failed"
	fi
	mount -o remount,ro /mnt/sd 2>/dev/null
else
	echo "SD not writable or not present: report only in $OUT"
fi

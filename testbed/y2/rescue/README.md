# y2-rescue

A small Linux initramfs (Buildroot, musl, static busybox) that runs on the Y2's **stock 3.4.67 kernel** from the
RECOVERY partition. It brings up a USB console so the Y2 can be poked at from the bench PC. `init` never
formats anything and mounts USRDATA and the microSD read-only (`noload` for ext4) while probing. Two exceptions:
a hand-off remounts USRDATA read-write before `switch_root`, and `diag.sh` remounts the microSD read-write
briefly to save its report (it reverts to read-only).

## What it does at boot

`overlay/init` mounts /proc, /sys, a tmpfs /dev (no devtmpfs on this kernel, so busybox `mdev` fills it),
starts the in-kernel watchdog kicker (`/proc/wdk`), sets up the USB gadget (ACM + RNDIS, fallback ACM, then
mass storage), starts a shell on the ACM port, brings up the RNDIS network interface (`rndis0` on this kernel, `usb0` elsewhere; found by name or by its link
under the gadget, 10 s wait) at 192.168.7.2, then starts a DHCP server, telnet and a web server (serving `/tmp`:
`rescue.log`, `diag.txt`) in that order, so each starts only after the address exists, waits 5 s for the hold/hand-off decision, then either hands over to USRDATA or stays.

Screen progress row (top-left of the display, green block = reached; backdrop is 8 colour bars):

| Blocks lit | Stage |
| --- | --- |
| 1 | proc/sys/dev mounted |
| 2 | mdev populated /dev |
| 3 | USB gadget configured (see the log for what it read back) |
| 4 | shell on the USB serial port |
| 5 | RNDIS interface up with 192.168.7.2 |

A solid red screen means PID 1 hit a fatal error; a shell is then left on the kernel console (UART).

## Build

```
testbed/y2/rescue/build-rescue.sh        # ~20-40 min first time, minutes afterwards
```

Output goes to `~/y2-build/` (outside the repo): `y2-rescue-recovery.img` (stock RECOVERY kernel + this ramdisk,
the one to flash) and `y2-rescue-boot.img` (same on the BOOTIMG layout, for later). Flashing is separate and
human-confirmed: `scripts/flash-recovery.sh ~/y2-build/y2-rescue-recovery.img` (never from this script).
Tests: `python3 -I -m unittest discover -s testbed/y2/tools/tests -v` (output checks skip when `~/y2-build` is absent).

## Getting in: boot-rescue.sh (the entry path)

LK on this unit has **no recovery or fastboot key combination** (the key checks are compiled out), so there is
no button route into RECOVERY. The way in is software only, and writes nothing to flash:

```
testbed/y2/scripts/boot-rescue.sh
```

1. Arms the preloader handshake (`mtk meta FASTBOOT`, from a fresh temp dir). Do as it says: power the Y2 off
   (hold power 10 s), unplug, wait 5 s, plug in with no buttons.
2. Waits up to 30 s for LK fastboot (`0bb4:0c01`), then `tools/lkboot.py reboot-recovery` (`oem reboot-recovery`:
   LK answers OKAY and boots the RECOVERY partition).
3. Waits up to 60 s for the rescue gadget (`18d1:4ee7`), then for the PC to get `192.168.7.1` by DHCP, and
   prints the tail of `http://192.168.7.2/rescue.log`.

It resumes from wherever the device is (already in fastboot or already in rescue skips the earlier steps) and
logs to `~/y2-backup/flash-logs/boot-rescue-<timestamp>.log`.

`tools/lkboot.py` is a tiny raw fastboot client with a fixed allowlist (`getvar NAME`, `boot IMAGE`, `reboot`,
`continue`, `reboot-recovery`); it has no flash, erase or free-form oem command. `boot` (RAM boot) is
unproven here: after `download` this LK stayed in fastboot and never replied.

## Connect

```
testbed/y2/scripts/usb-console.sh            # serial console (picocom); leave with Ctrl-A Ctrl-X
testbed/y2/scripts/usb-console.sh --telnet   # network shell instead
```

The device runs `udhcpd` on its RNDIS interface (template `overlay/etc/udhcpd.conf`, interface name filled in at
boot, leases in `/tmp`) and offers exactly `192.168.7.1/24`, no router or DNS. The PC's NetworkManager therefore
configures itself, no sudo. `usb-console.sh` waits for that (15 s) and only then falls back to `nmcli` or prints
`sudo ip` commands. Serial port: `/dev/serial/by-id/usb-neuPod_y2-rescue*`. Logs: `/tmp/rescue.log` on the device,
or `http://192.168.7.2/rescue.log` and `/diag.txt` from the PC. Run `diag.sh` in the shell for a full hardware
report (`diag.sh --no-capture`, alias `--no-input`, skips the 10 s per-input-device capture); it is saved to
`/tmp/diag.txt` and, if the microSD is writable, `/mnt/sd/y2-diag-<date>.txt`.

## Hardware facts (measured under this image)

- **Network:** the RNDIS interface is `rndis0`, not `usb0`.
- **Framebuffer** (`mtkfb`): 480x360, 32 bpp XRGB8888 (red at bit 16, green 8, blue 0, unused 24, 8 bits each),
  stride 1920 bytes, virtual 480x1104. `fbtool` pans and draws correctly (colour bars verified).
- **Backlight:** `/sys/class/leds/lcd-backlight/brightness`, max 255, boots at 128.
- **Headphone jack:** `/sys/class/switch/h2w/state` (0 none, 2 headphones).
- **Input:** only `event0` (`mtk-kpd`: volume, power) and `event2` (`mtk-tpd`: wheel as KEY_UP/KEY_DOWN ticks,
  centre and ring buttons) produce key events; `event1` and `event3` never do. Reads must be multiples of 16 bytes
  (`struct input_event`): busybox `hexdump` on the node fails with EINVAL, so use `dd bs=16` or `cat`.
- `/dev/kmsg` writes do nothing on this 3.4 kernel; `/tmp/rescue.log` is the log.

**Security:** telnet is an unauthenticated root shell, by design (accepted in the brief for a bench device). That is acceptable only because it is bound to
192.168.7.2 on the point-to-point USB link of a bench device. Do not put this image anywhere else.

## Hold and hand-off

After the 5 s window the init either stays in rescue (default) or switches to the system on USRDATA.
It **stays** when any of these is true:

- `y2-hold` exists in the root of the microSD (mounted read-only for the check),
- the kernel command line contains `y2.hold`,
- any key event arrives on `event0` or `event2` (volume/power, wheel/ring) during the 5 s,
- USRDATA (found by its start sector and size, not a fixed partition number) is missing, does not mount, or has no `/sbin/init`.

Otherwise it mounts USRDATA (ext4) and runs `switch_root /newroot /sbin/init`. The stock USRDATA holds Android
data and no `/sbin/init`, so today it always stays. After the shell is up, PID 1 is busybox `init`, which reaps
zombie processes, handles `reboot`/`poweroff` and keeps a shell on the kernel console.

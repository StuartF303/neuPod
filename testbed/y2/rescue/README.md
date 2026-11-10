# y2-rescue

A small Linux initramfs (Buildroot, musl, static busybox) that runs on the Y2's **stock 3.4.67 kernel** from the
RECOVERY partition. It brings up a USB console so the Y2 can be poked at from the bench PC. `init` never
formats anything and mounts USRDATA and the microSD read-only (`noload` for ext4) while probing. Two exceptions:
a hand-off remounts USRDATA read-write before `switch_root`, and `diag.sh` remounts the microSD read-write
briefly to save its report (it reverts to read-only).

## What it does at boot

`overlay/init` mounts /proc, /sys, a tmpfs /dev (no devtmpfs on this kernel, so busybox `mdev` fills it),
starts the in-kernel watchdog kicker (`/proc/wdk`), sets up the USB gadget (ACM + RNDIS, fallback ACM, then
mass storage), starts a shell on the ACM port, brings up `usb0` (192.168.7.2) with telnet and a tiny web
server for the logs, waits 5 s for the hold/hand-off decision, then either hands over to USRDATA or stays.

Screen progress row (top-left of the display, green block = reached; backdrop is 8 colour bars):

| Blocks lit | Stage |
| --- | --- |
| 1 | proc/sys/dev mounted |
| 2 | mdev populated /dev |
| 3 | USB gadget configured (see the log for what it read back) |
| 4 | shell on the USB serial port |
| 5 | `usb0` up with 192.168.7.2 |

A solid red screen means PID 1 hit a fatal error; a shell is then left on the kernel console (UART).

## Build

```
testbed/y2/rescue/build-rescue.sh        # ~20-40 min first time, minutes afterwards
```

Output goes to `~/y2-build/` (outside the repo): `y2-rescue-recovery.img` (stock RECOVERY kernel + this ramdisk,
the one to flash) and `y2-rescue-boot.img` (same on the BOOTIMG layout, for later). Flashing is separate and
human-confirmed: `scripts/flash-recovery.sh ~/y2-build/y2-rescue-recovery.img` (never from this script).
Tests: `python3 -I -m unittest discover -s testbed/y2/tools/tests -v` (output checks skip when `~/y2-build` is absent).

## Connect

```
testbed/y2/scripts/usb-console.sh            # serial console (picocom); leave with Ctrl-A Ctrl-X
testbed/y2/scripts/usb-console.sh --telnet   # network shell instead
```

The script waits for the USB device `18d1:4ee7`, sets the PC side of the link to 192.168.7.1/24 via NetworkManager
(or prints the `sudo ip` commands), then connects. Logs: `/tmp/rescue.log` on the device, or
`http://192.168.7.2/rescue.log` and `/diag.txt` from the PC. Run `diag.sh` in the shell for a full hardware
report (`diag.sh --no-capture` skips the 10 s per-input-device capture); it is saved to `/tmp/diag.txt` and, if
the microSD is writable, `/mnt/sd/y2-diag-<date>.txt`.

**Security:** telnet is an unauthenticated root shell, by design (accepted in the brief for a bench device). That is acceptable only because it is bound to
192.168.7.2 on the point-to-point USB link of a bench device. Do not put this image anywhere else.

## Hold and hand-off

After the 5 s window the init either stays in rescue (default) or switches to the system on USRDATA.
It **stays** when any of these is true:

- `y2-hold` exists in the root of the microSD (mounted read-only for the check),
- the kernel command line contains `y2.hold`,
- any key/wheel/touch event arrives on any input device during the 5 s,
- USRDATA (found by its start sector and size, not a fixed partition number) is missing, does not mount, or has no `/sbin/init`.

Otherwise it mounts USRDATA (ext4) and runs `switch_root /newroot /sbin/init`. The stock USRDATA holds Android
data and no `/sbin/init`, so today it always stays. After the shell is up, PID 1 is busybox `init`, which reaps
zombie processes, handles `reboot`/`poweroff` and keeps a shell on the kernel console.

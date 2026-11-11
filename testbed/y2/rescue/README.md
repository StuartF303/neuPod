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

### Screen

The backdrop is the neuPod boot splash (480x360, the firmware's own splash with its progress bar, icons and
status text removed), drawn by `fbtool image /etc/splash.y2i`. If that file is missing or fails validation nothing
is drawn and init falls back to the 8 colour bars. The booted rootfs shows the same splash from `S20display`
(plain grey if the image is unusable). A panel of another size gets the image centred (black margin) or
centre-clipped.

Progress is a row of five small dots along the bottom edge, drawn over the splash; a green dot = stage reached,
a dim grey dot = not yet (`fbtool status N` lights the first N):

| Dots lit | Stage |
| --- | --- |
| 1 | proc/sys/dev mounted |
| 2 | mdev populated /dev |
| 3 | USB gadget configured (see the log for what it read back) |
| 4 | shell on the USB serial port |
| 5 | RNDIS interface up with 192.168.7.2 |

A solid red screen means PID 1 hit a fatal error; a shell is then left on the kernel console (UART).

Splash asset and colour pipeline. Three things show this picture and they must look identical: stock LK (from the LOGO partition,
see `scripts/flash-logo.sh`), `fbtool image /etc/splash.y2i` here and in the rootfs, and the firmware's own LVGL splash.

- `splash/neupod-splash-480x360-master.png` is the 8-bit master (the PNG described below, background smoothed).
- LK can only show RGB565, and it expands a 565 pixel to the framebuffer by a **plain shift** (R = r5 << 3, G = g6 << 2, B = b5 << 3,
  low bits zero; the 32 bpp branch of LK's pixel writer, `lk.bin` of v3.2.0 at file offset 0x1c314) - from the `lk.bin` disassembly, **unverified on hardware until the first LOGO flash**, not by bit replication. So the brightest
  value LK shows is (248, 252, 248), not 255.
- `splash/neupod-splash-480x360.png` is the **canonical** splash: the master rounded once to RGB565 (`round(v * max / 255)`; the background gradient is ordered-dithered, the wordmark is not, which keeps it crisp) and expanded back by LK's shift. Every pixel has its low bits zero. A master that
  is already a bit-replicated 565 image (the host simulator's snapshots) is recovered exactly. `y2splash.py quantize` makes it.
- `overlay/etc/splash.y2i` is generated from the canonical PNG, so fbtool's output on the 32 bpp panel (it writes the 8-bit values as they
  are) equals LK's pixels. `y2logo.py replace` takes the canonical PNG and stores it as RGB565 in LOGO images 0 and 38 without any
  further rounding (it refuses a PNG that is not 565-exact unless `--quantize` is given). Tests compare fbtool's framebuffer with
  LK's expansion of the logo pixels.

Regenerate (the first line only when the master changes):

    python3 -I testbed/y2/tools/y2splash.py quantize testbed/y2/rescue/splash/neupod-splash-480x360-master.png testbed/y2/rescue/splash/neupod-splash-480x360.png
    python3 -I testbed/y2/tools/y2splash.py convert testbed/y2/rescue/splash/neupod-splash-480x360.png testbed/y2/rescue/overlay/etc/splash.y2i
    testbed/y2/scripts/build-logo.sh --out /some/dir/y2-logo.bin

Banding and dither: RGB565 has 8 levels per step in red and blue and 4 in green, so a plain rounding of the background gradient
(about (8,16,32) to (16,36,56)) shows as 9 flat bands. The canonical splash therefore applies a deterministic 4x4 ordered (Bayer) dither
to the background only: a pixel counts as background if it equals its row's left-edge pixel in the master. The wordmark and its
anti-aliased edge are not dithered (they equal the plain quantisation). The cost is size: `splash.y2i` grows from 4 KB to about 288 KB
(fbtool's limit is 1 MiB) because the runs get short. The firmware's own LVGL splash is rendered in 565 without dither and bands at rows
66, 93, 131, 180, 186, 197, 262, 278, 326, so its background differs from the canonical one by up to one 565 level in one channel.
The wordmark is identical to the firmware's, pixel for pixel in 565, and sits at the same place (x 148 to 332, y 52 to 87; LVGL top-centred at 45 px, 48 pt
Montserrat). The firmware splash adds a shimmer track, icon row, caption and version line, which the canonical splash leaves out; its
top and bottom colours are the same. On the real panel the firmware's 565 would be expanded by whatever the Y2 display path does
(replication gives white 239/247 instead of LK's 232/240); that driver is not written yet. If the firmware ever needs the pixels to match LK exactly, its
565 to 888 step has to be the same plain shift.

The master PNG came from the host simulator on the `feat/ui-resolution` branch (`NEUPOD_DISPLAY_SIZE=480x360`,
`--snapshots`, `splash.ppm`). The boot checks fail on the host (no battery gauge, wheel or haptics), so the
dynamic parts were removed and only the gradient and the neuPod wordmark kept. The captured gradient is stepped (a visible band
about halfway down), so the background was then replaced by one linear top-to-bottom ramp between the captured top and bottom
colours (`y2splash.py smooth`); the wordmark pixels are unchanged.
`fbtool check FILE [OUT.ppm]` validates an image without a panel (exit 3 on a bad file).

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
  stride 1920 bytes, virtual 480x1104. `fbtool` pans and draws correctly (colour bars verified; the splash image and progress dots are host-tested only, not yet seen on the panel).
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

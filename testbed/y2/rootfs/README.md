# y2 rootfs (T1.4)

A Buildroot **glibc** root filesystem for the Y2's USRDATA partition, booted on the **stock 3.4.67 kernel** via the
y2-rescue hand-off (rescue mounts USRDATA ext4 read-only, checks for `/sbin/init`, `/dev`, `/proc`, `/sys`, remounts
it read-write, moves its populated `/dev` `/proc` `/sys` in and `switch_root`s). glibc is there for .NET.

```
testbed/y2/rootfs/build-rootfs.sh     # first build is long (toolchain + packages); reruns are incremental
python3 -I -m unittest discover -s testbed/y2/tools/tests -p test_rootfs.py -v
```

Everything lives outside the repo under `~/y2-build` (`Y2_BUILD` overrides): Buildroot tree and `dl/` shared with the
rescue build (the pinned version and sha256 are read from `rescue/build-rescue.sh`), output in `out-rootfs/`, final
image `y2-rootfs.ext4` plus `y2-rootfs.ext4.sha256`. Flashing is a separate, human-confirmed step (not in this script).

## Image

- ext4, label `y2root`, 512 MiB (`BR2_TARGET_ROOTFS_EXT2_SIZE`). LK reports 0x31f00000 usable bytes for USRDATA
  (scatter says 0x32000000); the build and `post-image.sh` refuse anything larger.
- mke2fs options `-O ^metadata_csum,^64bit,^orphan_file,^metadata_csum_seed,uninit_bg`. Why each:
  - `^metadata_csum`: needs kernel 3.6+ (the mke2fs default turns it on); 3.4 refuses to mount it.
  - `^orphan_file`: needs kernel 6.1+ (default in e2fsprogs 1.47.x).
  - `^metadata_csum_seed`: needs 4.4+; only meaningful with metadata_csum anyway.
  - `^64bit`: 3.4 can handle it, but there is no need on a sub-16 TiB filesystem and it keeps the format conservative.
  - `uninit_bg`: kept explicitly; it is the checksum feature 3.4 does support (fast fsck, lazy itable init).
  - Kept: `has_journal extent huge_file flex_bg dir_nlink extra_isize dir_index ext_attr resize_inode`, all in 3.4.
    mke2fs also records default mount options `acl,user_xattr`; the kernel ignores them if it lacks support.
- The build script asserts none of the 3.4-incompatible features are set, runs `e2fsck -fn` and checks `/sbin/init`,
  `/dev`, `/proc`, `/sys` via debugfs.

## Boot sequence

busybox init runs `/etc/init.d/rcS` (the `S*` scripts, in order), then a respawning getty on the USB ACM port.

| Script | Does |
| --- | --- |
| `S00dev` | mounts a tmpfs `/dev` with static nodes **only if `/dev/null` is missing** (rescue normally hands one over), arms `mdev` as hotplug helper, `mdev -s` |
| `S01mounts` | `mount -a` (proc, sys, devpts, shm, /tmp, /run), then `/run/lock` (after the mounts), hostname, `ifconfig lo up` (replaces Buildroot's `S40network`) |
| `S01seedrng` | Buildroot's; seeds the RNG (harmless) |
| `S01wdk` | `echo "0 20 30" > /proc/wdk` (watchdog kicker) |
| `S10usb-gadget` | android_usb `rndis,acm` (fallback `acm`), VID:PID `18d1:4ee8` (rescue is `4ee7`), product `y2-linux`, RNDIS MAC `02:59:32:00:00:03`; device `192.168.7.2/24` on `rndis0` or `usb0`, busybox `udhcpd` hands the host `192.168.7.1` |
| `S20display` | backlight on, `fbtool fill 202020` |
| `S50dropbear` | generates host keys into `/etc/dropbear` on first boot, runs dropbear with `-s` (no password logins) |
| `S99diag` | does nothing unless `/etc/y2-diag-on-boot` exists (absent by default); then runs `diag.sh --no-capture` in the background, output `/tmp/diag.txt` (tmpfs), never remounts the SD read-write |

`post-build.sh` removes Buildroot's `S01syslogd`, `S02klogd`, `S50crond`, `S11modules` (no modules) and `S40network`, and
`S02sysctl` unless `/etc/sysctl.conf` has content, so nothing writes logs to eMMC at boot. There is no syslog; if one
is added later use `syslogd -C64` (in-memory ring, read with `logread`) started after the mounts. The console is
`/usr/sbin/y2-console` (respawned by init): it waits for `/sys/class/tty/ttyGS0`, runs getty, then sleeps 2 s so a
failing getty cannot spin init.

Diagnostics on demand: run `diag.sh` (report in `/tmp/diag.txt`; it remounts a present microSD read-write briefly to
save a copy unless `Y2_DIAG_NO_SD=1`, e.g. `Y2_DIAG_NO_SD=1 diag.sh --no-capture`). `touch /etc/y2-diag-on-boot` to
run it each boot.

Root is never fsck'd or remounted by the rootfs. `/var/log` and `/etc/dropbear` are real directories on ext4 (Buildroot
symlinks them to tmpfs by default); `/tmp` and `/run` are tmpfs.

`diag.sh` is copied from `rescue/overlay/bin/diag.sh` at build time (`post-build.sh`), not duplicated (one line is
patched in the installed copy to honour `Y2_DIAG_NO_SD`).

## Escape hatch

After `switch_root` a PID 1 failure panics and reboots into the same hand-off, so a broken rootfs would loop. To stay
in rescue instead: hold any wheel/ring button during rescue's 5 s hold window, or put a `y2-hold` file on the microSD.

## Access and security

- **SSH**: key-only. `post-build.sh` installs `/root/.ssh/authorized_keys` from `$Y2_SSH_PUBKEY` (a pubkey file),
  else `~/.ssh/id_ed25519.pub`, else `~/.ssh/id_rsa.pub`; with none it warns and installs none. The key goes into
  the image only, never into the repo. `ssh root@192.168.7.2` once the host has `192.168.7.1`. `post-build.sh` checks the key with `ssh-keygen -l -f` and
  refuses a file containing `PRIVATE KEY`. dropbear listens on 0.0.0.0:22 (no WLAN yet); `-p 192.168.7.2:22` in
  `/etc/default/dropbear` restricts it to the USB link.
- **USB serial** (`/dev/ttyGS0`, 115200): root has no password, so the console is open to anyone with the USB cable,
  the same trust model as rescue's telnet. Set a password (or drop the getty) before the device leaves the bench.

## Environment

`/etc/profile.d/dotnet.sh`: `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` (no ICU) and `DOTNET_EnableWriteXorExecute=0`
(kernel 3.4 has no `memfd_create`). Only login shells read it: for `ssh y2 'cmd'` one-liners use
`ssh root@192.168.7.2 '. /etc/profile; cmd'`.

Kernel 3.4 vs glibc 2.42: headers are 3.4.x, so glibc itself runs (verified in a chroot on the device). glibc-internal
users of newer syscalls fall back, but applications calling them get `ENOSYS`: `getrandom`, `memfd_create`,
`renameat2`, `copy_file_range`, `epoll_pwait2`, `pidfd_*`. `arc4random` may block early in boot waiting on `/dev/random`.

**Audio:** ALSA sees no sound card on this kernel (`/proc/asound/cards` is empty; audio is MTK `/dev/eac`), so `aplay`
and mpv's ALSA output will not play. Stage 2 problem; the packages stay.

## Packages

busybox (init, mdev, udhcpd, getty, login), dropbear, util-linux (basic binaries), e2fsprogs (+resize2fs, debugfs),
exfatprogs, dosfstools, alsa-lib, alsa-utils (aplay, amixer, alsamixer, alsactl, speaker-test), tinyalsa, evtest,
i2c-tools, iw, wpa_supplicant (Stage 2 prep), ca-certificates, tzdata, zlib, libopenssl, strace, htop, file, less,
rsync, glibc utils, and `y2-fbtool`. **No bluez5 / bluez-alsa**: the stock kernel has no `CONFIG_BT` (Bluetooth is
MTK STP only), so BlueZ would find no HCI. No ICU (invariant globalization instead).

mpv (0.40, ALSA output; pulls ffmpeg, libass, libplacebo, freetype, harfbuzz) builds and is included, about 20 MiB
and untested on the device. Its DVB input is switched off (`package/y2-mpv-compat`) because it needs kernel headers
newer than 3.4. Stage 3 decides whether to keep it. rsync 3.4.3 needs a `linux/openat2.h` shim
(`package/y2-rsync-compat`); on a 3.4 kernel openat2 returns ENOSYS and rsync falls back to its slower safe path.

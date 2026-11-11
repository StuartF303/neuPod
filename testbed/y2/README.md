# Innioasis Y2 testbed

Short form of the plan doc ["neuPod — Innioasis Y2 Testbed Plan"](https://claude.ai/artifact/YS4RqFJdRaxtgXYc9pLBcc).
The Y2 (MT6582, Android 4.4, Linux 3.4.67) becomes a Linux testbed for the neuPod UI, library engine and
companion-app protocol: stock kernel and blobs kept, Android replaced by our initramfs + Buildroot rootfs.
Nothing here ships on Y2 hardware.

| Stage | Goal | Gate | Status |
| --- | --- | --- | --- |
| 0 | Recon, security fuses, full eMMC backup | G0 | in progress |
| 1 | Rescue initramfs with USB console (ACM + RNDIS), Buildroot rootfs on USRDATA, .NET smoke test | G1 | not started |
| 2 | Audio, Bluetooth, Wi-Fi, power | G2 | not started |
| 3 | neuPod UI in C# on the framebuffer | — | not started |

## Hard rules

- Never write PRELOADER, UBOOT (lk), SECCFG, NVRAM, PRO_INFO, PROTECT_F/S, SEC_RO. Only BOOTIMG, RECOVERY, USRDATA — and only after G0.
- No flash without a verified full read-back in `~/y2-backup/<date>/` **and** on the NAS; flash scripts check both.
- Scripts are idempotent. One PR per task. Findings go in `docs/` as they happen.

## Running the neuPod C firmware

The firmware itself (`NEUPOD_PLATFORM=y2`, see [docs/y2.md](../../docs/y2.md)) runs on the Y2 as a Linux process.

```
testbed/y2/scripts/deploy-fw.sh [--select] [--no-restart] [--no-build] [--dry-run]    # Y2_HOST=root@192.168.7.2 by default
testbed/y2/scripts/select-ui.sh fw|demo [--dry-run]
```

`deploy-fw.sh` cross-builds (Release, Buildroot toolchain from `~/y2-build/out-rootfs/host`, override with
`NEUPOD_Y2_TOOLCHAIN` / `NEUPOD_Y2_TRIPLE`), copies the stripped binary over ssh into `/opt/neupod-fw.new`, checks its SHA-256 on
the device, installs `/etc/init.d/S91neupod-fw` and a refreshed `S90neupod-demo`, then renames the new build into `/opt/neupod-fw`.
The previous build stays in `/opt/neupod-fw.old` until the new one has run 5 s without crashing, and is restored if it does not.
`--no-restart` only stages. It never writes anything but `/opt/neupod-fw*`, the two init scripts and `/etc/neupod-ui`, and never
a partition or the internal eMMC. A first install without `--select` leaves the screen with whatever UI it had.

Both UIs want `/dev/fb0`, so `/etc/neupod-ui` (`fw` or `demo`; missing = `demo`) says which one the init scripts start.
`--select` (or `select-ui.sh fw`) stops the demo, writes `fw` and starts the firmware; `select-ui.sh demo` switches back. If a
`--select` deploy fails, the demo is selected again. `deploy-demo.sh` only stages while `fw` is selected.

On the device: log `/tmp/neupod-fw.log` (tmpfs, emptied past 256 KB), PID files `/run/neupod-fw.pid` (respawn wrapper, 2 s pause)
and `/run/neupod-fw.app.pid`, keys from ssh with `echo s > /run/neupod.keys` (`w s` scroll, `e` select, `m` menu, `a d`, `p`).

## Layout

```
tools/     setup-host.sh, udev/51-mediatek.rules, mtk-bootimg.py (+ tests/)
scripts/   backup-all.sh, flash-*.sh, restore-stock.sh, usb-console.sh, deploy-demo.sh, deploy-fw.sh, select-ui.sh
rescue/    y2-rescue initramfs
buildroot/ external tree
recon/     captures — git-ignored, tiny + NAS only
docs/      findings, decisions, bench log — git-ignored, tiny + NAS only
```

Findings, logs and docs (`docs/`, `recon/`) are git-ignored: they live on the bench box and the NAS (`\\warrender2\public\y2-backup\notes\`), not in this public repo. Host setup: run `tools/setup-host.sh` (see its header).

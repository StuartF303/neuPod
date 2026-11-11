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

- Never write PRELOADER, UBOOT (lk), SECCFG, NVRAM, PRO_INFO, PROTECT_F/S, SEC_RO. Only BOOTIMG, RECOVERY, USRDATA and LOGO (allowed 2026-11-11) — and only after G0.
- No flash without a verified full read-back in `~/y2-backup/<date>/` **and** on the NAS; flash scripts check both.
- Scripts are idempotent. One PR per task. Findings go in `docs/` as they happen.

## Layout

```
tools/     setup-host.sh, udev/51-mediatek.rules, mtk-bootimg.py, y2flash.py, y2logo.py, y2splash.py (+ tests/)
scripts/   backup-all.sh, flash-*.sh, build-logo.sh, restore-stock.sh, usb-console.sh
rescue/    y2-rescue initramfs
buildroot/ external tree
recon/     captures — git-ignored, tiny + NAS only
docs/      findings, decisions, bench log — git-ignored, tiny + NAS only
```

Findings, logs and docs (`docs/`, `recon/`) are git-ignored: they live on the bench box and the NAS (`\\warrender2\public\y2-backup\notes\`), not in this public repo. Host setup: run `tools/setup-host.sh` (see its header).

## Flash scripts

Every write goes through `tools/y2flash.py` and the shared guards in `scripts/lib-flash.sh`: the verified backup must exist
locally and (checksum file) on the NAS, the write calibration from `probe-write.sh` must exist, a typed confirmation word is
required, the device is pre-read and must hold the backup or a known earlier result, a 4 KiB canary proves the write address,
the image is written, read back and compared, and every attempt is logged. `--dry-run` (on every script) runs the guards and
prints the plan without connecting; environment overrides are honoured only with it. A failed or interrupted write prints
the matching restore command.

| Script | Partition | Typed word | Restore |
| --- | --- | --- | --- |
| `flash-boot.sh` | BOOTIMG (`y2-rescue-boot.img`) | `BOOTIMG` | `restore-stock.sh --target BOOTIMG` |
| `flash-recovery.sh` | RECOVERY | `RECOVERY` | `restore-stock.sh --target RECOVERY` |
| `flash-rootfs.sh` | USRDATA | `DESTROY-USRDATA` (always typed) | `restore-stock.sh --target USRDATA` |
| `flash-logo.sh` | LOGO (3 MiB at scatter `0x4400000`) | `LOGO` | `restore-stock.sh --target LOGO` |

`restore-stock.sh` with no target restores BOOTIMG and RECOVERY only; LOGO and USRDATA are never part of `all`.

### Power-on logo (LOGO)

Stock LK draws the "innioasis" logo from the LOGO partition (`logo.bin`: 39 images, each zlib-compressed RGB565). Images 0
(LK power-on) and 38 (kernel boot logo, a byte-identical copy) are the 480x360 logo; image 37 is the "100 %" battery frame of the
charging animation and is **not** a logo (an earlier note said 0 and 37). The rest is the charging animation and stays
byte-identical.

    testbed/y2/scripts/build-logo.sh --out /some/dir/y2-logo.bin     # stock logo.bin + canonical splash; never overwrites without --force
    testbed/y2/scripts/flash-logo.sh --dry-run /some/dir/y2-logo.bin # guards + plan, no device
    testbed/y2/scripts/flash-logo.sh /some/dir/y2-logo.bin           # real flash (default image ~/y2-build/y2-logo.bin)

`tools/y2logo.py` has `info` (strict validation: header, counts, offsets, zlib, image sizes), `replace` and `extract`; the flash
script refuses any image that does not pass `info`, or is larger than the partition. The written image is smaller than the
partition (about 97 KB of 3 MiB): the rest of LOGO keeps its old bytes and the header's payload size ignores them. The canary for
LOGO sits 2 MiB into the partition (inside the unused tail), not at the end as for BOOTIMG/RECOVERY, because a base error of
+0xb80000 from the last sector would land in ANDROID; from 0x45ff000 it lands in RECOVERY or the last sector of EXPDB.

Colours: LK expands RGB565 by plain shift, so the canonical splash (`rescue/splash/neupod-splash-480x360.png`) is quantised once
(see `rescue/README.md`) and the LK logo, the fbtool splash and the firmware's own splash share one pixel source.

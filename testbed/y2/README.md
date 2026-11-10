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

## Layout

```
tools/     setup-host.sh, udev/51-mediatek.rules, mtk-bootimg.py (+ tests/)
scripts/   backup-all.sh, flash-*.sh, restore-stock.sh, usb-console.sh
rescue/    y2-rescue initramfs
buildroot/ external tree
recon/     captures — git-ignored, tiny + NAS only
docs/      findings, decisions, bench log — git-ignored, tiny + NAS only
```

Findings, logs and docs (`docs/`, `recon/`) are git-ignored: they live on the bench box and the NAS (`\\warrender2\public\y2-backup\notes\`), not in this public repo. Host setup: run `tools/setup-host.sh` (see its header).

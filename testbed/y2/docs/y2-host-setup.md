# Y2 testbed — host setup ("tiny")

Two steps, both idempotent:

```sh
sudo testbed/y2/tools/setup-host.sh --system   # apt packages, udev rule, plugdev/dialout groups
testbed/y2/tools/setup-host.sh                 # testbed/y2/.venv with pinned mtkclient
```

`sudo` needs a real terminal; it does not work through Claude Code's `!` prefix.

## What it installs

| Item | Detail |
| --- | --- |
| apt | `android-tools-adb android-tools-fastboot evtest picocom device-tree-compiler cpio gzip python3-pip python3-venv python3-dev libusb-1.0-0 git binwalk usbutils` |
| udev | `/etc/udev/rules.d/51-mediatek.rules` (source `tools/udev/51-mediatek.rules`; an older `60-y2-mtk.rules` is removed): `0e8d:0003` BROM, `0e8d:2000/2001` preloader/DA, `18d1:*` Google, `0bb4:*` stock Y2 gadget. Mode 0660 group `plugdev` + `uaccess`; `ID_MM_DEVICE_IGNORE=1` keeps ModemManager off the preloader CDC-ACM port. |
| venv | `testbed/y2/.venv` (git-ignored) |
| mtkclient | Not on PyPI. Installed from `github.com/bkerler/mtkclient` tag `v2.1.4.1`, commit `5a863eece86fcaa97cb8325cf747e0aae3c307e4`. Entry point `testbed/y2/.venv/bin/mtk`. Stamp file `.venv/.mtkclient-commit` prevents reinstall. |

Bump mtkclient by editing `MTKCLIENT_TAG`/`MTKCLIENT_COMMIT` in `setup-host.sh`; the stamp mismatch triggers a reinstall.

## Firmware package

`~/y2-firmware/y2_v3.2.0_FM-20260813/` (from the Innioasis Google Drive zip, sha256
`a9e04111fc8b87c54324e7bc111f1a75d0080de3ea07fa48c82ec30933d833c0`). Per-file checksums in
`recon/firmware-v3.2.0/SHA256SUMS`. Images stay out of git.

## Boot image tool

`tools/mtk-bootimg.py info|unpack|repack` — Android v0 boot images with MTK 512-byte
`KERNEL`/`ROOTFS` headers. Tests: `python3 -I -m unittest discover -s testbed/y2/tools/tests -v`.

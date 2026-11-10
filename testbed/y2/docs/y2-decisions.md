# Y2 testbed — decisions

Where the task brief, the plan doc, or the hardware left a choice open. Safer reading wins (hard rule 6).

| Date | Decision | Why |
| --- | --- | --- |
| 2026-11-10 | mtkclient pinned to git tag `v2.1.4.1` (`5a863ee`), installed via pip from GitHub | Not published on PyPI; a commit pin is the reproducible equivalent of a pip pin. |
| 2026-11-10 | Backups go to `~/y2-backup/<date>/` + NAS, not the plan doc's `recon/dump/` | Task brief overrides the plan; keeps images out of the repo tree entirely. |
| 2026-11-10 | The plan's G0 "no-op re-write of `recovery`" is treated as a write: only after G0 is confirmed by Stuart | Brief: nothing written before G0 confirmation. |
| 2026-11-10 | `mtk-bootimg.py unpack` refuses a non-empty dir without `--force`; `--force` removes stale outputs first. `repack` rejects over-length header fields and malformed `.mtkhdr` | Review found stale files / truncation could yield a wrong but plausible image. |
| 2026-11-10 | udev rule installed as `51-mediatek.rules` (brief's deliverables name); also covers `0bb4:*` | Stock Y2 gadget enumerates as `0bb4:0001`. |
| 2026-11-10 | No ADB on stock FM_3.2.4 (no build-number entry in About) → T0.2 stock survey comes from the BROM read-back | Brief's stated fallback. |
| 2026-11-10 | `printgpt` and `rl` count as read-only even though mtkclient loads a DA into SRAM/DRAM | Nothing is written to eMMC; DA load is volatile. |

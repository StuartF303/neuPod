# Architecture decision records

Format: [Michael Nygard](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions).

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-rp2350-phase1-platform.md) | RP2350 as Phase 1 platform | Proposed |
| [0002](0002-display-st7789-spi-phase1.md) | Display: ST7789 SPI for Phase 1 | Proposed |
| [0003](0003-audio-quality-policy.md) | Audio quality policy | Proposed |
| [0004](0004-donor-ipod-classic-6g.md) | Donor platform: iPod Classic 6G (A1238) | Proposed |
| [0005](0005-firmware-architecture.md) | Firmware architecture: portable app over a HAL, host build | Proposed |
| [0006](0006-storage-microsd-sdio.md) | Storage: microSD over 4-bit SDIO, exFAT | Proposed |
| [0007](0007-bluetooth-audio-path.md) | Bluetooth audio path: BTstack or external transmitter | Proposed (updated 3 Nov: no bare CYW43439 on the final board) |
| [0008](0008-card-format.md) | Card format: exFAT with 128 KB clusters | Proposed |
| [0009](0009-rtos-freertos-smp.md) | RTOS: FreeRTOS (SMP part amended by 0011) | Accepted, amended |
| [0010](0010-ui-lvgl.md) | UI toolkit: LVGL v9 | Accepted |
| [0011](0011-bare-metal-audio-core.md) | Single-core FreeRTOS on core 0, bare-metal audio core on core 1 | Accepted |
| [0012](0012-audio-codec-final-board.md) | Audio codec for the final board (ES8311 / ES8388 / PCM5102A) | Proposed (mono check open) |
| [0013](0013-bluetooth-ram-psram.md) | Bluetooth stage 2 RAM on the testbed, PSRAM on the final board | Accepted |
| [0014](0014-ota-ab-partitions.md) | OTA with bootrom A/B partitions and TBYB | Accepted |
| [0015](0015-clock-plan.md) | Clock plan | Accepted |
| [0016](0016-gapless-playback-and-play-queue.md) | Gapless playback and the play queue | Accepted |
| [0017](0017-music-library-index.md) | Music library index | Accepted |
| [0018](0018-ui-resolution-profiles.md) | UI resolution profiles: 320x240 product panel, native 480x360 for the Y2 testbed | Accepted |
| [0019](0019-bluetooth-audio-sink-boundary.md) | Bluetooth audio output behind a stack-neutral boundary (BTstack adapter, per-platform transports, UI) | Accepted |

Every feature ADR carries a **RAM budget** line in its Consequences (from 3 Nov 2026).

# neuPod

neuPod is a drop-in replacement logic board for the Apple iPod Classic 6G (A1238). It reuses the
original shell, click wheel and (optionally) screen, and adds Wi-Fi, Bluetooth, microSD storage
and USB-C. The firmware is written from scratch.

Two compute platforms are evaluated side by side from one source tree:

- **Raspberry Pi Pico 2 W / RP2350** — primary, Phase 1 testbed in progress
- **ESP32-S3** — comparison

## Documents

| Page | Contents |
|------|----------|
| [Requirements](requirements.md) | What neuPod must do (exported from the Claude design doc) |
| [Design](design.md) | How it does it (exported from the Claude design doc) |
| [Reference](reference.md) | Datasheets, protocols, prior art (exported from the Claude design doc) |
| [UI mock-ups](ui.md) | Screen designs at 320×240 and click-wheel interaction notes |
| [Phase 1 wiring](wiring-phase1.md) | Pico 2 W breadboard pin plan |
| [Click-wheel protocol](click-wheel-protocol.md) | iPod 6G click-wheel serial protocol |
| [Architecture decisions](adr/index.md) | ADRs |

Source: <https://github.com/StuartF303/neuPod>

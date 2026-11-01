# neuPod

Replacement logic board for the iPod Classic 6G, built around the Raspberry Pi Pico 2 W
(RP2350 + CYW43439).

Pin plan: see the neuPod design doc in Claude (source of truth for pin assignments).

## Layout

- `app/` — firmware entry point (`neupod` executable)
- `hal/rp2350/` — `hal_rp2350` hardware abstraction (target)
- `hal/host/` — host-side HAL for tests
- `tests/`, `tools/`, `docs/`, `boards/`
- `scripts/` — `flash.sh` (OpenOCD via Debug Probe), `console.sh` (UART via minicom)

## Build

    mkdir build && cd build
    cmake -G Ninja ..
    ninja

Requires `PICO_SDK_PATH` (SDK >= 2.1.0). Board `pico2_w`, platform `rp2350-arm-s`.
Console: UART0 (GP0/GP1) at 115200 via the Debug Probe.

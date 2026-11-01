# Contributing to neuPod

## Branches

Work on short-lived branches off `main`:

| Prefix | Use |
|--------|-----|
| `feat/<topic>` | new functionality |
| `fix/<topic>` | bug fixes |
| `docs/<topic>` | documentation only |
| `hw/<topic>` | hardware (`boards/`) changes |
| `chore/<topic>` | tooling, CI, refactors with no behaviour change |

## Commits

Use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/):

    <type>(<scope>): <summary>

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, `chore`, `perf`.
Scopes are typically a platform or subsystem: `rp2350`, `esp32s3`, `host`, `display`,
`input`, `audio`, `storage`, `docs`, `ci`.

## Building

Each platform has its own build directory; they do not interfere.

### RP2350 (Pico 2 W) — primary

Requires `arm-none-eabi-gcc`, CMake ≥ 3.13, Ninja and Pico SDK ≥ 2.1 with `PICO_SDK_PATH` set.

    scripts/build.sh rp2350          # -> build/rp2350/
    scripts/flash.sh                 # OpenOCD via Debug Probe
    scripts/console.sh               # UART0 @ 115200

### ESP32-S3 — comparison

Requires ESP-IDF ≥ 5.3 (`scripts/setup-esp-idf.sh` installs it to `~/esp/esp-idf`).

    . ~/esp/esp-idf/export.sh
    scripts/build.sh esp32s3

### Host (desktop simulator + unit tests)

Requires a native C compiler; SDL2 development headers enable the simulator window.

    scripts/build.sh host
    ctest --test-dir build/host --output-on-failure

## Code style

- C11, 4-space indent, 100 columns — run `clang-format` (config in `.clang-format`).
- `app/` and `hal/include/` must stay portable: no SDK headers, no platform `#ifdef`s.
- Platform code lives under `platforms/<name>/` (or `hal/host/` for the simulator).

## Pull request checklist

- [ ] Branch name and commit messages follow the conventions above
- [ ] `scripts/build.sh rp2350` builds cleanly
- [ ] Host tests pass (`ctest --test-dir build/host`)
- [ ] `clang-format` applied to changed C files
- [ ] Docs / `CHANGELOG.md` (Unreleased) updated where behaviour changes
- [ ] Hardware facts (pins, parts, measurements) are sourced — no guesses; unknowns marked `TODO`
- [ ] Tested on hardware? State the board and what was verified, or "not hardware-tested"

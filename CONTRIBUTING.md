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

## CI toolchain image

The `rp2350`, `host` and `y2` jobs in `.github/workflows/firmware.yml` run inside `ghcr.io/stuartf303/neupod-ci`
(`.github/ci/Dockerfile`): Ubuntu 24.04 (pinned by digest), the compilers, CMake, Ninja, SDL2 and the Pico SDK at
`/opt/pico-sdk` (`PICO_SDK_PATH` is set). They run no `apt`, and use the same compilers every time, which the rp2350
size budgets and the core-1 closure check depend on. Each job prints `/etc/neupod-ci-versions`. The `esp32s3` job uses
Espressif's own image.

**Changing the toolchain or the Pico SDK version** (edit `.github/ci/Dockerfile`, including `ARG PICO_SDK_VERSION`):

1. Merge the change to `main`. `ci-image.yml` builds and pushes the image (also: Actions > ci-image > Run workflow;
   it also rebuilds monthly for Ubuntu updates). The run summary shows the new digest.
2. In a second PR, replace the `sha256:` digest in the three `image:` lines of `firmware.yml` (they must match).
   The PR's own CI then runs on the new image. The image must be public (package settings on GitHub) or the jobs
   cannot pull it.

**Run CI locally** (from the repo root; the first command may need `docker login ghcr.io`):

    IMG=ghcr.io/stuartf303/neupod-ci:latest
    docker run --rm -v "$PWD":/work -w /work "$IMG" bash -c '
      git submodule update --init lib/FreeRTOS-Kernel lib/lvgl lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico &&
      scripts/build.sh host --clean -- -DNEUPOD_REQUIRE_CRYPTO=ON &&
      ctest --test-dir build/host --output-on-failure -j3 &&
      scripts/build.sh rp2350 --clean -- -DNEUPOD_REQUIRE_CRYPTO=ON'

Build the image yourself with `docker build -f .github/ci/Dockerfile -t neupod-ci:local .`.

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

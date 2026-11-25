# Contracts: build option, HAL call, console lines

The firmware has no external API for this feature. Three things are interfaces other people and scripts rely on.

## 1. Build option

| Option | Where | Default | Effect |
|---|---|---|---|
| `NEUPOD_ART_HIRES` | top-level `CMakeLists.txt` (host, Y2, RP2350 boards) and `platforms/esp32s3/CMakeLists.txt` (IDF) | ON for host, Y2, `rp2350-amoled241`, `esp32s3`; OFF for the Pico 2 W | the big-RAM player (research R2) |
| `NEUPOD_PSRAM_SECTION` | `platforms/rp2350/CMakeLists.txt`, not a user option | defined for `rp2350-amoled241` only | `NEUPOD_PSRAM_BSS` places variables in PSRAM; the PSRAM boot check is compiled in |

`scripts/build.sh rp2350-amoled241 -- -DNEUPOD_ART_HIRES=OFF` (new build directory or `--clean`) gives today's image, for
comparisons. Turning `NEUPOD_ART_HIRES` on for the Pico 2 W is not supported (it does not fit).

## 2. HAL call (RP2350 AMOLED only)

```c
/* hal/include/hal_system.h, declared only #ifdef NEUPOD_PSRAM_SECTION */
bool hal_psram_ok(void);           /* true once PSRAM passed its start-up check and its section is zeroed */
const char *hal_psram_reason(void); /* "", or why it failed: "no chip", "too small", "timing" */
```

Called by the runtime's boot before any service starts. Not present on any other platform (the runtime does not call it
there), so no other HAL needs a stub.

## 3. Console lines (bench scripts and the owner read these)

RP2350 AMOLED, once at start-up, after the version line:

```
psram: 8192 KB at 62500 kHz, neupod section 1257 KB
```

On failure, every 2 s until reset:

```
boot: psram FAILED: no chip
```

ESP32-S3: the IDF's own PSRAM lines are unchanged; the firmware adds the same `psram:` summary from
`esp_psram_get_size()` and the linker symbols of `extram_bss` (`_ext_ram_bss_start`, `_ext_ram_bss_end`), so both boards report
the same way (FR-008).

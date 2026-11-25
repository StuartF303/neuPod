# Data model: Full album art on the AMOLED dev boards

No new data structures: the feature moves existing ones and changes where they live. This file records what they are, so
the budget in [plan.md](plan.md) can be checked against them.

## Album-art cache slot (`art_slot_t`, `app/runtime/art_service.c`, unchanged)

| Field | Size (32-bit) | Meaning |
|---|---|---|
| `key` (`folder`, `album`, `emb_len`, `emb_kind`) | 16 B | which album's picture (the existing album key) |
| `used`, `w`, `h` | 6 B (+2 padding) | slot filled; thumbnail size (at most 160 x 160) |
| `stamp`, `ver` | 8 B | last use (LRU order); fill counter (the UI's "same picture" test) |
| `handle` | 24 B | library handle of a browse request, "" for the playing track |
| `px` | 51,200 B at 160 px (8,192 B at 64 px) | RGB565 pixels, CPU byte order |

24 slots = **1,230,144 B** (`ART_CACHE_N` 24, `ART_MAX_DIM` 160). Rules (unchanged): least recently used replaced first; the
playing track's slot, the one before it, and the browse handles wanted now and just before are never replaced while another
slot can be taken. One writer (storage task), one reader (UI task), sequence counters for consistency.

**New constraint**: on the RP2350 AMOLED board the array lives in `.psram_noload` and is zeroed by the HAL before the
runtime starts (the C zero-initialisation the code relies on: `used == false`). On the ESP32-S3 it lives in `extram_bss`,
which the IDF zeroes.

## Other moved buffers

| Buffer | Owner | Size at `NEUPOD_ART_HIRES` | ESP32-S3 | RP2350 AMOLED |
|---|---|---|---|---|
| Playlist pool, file paths, names, rows | `playlist_service.c` | about 23.9 KB | PSRAM (object) | PSRAM (marked) |
| Plays table + inbox | `plays_store.c` | about 24.6 KB | PSRAM (object) | PSRAM (marked) |
| Play-queue name pool | `play_queue.c` | 8,192 B | PSRAM (object) | PSRAM (marked) |
| Ratings table + file image | `ratings_store.c` | about 2.2 KB | internal | internal |
| Art browse state | `art_service.c` | about 0.7 KB | PSRAM (object) | internal |

## Board memory map (entity from the spec)

Per board: region (internal SRAM / PSRAM), what is placed there, and its budget. The authoritative tables are in
[plan.md section 5](plan.md#5-ram-budget-per-board-constitution-iv) and, after the work, in each board's doc page.

## PSRAM state (RP2350 AMOLED, new, `platforms/rp2350/boards/amoled241/psram_amoled.c`)

| State | Set when | Effect |
|---|---|---|
| unknown | reset | nothing may touch `.psram_noload` |
| OK | the chip answered its ID, the size covers `__psram_end__`, re-timed for 125 MHz, section zeroed | `hal_psram_ok()` true; boot continues |
| FAILED (reason: no chip / too small / re-time error) | any check fails | `hal_psram_ok()` false; boot stops with the message (FR-007) |

Transitions: unknown -> OK or FAILED once, in `hal_system_init()`; never changes afterwards.

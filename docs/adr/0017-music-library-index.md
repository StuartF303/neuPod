# ADR-0017: Music library index

Status: **proposed** (design note, 4 Nov 2026; decisions D1–D6 below are open)

## Context

Music > Artists / Albums / Songs / Genres and Shuffle Songs are placeholders (`app/ui/ui_screens.c:140-177`). Playback today
comes only from Extras > Files, with the ADR-0016 folder queue. Requirements: browse stays fast with 100k+ tracks, with the
index cached in PSRAM or on the card (`docs/requirements.md`, Storage). Constraints:

* **RAM.** Main SRAM is 512 KB and fully budgeted (`app/os/memory_budget.h`): FreeRTOS heap 80 KB, LVGL 72 KB, codec arena
  99 KB, PCM ring 32 KB. PSRAM (U1 on the Waveshare board, APS6404L) is not fitted yet, and the design must not depend on it.
* **Card.** exFAT. The card is written by Finder, Explorer and Linux, so the timestamps on directories can't be trusted to
  flag changes inside them. Every file I/O goes through the storage task (`docs/runtime.md`). Playback streaming takes
  priority over everything else on the card.
* **Tags.** `audio_tags_t` (`app/include/meta.h`) has title, artist, album and track number only. A library also needs
  album artist, genre, disc, year, the compilation flag and the sort tags.
* **UI.** `ui_list` already pulls rows from a model on demand (`ui_list_model_t` with count and get). A 100k-row list
  creates only the visible rows, so the index has to answer "row *i* of list *L*" cheaply. It does not need to fit in RAM.

## Proposal

### On-card index, immutable, rebuilt as a set

The index lives in `/.neupod/lib/`. Every file is written once per build and never edited in place. Files are
little-endian, with fixed-size records, and each one starts with `magic, version, count, crc32`.

| File | Record | Content |
| --- | --- | --- |
| `manifest.bin` | 1 | generation, the card fingerprint (D3), counts, CRC of each file, letter-jump tables. **Written last.** A build without a valid manifest doesn't exist |
| `dirs.bin` + `names.str` | 8 B | directory table (parent, name offset), so a path costs a dir id plus a file name rather than the full string |
| `tracks.bin` | 32 B | dir id, name offset, title offset, artist / album / genre ids, track, disc, year, duration (s), format/rate/bits code, file size, mtime |
| `strings.str` | var | deduplicated UTF-8 string pool (titles, artist, album and genre names) |
| `artists.bin`, `albums.bin`, `genres.bin` | 16 B | sorted: name offset, an 8-byte folded sort-key prefix, first child, child count |
| `by_album.u32`, `by_title.u32`, `genre_artists.u32` | 4 B | track or artist ids in display order: album = (disc, track, title); Songs = title |

The sort orders let each browse level be a contiguous range: artist → its albums → their tracks. Each list screen therefore
holds just (table, first, count), and row *i* is one 16 or 32 B record plus one string read.

**Estimated size at 100k tracks:** about 3.2 MB of tracks, about 3 MB of strings, about 1.5 MB of paths and about 1 MB of
indexes, so **about 9 MB** in total, which is nothing on the card.

**Atomic rebuild.** The scanner writes into `/.neupod/lib.tmp/`, syncs every file, writes the manifest last, then renames
`lib` → `lib.old`, renames `lib.tmp` → `lib`, and deletes `lib.old`. At boot, if `lib` is missing or its manifest is bad and
`lib.old` is valid, it renames `lib.old` back. If power is lost at any point, you keep the old library or get the new one,
never a mix. The writes use the same write-lock path as the settings store (`docs/bluetooth.md`).

### Reader (browse)

* The reader keeps one `FIL` per open table, with `FF_USE_FASTSEEK` already on, and a small LRU page cache of 4 × 2 KB pages
  covering records and strings. The visible screen (10 rows) and the scroll window fit in it.
* Fast-scroll letter jumps (`docs/ui.md`, Library) use the 27-entry tables in the manifest, with no search.
* With PSRAM fitted, the same cache is simply larger (for example 1 MB), and nothing else changes.
* Reads go through the storage task at a lower priority than stream reads, so browsing never starves playback.

### Scanner (build)

1. **Walk** `/Music`, or every root folder except `/.neupod`; TBD with D1. For each audio file, match it against the
   previous `tracks.bin` sorted by (dir, name). If size and mtime are unchanged, copy the old record. Otherwise parse it
   with the existing `*_parse` functions, after **extending `audio_tags_t`**: TPE2 / aART / ALBUMARTIST, TCON (including
   ID3v1 `(n)` genre references) / ©gen / gnre / GENRE, TPOS / disk / DISCNUMBER, year, TCMP / cpil / COMPILATION, and the
   TSOP / TSO2 / TSOA / TSOT, soar / soaa / sonm and *SORT sort tags. Artwork is unchanged (`art_ref_t`, not indexed).
2. **Intern** strings without an in-RAM hash table, which would need about 160 KB at 100k tracks (about 20k distinct names at
   8 B each). Strings spill to sorted runs on the card instead, and a merge removes the duplicates and assigns the ids.
3. **Sort.** An external merge sort on keys of (12-byte folded prefix + 4-byte id), with 32 KB runs. Ties beyond the prefix
   compare the full strings from the pool, which is rare. **Collation:** ASCII case-folded; Latin-1 and Latin Extended-A
   diacritics folded through a small table; leading "The " ignored, as the iPod does; sort tags win when present; digits
   come before letters; everything else sorts by code point under "#".
4. **Write** the tables, then the manifest, then swap (see above).

**Estimated cost, to be measured on the bench:** the walk takes about 0.2–0.4 ms per directory entry. Parsing a new or
changed file takes about 10–20 ms: open, read the head (pictures are skipped by seeking), and for MP4 walk to `moov`. A
first scan of 5k tracks takes about 1.5 min, of 100k tracks about 25–35 min. A rescan after adding one album is the walk
plus a few files, so seconds for 5k tracks and under a minute for 100k.

**Scanner RAM:** the sort and intern buffers (32 KB) are borrowed from the **codec arena**, which is free while nothing is
playing; the tag scratch is about 1 KB. That gives a static `.bss` cost of zero for the scanner, and is why D2 matters.

### Grouping rules

* Albums are keyed by (album, album artist). If there is no album artist, the artist is used, except for compilations,
  where the key is (album, "Compilations"). One album spread over several folders stays one album, and two albums called
  "Greatest Hits" by different artists stay apart.
* Artists lists album artists (D4). Compilations gets its own Music menu entry, as on the iPod.
* With no album tag, the album is the folder name (D5). With no artist, it's "Unknown Artist". With no title, it's the file
  name, as Now Playing does today.

### Playing from the library

Choosing a song queues its range: the album, the artist's tracks, all songs or the genre. That's a range in one of the
order tables, so the queue becomes **(table, first, count, position)** instead of a list of names. ADR-0016 gapless and
next/previous are unchanged; `play_queue.c` gets a "range source" alongside the folder source.

Library-wide shuffle (Shuffle Songs over 100k) can't store a permutation: 400 KB of ids. It uses a stateless keyed
permutation over [0, count) instead: a 4-round Feistel network on the next power of two, re-applied until the result is in
range (cycle-walking). That's O(1) RAM, reproducible from a seed saved with resume, and visits every song exactly once.
The `feat/shuffle-repeat` index-array shuffle stays for folder queues of up to 128 tracks.

## Decisions needed

| # | Question | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | Who builds the index | (a) the device only; (b) the device, plus an optional PC tool (Python) that writes the same format while the card is in the PC | **(a) now**, (b) later if first scans of large libraries are too slow. The device is the source of truth either way, and still validates the manifest |
| D2 | When the scanner runs | (a) only when nothing is playing, with a progress screen ("Updating library… 1,234 / 5,000"), cancelled by Play, resumed later; (b) in the background during playback (needs its own ~32 KB of RAM and competes for the card) | **(a)**. It needs no RAM and can't put playback at risk; (b) becomes realistic once PSRAM is fitted |
| D3 | How changes are noticed at boot | (a) full walk every boot (always correct, tens of seconds at 100k); (b) fingerprint: volume serial number plus free-cluster count (changes on almost any write), and walk only if it differs; (c) manual "Update library" only | **(b) plus a manual Settings > Update library** that forces a walk. A false "unchanged" needs an exactly zero-size-change write, which a card in a PC almost never produces |
| D4 | Artists list | (a) album artists, with compilations under their own menu (iPod); (b) every track artist | **(a)** |
| D5 | Missing album tag | (a) folder name; (b) "Unknown Album" | **(a)**: libraries organised by folder still browse sensibly |
| D6 | Files the device can't play (AAC, more than 2 channels, 32-bit ALAC, bad headers) | (a) leave them out of the library, show them in Files, and give a count with reasons in Settings > About; (b) list them greyed out | **(a)** |

## Consequences

* **RAM budget:** reader about 10 KB (2 KB of open `FIL`s plus 8 KB of page cache) from the FreeRTOS heap, so check the heap
  margin (runtime monitor) before merging. Scanner: 0 B static, 32 KB borrowed from the codec arena while idle. Library
  queue: 16 B instead of a 3 KB name pool. Flash: about 12–15 KB for the scanner, reader and collation table (estimate).
* Phasing, one PR each: **L1** tag extension + walker + index writer + reader, host-tested on a FatFs disk image with a
  synthetic tree of 100k entries; **L2** the Artists / Albums / Songs / Genres / Compilations screens, letter jump, play
  from library; **L3** incremental rescan, fingerprint and progress UI; **L4** M3U/M3U8 playlists, Shuffle Songs (Feistel),
  album browser.
* The Files browser and the folder queue stay as they are, as the fallback while the index is missing or being built.

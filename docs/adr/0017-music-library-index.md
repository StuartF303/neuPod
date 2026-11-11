# ADR-0017: Music library index

Status: **accepted** (design note 4 Nov 2026; decisions D1–D6 taken 4 Nov 2026, see Decisions)

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
| `tracks.bin` | 32 B | dir id, name offset, title offset, artist / album / genre ids, track, disc, year, duration (s), format/rate/bits code, playable code (0 = playable, else the reason: D6), file size, mtime |
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

## Decisions

Taken 4 Nov 2026.

| # | Question | Decision | Rejected |
| --- | --- | --- | --- |
| D1 | Who builds the index | **The device only.** The device is the source of truth and validates the manifest. A PC tool writing the same format stays possible later if first scans of large libraries prove too slow | A device plus PC tool now (two implementations of tags, collation and "playable" that would drift) |
| D2 | When the scanner runs | **Only when nothing is playing**, with a progress screen ("Updating library… 1,234 / 5,000"), cancelled by Play and resumed later. It borrows the codec arena, so it needs no RAM of its own and cannot cause underruns. Revisit once PSRAM is fitted | In the background during playback (~32 KB more RAM, competes with the stream pump for the card) |
| D3 | How changes are noticed at boot | **Fingerprint** (volume serial number plus free-cluster count; walk only if it differs) **plus a manual Settings > Update library** that forces a walk. A miss needs a write that changes no cluster count (for example an in-place retag inside the tag padding) | A full walk every boot (tens of seconds at 100k); manual only |
| D4 | Artists list | **Album artists**, falling back to the track artist; compilations (`TCMP` / `cpil`, or album artist "Various Artists") under their own **Compilations** menu, as on the iPod | Every track artist |
| D5 | Missing album tag | **The folder name** as the album, so libraries organised by folder still browse sensibly | "Unknown Album" |
| D6 | Files the device can't play (AAC, more than 2 channels, 32-bit ALAC, bad headers) | **Listed, greyed out.** See the D6 notes below | Leaving them out, with a count in About |

### D6 notes

* The scanner indexes every audio file it recognises by extension or header and stores a **playable code** in the track
  record (0 = playable; otherwise codec unsupported, too many channels, unsupported bit depth, damaged header). Their
  tags are parsed as normal, so they sort, group and count like any other track.
* Rows for them are drawn greyed out. Select shows the reason ("Can't play: AAC") instead of opening the track.
* Queues skip them: auto-advance and Next / Previous move to the next playable record in the order table, and Shuffle
  Songs steps the Feistel permutation on past them, just as it already cycle-walks past ids out of range (one 32 B record
  read per skipped id, through the page cache). Every skip loop is bounded by the queue length, so a queue with no playable
  record stops with "Nothing playable here".
* Counts (Artists > *n* songs, About) show playable and total ("1,204 songs, 37 can't play").
* The manifest records a **decoder-set version** (bumped by firmware that adds a codec or lifts a limit). On a mismatch the
  next idle scan re-checks only the records with a non-zero playable code, so new codec support un-greys tracks without a
  full rebuild.

## Consequences

* **RAM budget:** reader about 10 KB (2 KB of open `FIL`s plus 8 KB of page cache) from the FreeRTOS heap, so check the heap
  margin (runtime monitor) before merging. Scanner: 0 B static, 32 KB borrowed from the codec arena while idle. Library
  queue: 16 B instead of a 3 KB name pool. Flash: about 12–15 KB for the scanner, reader and collation table (estimate).
* Phasing, one PR each: **L1** tag extension + walker + index writer + reader, host-tested on a FatFs disk image with a
  synthetic tree of 100k entries; **L2** the Artists / Albums / Songs / Genres / Compilations screens, letter jump, play
  from library; **L3** incremental rescan, fingerprint and progress UI; **L4** M3U/M3U8 playlists, Shuffle Songs (Feistel),
  album browser.
* The Files browser and the folder queue stay as they are, as the fallback while the index is missing or being built.

## L1 as built

Stage L1 is in `app/library/` (`lib_collate.c`, `lib_store.c`, `lib_esort.c`, `lib_build.c`, `lib_reader.c`; API in `app/include/library.h`) and the extended tags in `app/audio/meta.c` / `mp4.c` (into `audio_tags_ext_t`, not `audio_tags_t`: see Measured). Tests: `ctest -R library` (collation, every new tag per format, grouping, round trip, swap crash safety at each step) and `ctest -R library_big` (100,000-file tree, label `slow`). Where the build differs from the text above:

* **Record sizes.** `tracks.bin` is 48 B, not 32 B: the 32 B list does not fit (file size and mtime for L3, three 32-bit string offsets, two 32-bit ids). `albums.bin` is 20 B (it also holds the album artist's id). Artists and genres stay 16 B. Each of the three tables has one extra sentinel row at the end whose "first child" is the total, so a row's child count is `next.first - first` and no count field is needed.
* **Two extra files.** `by_album_name.u32` (album ids in album-name order: `albums.bin` is in (artist, album) order so an artist's albums are contiguous, which the Albums list cannot use) and `enames.str` (the names of artists, albums and genres in sorted order). Without `enames.str` the names of a screenful of artists sit in a pool shared with all the titles, one page each; with it a 10-row Artists screen is one page of names and one page of records.
* **Compilations** are the tail block of `albums.bin` (`comp_first` in the manifest), sorted by album name, with no artist; their tracks have no album-artist id and are not in the Artists list or the genre-artist links.
* **Genre -> tracks.** There is no table that lists a genre's tracks (the ADR's order tables do not have one): a genre is browsed through its artists. "Play this genre" needs one more order file (L2/L4).
* **Strings.** Titles are written to the pool as they are met; artist, album and genre strings are not repeated back to back (one-entry caches), which removes almost all duplicates without a hash table. Entities are then identified by sorting, not by pool offset: two tags whose sort keys are equal (case, accents and a leading "The " ignored) are one artist / album / genre; the first spelling met is the display name.
* **Sort keys** are 24 folded bytes (the ADR said 12 + 4) with the full-string comparison from the pool only when two keys agree over all 24; `albums.bin` / `artists.bin` / `genres.bin` keep the first 8 bytes.
* **Scratch.** `lib_build()` takes the 32 KB buffer as a parameter and uses all of it: the sort fills what is free with records, and the merge carves its FIL objects from the same buffer (FatFs' sector buffer in each FIL is the merge read buffer).
* **Manifest** holds the generation, the decoder-set version, the fingerprint (supplied by the caller, D3 is L3), the counts, size and CRC of each file and the five 27-entry letter-jump tables. A library is "valid" when the manifest checks out and every file has its recorded size; `lib_manifest_load(dir, full=true)` also checks every CRC (0.1 s for 100k tracks on the host; seconds on the card, so not done at every boot).
* **Reader** is a caller-allocated `lib_reader_t` (no heap): 10 FILs kept open (with fast-seek link maps where they fit), one more for `dirs.bin` / `names.str` on demand, 4 x 2 KB LRU pages. The Songs list is random access (title order is not track order), so a 10-row Songs screen costs about 28 page reads and does not stay in 4 pages: the UI must keep its own rendered rows. Artists and Albums are contiguous and do stay cached.
* **Files recognised** are the ones the player's queue plays (`.wav .flac .mp3 .m4a .mp4`); names starting with `.` and hidden / system entries are skipped (`._file.mp3`).
* **Tracks are in walk order**, not sorted by (dir, name); L3 will have to match the old index by lookup or sort the walk.
* **Measured** (RP2350 build, 100,000-file host run; the bench numbers for scan speed on a real card are still to come): the five library files are 27.6 KB of code and tables when linked (scanner 17 KB, reader 5 KB, store 2.9 KB, sort 1.5 KB, collation 1.2 KB), above the 12-15 KB estimate; the tag extension adds 2.5 KB to the image today because the library is not linked into it yet. `audio_tags_t` is unchanged (216 B): the new tags live in `audio_tags_ext_t` (232 B), which the parsers fill only when the scanner supplies one with `meta_set_tags_ext()` (like `meta_set_scratch()`; the scanner takes it from its scratch), so the player's and track feed's copies did not grow. The scanner itself has no static RAM. Index for 100,000 tracks: 8.0 MB; host build of it 12 s with 289 sort runs and 10 merge steps; `lib_reader_t` is 16.5 KB (11 FIL objects 6.7 KB, page cache 8 KB, manifest 0.7 KB), above the 10 KB estimate.
* **Swap atomicity on exFAT.** The swap is two directory renames. FatFs does each as one directory-entry update, but on exFAT that is an entry *set* (file, stream and name entries) that can span a sector boundary, so a power cut inside a rename can leave that one directory entry damaged; the ADR's "never a mix" holds between the renames and for a cut between sectors, not for a tear inside the rename itself. `lib_recover()` treats a damaged entry like a missing library (manifest not readable) and falls back to the other copy, so the worst case is the library being rebuilt, but a card whose directory entry is torn also needs the host's usual filesystem check. Renames of a folder do not move its contents.
* **Writes go through the nfs write scope.** `lib_build()` and `lib_recover()` open `nfs_write_scope_enter("/.neupod")` and then the hardware write lock, and release them in the reverse order when they finish (the same order and narrowness as the settings store). The scope is always `/.neupod`, whatever `cfg->base` says: a base outside it is refused up front (`LIB_E_DENIED`) before anything is touched, and a scope already held by someone else (a settings save) makes the build report an error and not start. Every change to the card's directory tree (`mkdir`, `unlink`, `rename`, so also the recursive delete) is a real `nfs_mkdir` / `nfs_unlink` / `nfs_rename` call and so is gated by nfs itself. File *contents* are written through the library's own FatFs `FIL` objects, not nfs file handles, because the scanner holds up to about 40 files open at once from the 32 KB scratch while nfs has four handles of about 1.6 KB each in static RAM; `lib_fopen()` instead refuses any write-open unless the scope is open and the path is inside it (`nfs_write_scope_allows`). Reads (walk, tag parsing, the reader, the manifest check) stay on FatFs directly, as the other readers do. On the host the tests link the FatFs backend of nfs (`app/storage/nfs_fatfs.c`) over a mounted disk image instead of the POSIX one, with `hal_host_storage_set_mounted()` telling it a volume is mounted; the tests prove that during a build nfs and `lib_fopen` refuse writes, creations, deletions and renames under `/Music`, and that a base such as `/Music`, `/`, `/.neupod2` or `/.neupod/../Music` writes and deletes nothing.
* **Failure stance.** Everything before the swap only touches `lib.tmp` and `work`, so a failed or cancelled build (card full, card error, Play pressed) deletes those and leaves the old library. If the second rename fails the first is undone; if that also fails `lib_recover()` at the next boot restores it, and `lib_build()` refuses to start while the last swap cannot be settled. A manifest is only accepted when every count agrees with the size of the file it describes, so a damaged or hostile card cannot make the reader read outside a table. The crash and failing-rename hooks exist only in the test library (`NEUPOD_LIB_TEST_HOOKS`).

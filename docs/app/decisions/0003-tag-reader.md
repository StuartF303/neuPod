# 0003: Tag reader

Status: Proposed (2026-11-21) · Owner: Library engineer

## Context

- Fields: title, artist, album artist, album, track and disc number, year, genre, compilation flag, duration, sample
  rate, bit depth, embedded art (location and type, bytes read only when the art cache needs them).
- Containers: FLAC (Vorbis comments, PICTURE), ALAC and AAC in MP4 (`ilst` incl. `aART`, `cpil`, `disk`, `trkn`,
  `covr`), MP3 (ID3v2.3/2.4, ID3v1 fallback, Xing/Info/LAME and VBRI for duration), WAV (`LIST/INFO`, `id3 ` chunk),
  Ogg Opus and Ogg Vorbis.
- Same field semantics as the firmware (`app/audio/meta.c`, `mp4.c`): first four digits of DATE/TDRC/`©day` as year,
  ID3v1 `(n)` genres resolved, TCMP/`cpil`/COMPILATION as flag, TPE2/`aART`/ALBUMARTIST as album artist. The spec
  wants a library to look the same on phone and device.
- Robust: a damaged file yields "unreadable, reason" or partial tags, never an exception that stops the scan, never a
  hang, never unbounded memory.
- Fast: headers only. Budget about 60 ms a file (5,000 FLACs in under 5 min), most of which is I/O.
- Reads from a random-access byte source that can come from an Android SAF document (file descriptor) or an iOS
  security-scoped URL, not only a `dart:io` path (see [0004](0004-folder-access.md)).
- Runs in a background isolate; no platform-channel call per field or per read.
- Licence compatible with an MPL-2.0 app on both stores; actively maintained; read-only use (the app never writes tags).

## Options considered

Checked on pub.dev, crates.io and GitHub on 2026-11-21.

| Option | Version | Last release | Licence | Platforms | Fields | Containers | Byte source | Maintained |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Own Dart parser** (port of firmware `meta.c`/`mp4.c`, plus Ogg and WAV tags) | n/a | n/a | MPL-2.0 (ours) | all | all, by design | all, by design | any (our interface) | us |
| `audio_metadata_reader` (pure Dart) | 1.8.1 | 2026-10-14 | MIT | Android, iOS, desktop | no compilation flag, no bit depth in the common model; `aART` not read; MP3 album artist mis-mapped ([#102](https://github.com/ClementBeal/audio_metadata_reader/issues/102)) | all except WAV `id3 ` unclear | `dart:io File` only (sync `RandomAccessFile`) | yes (commits 2026-10-28) |
| `lofty` 0.25.5 (Rust) via `flutter_rust_bridge` 2.13.0, our own bindings | 0.25.5 / 2.13.0 | 2026-10-25 / 2026-08-23 | MIT OR Apache-2.0 / MIT | all (needs Rust toolchain) | all | all, plus many more | path, or `File::from_raw_fd` | very (commits 2026-11-21) |
| `audiotags` (lofty wrapper) | 1.4.5 | 2025-04-15 | MIT | all | partial | via lofty | path only | no: last push 2025-06-12 |
| `metadata_god` (lofty wrapper) | 1.1.0 | 2025-08-18 | MIT | all | partial | via lofty | path only | no: last push 2025-08-18 |
| `flutter_media_metadata` | 1.0.0+1 | 2022-01-30 | MIT | - | - | - | - | no (Dart 2 only) |
| `id3` | 1.0.2 | 2021-11-08 | MIT | - | MP3 only | MP3 | - | discontinued |
| `dart_tags` | 0.4.1 | 2024-03-27 | MIT | all | ID3 + MP4 only | MP3, MP4 | bytes | stale |
| Platform: `MediaMetadataRetriever` / `AVAsset` | OS | OS | OS | per platform | Android: sample rate and bit depth only on API 31+; iOS AVFoundation does not read Ogg | uneven, OEM-dependent | URI/fd (Android), URL (iOS) | OS |

**Own Dart parser.** About 1,500-2,000 lines of Dart: FLAC metadata blocks, MP4 box walk (`moov/udta/meta/ilst`,
`stsd` for ALAC/AAC rate and depth, `mdhd` for duration), ID3v2.2-2.4 with unsynchronisation and extended headers, the
MPEG frame header with Xing/Info/LAME/VBRI, RIFF `fmt `/`LIST INFO`/`id3 `, Ogg page reader with the Opus and Vorbis
identification and comment headers and the last granule position for duration. FLAC, MP3, MP4 and the text and genre
rules already exist in C under `app/audio/` and are ported, not designed; WAV tags and Ogg are new. It reads through a
small interface (`readAt(offset, length)`, `length`) so the same code runs on an Android fd, an iOS path and test
bytes, in any isolate. Every length read from a file is bounded against the file size and a cap, which is how the
firmware parser is written already.

**audio_metadata_reader.** Active, MIT, pure Dart and closest off-the-shelf fit, but its common model lacks the
compilation flag and bit depth, MP4 `aART` is not in its atom list, MP3 `artist` takes TPE2 before TPE1 (open issue
#102), and a bad Vorbis DATE throws and drops all metadata (#121). It takes a `dart:io File` only. We would be
reading its format-specific objects and patching around the gaps, or forking. Kept as a **test oracle**
(dev dependency only, never shipped): the scanner tests compare our output with it on the fixtures.

**lofty via flutter_rust_bridge.** The most complete and most fuzzed reader available, permissively licensed and very
active. Costs: no maintained Flutter wrapper (both wrappers are stale), so we own the bindings; a Rust toolchain with
Android NDK targets and iOS targets on every developer machine and on both CI runners; several MB of native code per
ABI; a second language in the repo. It reads a path or a raw fd, so 0004 still works. This is the fallback if our
parser fails the spike.

**Platform readers.** Rejected: two implementations with different answers, sample rate and bit depth missing below
Android 12, no Ogg on iOS, and each call crosses a platform channel.

## Decision

Write our own pure-Dart tag reader in `mobile/` (data layer), ported from the firmware parsers and extended with Ogg
Opus/Vorbis and WAV `LIST INFO`/`id3 `. It gives exactly the fields and semantics the device uses, reads through a
random-access interface that works with SAF file descriptors and security-scoped URLs, runs in a background isolate,
adds no package, no native toolchain and no licence row, and its size is bounded because most of it exists in C
already. `audio_metadata_reader` is used only as a test oracle. `lofty` through `flutter_rust_bridge` is the
recorded fallback.

## Consequences and risks

- We own correctness on malformed files. Mitigation: bounds checks on every length; a per-file time and byte budget;
  the scanner catches everything per file; a fuzz test (mutated fixtures) in CI; the fixtures in `tests/assets/audio`
  plus generated Ogg, WAV-with-tags and odd ID3 cases.
- Two implementations of the same rules (C and Dart) can drift. Mitigation: a shared table of golden cases (file,
  expected fields) used by both test suites where practical.
- Formats beyond the spec (AIFF, APE, DSF) are not read; they are listed greyed by the scanner as the device does.
- Art: the scan records offset, length and kind (from its first bytes, not the MIME) of the best picture, as `meta_art_offer` does (decision 0010);
  bytes are read later by the art cache, so the scan stays header-only.
- Sync reads in an isolate are fine; the isolate must never be the UI isolate.

## To verify in the phase 1 spike

1. Port FLAC + MP3 + MP4 readers; parse every file in `tests/assets/audio`; compare with `audio_metadata_reader` and
   with the firmware's host tests; differences explained.
2. Time 5,000 FLACs (generated, with 500 KB embedded art) on the Android emulator and the owner's iPhone, read through
   the 0004 byte source: target under 60 ms a file, header bytes read per file under 128 KB when art is skipped.
3. Ogg: Opus and Vorbis fixtures (generated with `opusenc`/`oggenc`), duration from the last granule (minus Opus
   pre-skip) within 1 ms of `ffprobe`.
4. MP3 VBR without Xing: estimated duration flagged as estimated, as the firmware does.
5. Fuzz: 10,000 mutated fixture files, no exception escapes, no file over 1 s, memory flat.
6. If 1-5 fail or the port is over its time box, prototype `lofty` via FRB on both CI runners and record the binary
   size and build-time cost.

## Implementation so far (phase 1, 2026-11-22)

`mobile/lib/data/scan/tag_reader.dart` reads FLAC (STREAMINFO, Vorbis comments, an ID3v2 tag in front skipped) and
MP3 (ID3v2.2 to 2.4 text frames with unsynchronisation and extended headers; first frame confirmed by the next;
duration from Xing/Info minus the LAME delay and padding, or VBRI, else estimated and flagged) through a `ByteSource`
(`readAt`, `length`), ported from `app/audio/meta.c`. Phase 1 keeps title, artist, album, track number, duration,
sample rate and bit depth; text is not cut at the firmware's small buffer sizes (1,024 characters instead), and UTF-16
surrogate pairs are kept. The scanner opens files on the root isolate (`neupod_storage.openHandle`: a detached fd on
Android, a `file://` URI on iOS) and parses them in a background isolate with `Isolate.run`, 32 files per run.
Failures become a reason on the track (`not a FLAC file`, `file is cut short`), never an exception that stops the
scan; a 2,000-case mutation test checks that. The rest of this note (other containers and fields, art, the
`audio_metadata_reader` oracle, timing at 5,000 files) is phase 3.

Also read in phase 1: the title sort tag (ID3 `TSOT`/`TST`, Vorbis `TITLESORT`), which the sort key uses (owner
decision 11). Failures are stored as a reason **code** (`UnreadableReason`: not downloaded, can't open, can't read,
wrong format, damaged); the UI words it, and the transient ones (can't open, can't read) are retried on the next scan.
VBRI is accepted only with the firmware's `vbri_parse` checks (62 fixed bytes, non-zero frames, entries, scale and
frames per entry, entry size 1 to 4).

Limits shared with the firmware, kept on purpose so both read the same tags: ID3v2.3 unsynchronisation is undone per
frame only when the tag header flag is set (as `id3v2_tags` does), compressed or encrypted frames are not decoded, and
a text frame over 400 bytes is skipped.

**Display text and key text are separate (owner decision 11: the app matches the firmware's code).** The reader
decodes the whole tag for display (UTF-16 pairs kept, malformed UTF-8 as U+FFFD), and separately keeps the title and
title sort tag exactly as the firmware's `meta_text_to_utf8()` stores them (`firmwareText()`, a port): into its
64- and 32-byte buffers, whole characters only, UTF-16 surrogates (pairs and lone ones) as `?`, malformed UTF-8 copied
through raw. A FLAC whose comment block is over 4096 bytes (`TAG_SCRATCH` in lib_build.c, which `flac_parse` then
skips) has no key text, so its key falls back to the file name as on the device. The song key is
`lib_collate_key()` (ported over raw bytes, `collateKeyBytes`) of that title, or of the file name with its extension
cut by `cut()` to 64 bytes, with the sort tag. `scripts/app/gen_tag_key_vectors.py` builds small tagged files for
each of these cases and records what the compiled firmware C makes of them; the app's test compares byte for byte.
Text is not trimmed (the firmware keeps spaces, and they sort). The Songs list shows an untagged song's name without
its extension (the folder-convention fallback comes in phase 3).

## Sources

All checked 2026-11-21.

- audio_metadata_reader: https://pub.dev/packages/audio_metadata_reader ; https://github.com/ClementBeal/audio_metadata_reader
  (README, `lib/src/parsers/tags/tag_parser.dart`, `lib/src/parsers/containers/mp4.dart`, issues #102, #121)
- lofty: https://crates.io/crates/lofty ; https://github.com/Serial-ATA/lofty-rs
- flutter_rust_bridge: https://pub.dev/packages/flutter_rust_bridge
- audiotags: https://pub.dev/packages/audiotags ; https://github.com/erikas-taroza/audiotags
- metadata_god: https://pub.dev/packages/metadata_god ; https://github.com/KRTirtho/frb_plugins
- flutter_media_metadata: https://pub.dev/packages/flutter_media_metadata
- id3: https://pub.dev/packages/id3 ; dart_tags: https://pub.dev/packages/dart_tags
- MediaMetadataRetriever: https://developer.android.com/reference/android/media/MediaMetadataRetriever
- Flutter supported platforms (Flutter 3.50: Android 24-37, iOS 15-27): https://docs.flutter.dev/reference/supported-platforms
- Firmware reference parsers: `app/audio/meta.c`, `app/audio/mp4.c`, `app/audio/wav.c`, `app/include/meta.h`

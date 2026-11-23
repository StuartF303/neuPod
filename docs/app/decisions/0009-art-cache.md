# 0009: Album art and the art cache

Status: Accepted (2026-11-23) · Owner: Library engineer

## Context

- Gate 1 (owner, 2026-11-23): no album art anywhere, on iOS or Android, in the screens or on the lock screen. The
  screens and the player could show art; the library supplied none.
- Firmware behaviour is the rule. The device picks art in `app/audio/meta.c` (`meta_art_offer`), `app/audio/art.c`
  (`art_sources`, `art_folder_rank`, `art_folder_pick`, `art_decode` over TJpgDec) and `app/runtime/art_service.c`
  (`find_and_decode`); the library's album header and Cover Flow ask by the album's first song (`ui_library.c`).
- The spec puts an art cache in the data layer and asks for work off the UI isolate and for bounded memory.
- The scan stays header-only (decision 0003): it records where a picture is, never its bytes.

## The firmware's rules, ported exactly (`mobile/lib/domain/library/art_rules.dart`)

1. Embedded picture: every FLAC PICTURE block and ID3v2 APIC/PIC frame is offered; a JPEG beats a PNG beats anything
   else, a front cover (type 3) adds one, the first wins a tie. The kind comes from the first bytes, not the mime.
   ID3 frames that are compressed, encrypted or unsynchronised (frame or tag), and descriptions past the first 160
   bytes, are skipped.
2. Sources in order: the embedded picture if it is a JPEG, then a cover file in the track's own folder. One that does
   not decode gives way to the next.
3. Cover file names: `cover.jpg`, then `folder.jpg`, then `front.jpg`, ASCII case folded. No `.jpeg`, no PNG.
4. Decodable means TJpgDec would decode it (`mobile/lib/data/art/firmware_jpeg.dart` ports `jd_prepare` and
   `art_decode`'s checks): baseline (SOF0) only, 8-bit tables, 1 or 3 components, Y sampling 1x1, 2x2 or 2x1, tables
   within its 4096-byte pool (each definition takes pool, so repeated tables overflow it), at most 5120 px a side, no
   side that scales to nothing at the decoder's scale (the 160 px builds, `NEUPOD_ART_HIRES`), and compressed data
   that runs to a marker before the picture ends: TJpgDec reads 16 bits ahead, so a cut anywhere in the scan, even
   only the end marker missing, fails there. Progressive JPEGs and PNGs are not shown, as on the device.
5. TJpgDec ignores EXIF orientation (APP1) and the Adobe colour transform (APP14): the app blanks both segments
   before decoding, so a cover is the same way up and the same colours as on the device.
6. An album's art is its first song's in album order; Now Playing and the lock screen show the playing track's own.

Checked against the C compiled for the host: `scripts/app/gen_art_vectors.py` (with `scripts/app/art_vectors.c`)
writes `mobile/test/fixtures/art/expected.tsv`, which `test/data/scan/art_vectors_test.dart` reads. For each cover
file the C also gives the thumbnail's shape and its colour at the centres of its quarters, which the app's decode
must match (so a rotation or a colour transform shows).

Where the app cannot see what the device sees, a lead decision as for walk order (`drift_library_view.dart`): names
of one rank in one folder (`Folder.jpg` and `folder.jpg` on a case-sensitive disk) are taken in byte order, since the
card's directory order is not visible; a cover file not downloaded from iCloud is not read (owner decision 7), so the
folder has none, which is what the device shows for a cover file it cannot decode.

Known differences:

- The device skips a cover file with the FAT hidden or system attribute (`art_service.c` `scan_next`, line 380:
  `nfs_dirent_hidden`); Windows Media Player writes `Folder.jpg` that way. Neither Android's folder access nor iOS
  shows FAT attributes, so the app uses such a file (it skips only names starting with a dot, as the device does).
- The app reads at most 32 MB of a picture (`maxArtBytes`); the firmware has no byte limit, only its 5120 px one.

## Options considered for decoding

| Option | Off the UI isolate | Tests | Cost |
| --- | --- | --- | --- |
| **`image` 4.13.1** (pure Dart, MIT, Brendan Duncan; released 2026-11-14) in `Isolate.run` | Yes, all of it | Plain `flutter test`, same code on both platforms | A package (with `archive` 4.7.1 MIT, `posix` 6.5.4 MIT); slower than native decoders |
| `dart:ui` `instantiateImageCodec` + `toByteData` | Decodes on engine threads, but the work is driven from the UI isolate and encoding a file needs the raster thread | Needs the engine (`runAsync`, real codecs) | None |
| Native (UIImage / BitmapFactory) through a plugin | Yes | Device only | Two implementations to keep equal |

**Decision: `image`**, in a background isolate, one picture at a time, decoded at the largest reduced scale (1/2,
1/4, 1/8) that keeps the long side at 600 px or more, as `art_decode` picks its own (a 5120 px cover would be about
79 MB of pixels at full scale). Every picture the device accepts is a baseline JPEG at most 5120 px a side, so the
pure-Dart decoder's speed is acceptable for a one-off per album.

## The cache (`mobile/lib/data/art/art_cache.dart`)

- In `art/` under the plugin's private support directory (excluded from backups, like the library database).
- Two JPEGs per picture: 192 px (rows, album header, Music menu: drawn up to 96 design px, decoded at twice that) and
  600 px (Home, Now Playing and Cover Flow up to 220 design px; the lock screen). Shrunk to fit, never enlarged,
  never the original.
- Keyed by a hash of every source's URI, size, modification time and place in its file: a changed file or a cover
  file added or removed is a new key.
- At most 64 MB, least recently used out first (a hit refreshes the file's time); the lock screen's song and the
  next four are pinned.
- One render at a time: the lock screen's first, then the newest request (what is on screen now). A cover that
  leaves the screen before its render starts withdraws it, and a render nobody waits for is dropped.
- The lock screen: each queue item's `artUri` is the `file://` path of its 600 px file; the first song's is made before
  the queue is set (waiting at most 2 s), the next four as the queue plays, found through the library by the queue
  items' ids (so a queue restored after a restart is served too).

## Schema

Schema version 3 adds six nullable columns to `tracks` (no new table: art is rebuilt by a rescan, so the guard test
stays at `sources` and `tracks`): `art_offset`, `art_length`, `art_kind`, `folder_art_uri`, `folder_art_size`,
`folder_art_mtime`. Scan version 3 reads every file again for its picture; every scan notes each folder's cover file
on its tracks without reading them.

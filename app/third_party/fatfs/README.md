# FatFs (vendored)

- Project: FatFs - Generic FAT Filesystem Module by ChaN
- Version: R0.15 w/patch1 (FF_DEFINED 80286)
- Source: http://elm-chan.org/fsw/ff/ (archive `arc/ff15.zip`)
- Licence: BSD-style, see `LICENSE.txt` and the header of each source file (kept intact).

Local changes: only `ffconf.h` (LFN=1 static buffer, code page 437, FF_USE_MKFS=1, FF_FS_NORTC=1,
exFAT off, FF_FS_READONLY=0). The stock `diskio.c` is not vendored; the glue onto `hal_storage`
lives in `app/storage/diskio.c`.

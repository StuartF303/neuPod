#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Regenerates the expected artist, album and song lists of the drift library view's entity tests
# (mobile/test/data/library_view/fixtures/*.expected) from the firmware's own library builder: each *.tsv there is given
# to scripts/app/library_entities.c, which runs lib_build() (app/library/lib_build.c) on a FatFs image and prints what
# lib_list_row() / lib_album() give.
# Run from anywhere: sh mobile/tool/library_entities/gen_library_entities.sh [output folder]
# CI writes to a temporary folder and diffs it against the fixtures, so a firmware change shows up.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
fixtures="$root/mobile/test/data/library_view/fixtures"
out=$(cd "${1:-$fixtures}" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$root"
cc -std=c11 -O1 -w -DNEUPOD_LIB_TEST_HOOKS=1 -Iapp/include -Iapp/third_party/fatfs -Ihal/include -Ihal/host/include \
    -Ihal/common -Itests -o "$tmp/library_entities" scripts/app/library_entities.c app/library/lib_*.c \
    app/third_party/fatfs/ff.c app/third_party/fatfs/ffunicode.c app/storage/ff_alloc.c app/storage/diskio.c \
    app/storage/nfs_fatfs.c app/storage/nfs_path.c app/audio/meta.c app/audio/mp4.c app/audio/wav.c \
    hal/host/hal_storage.c hal/common/hal_storage_common.c app/os/os_alloc_malloc.c
# The program makes its scratch disk image in the current folder.
cd "$tmp"
# It must refuse sibling paths that differ only in case (FAT folds case, so they would be one folder and break the
# walk model): a folder pair and a file pair, each expected to fail with its message.
for bad in '/Music/Live/1.flac\tA\tX\tL\t1\n/Music/live/2.flac\tB\tX\tL\t1\n' \
    '/Music/a/One.flac\tA\tX\tL\t1\n/Music/a/one.flac\tB\tX\tL\t1\n'; do
    if printf "$bad" | "$tmp/library_entities" > /dev/null 2> "$tmp/err"; then
        echo "library_entities accepted paths that differ only in case" >&2
        exit 1
    fi
    grep -q "differ only in case" "$tmp/err" || { cat "$tmp/err" >&2; exit 1; }
done
echo "checked: paths that differ only in case are refused"
for f in "$fixtures"/*.tsv; do
    name=$(basename "$f" .tsv)
    "$tmp/library_entities" < "$f" > "$out/$name.expected"
    echo "wrote $out/$name.expected"
done

#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Regenerates mobile/test/fixtures/plays/firmware_vectors.json from the firmware's own play counting and track
# statistics (app/runtime/play_tracker.c, app/settings/ratings_store.c, app/settings/plays_store.c), compiled for the
# host with scripts/app/plays_vectors.c. The app's test test/domain/library/play_stats_vectors_test.dart replays them.
# Usage: sh scripts/app/gen-plays-vectors.sh [output.json]   (needs cc). CI writes to a temporary file and diffs it.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -O1 -Wall -DNEUPOD_ART_HIRES \
    -I"$root/app/include" -I"$root/hal/include" \
    -o "$tmp/gen" "$here/plays_vectors.c" \
    "$root/app/runtime/play_tracker.c" "$root/app/settings/ratings_store.c" "$root/app/settings/plays_store.c"
out=${1:-"$root/mobile/test/fixtures/plays/firmware_vectors.json"}
mkdir -p "$(dirname "$out")"
"$tmp/gen" > "$out"
echo "wrote $out"

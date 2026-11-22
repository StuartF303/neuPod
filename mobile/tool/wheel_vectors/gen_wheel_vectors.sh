#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Regenerates mobile/test/domain/wheel/fixtures/firmware_vectors.json from the firmware's own wheel code.
# Run from anywhere: sh mobile/tool/wheel_vectors/gen_wheel_vectors.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
{
    sed -n '/^int32_t ui_accel_feed/,/^}/p' "$root/app/ui/ui_logic.c"
    sed -n '/^static char norm/p' "$root/app/ui/input/wheel_input.c"
    sed -n '/^void wi_letterjump_init/p' "$root/app/ui/input/wheel_input.c"
    sed -n '/^bool wi_letterjump_scroll/,/^}/p' "$root/app/ui/input/wheel_input.c"
    sed -n '/^void wi_letterjump_tick/,/^}/p' "$root/app/ui/input/wheel_input.c"
} > "$tmp/extracted.inc"
cc -std=c11 -Wall -Werror -I"$tmp" -I"$root/app/include" -I"$root/hal/include" \
    -o "$tmp/gen" "$here/gen_wheel_vectors.c"
"$tmp/gen" > "$root/mobile/test/domain/wheel/fixtures/firmware_vectors.json"
echo "wrote mobile/test/domain/wheel/fixtures/firmware_vectors.json"

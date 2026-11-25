#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
#
# Real iOS simulator screenshots for the owner's design gate 2: Home and Now
# Playing, in Classic Light and Modern Dark, with the phone's status bar hidden or
# shown and the wordmark on or off (fit A and the mode's body finish only). The scene is chosen by settings read by
# gate2_sim_main.dart (here a scene file, /tmp/neupod-gate2-scene.env, which
# the simulator app can read), so one build serves every shot.
#
#   cd mobile
#   flutter build ios --simulator --debug --no-codesign -t tool/gate2_shots/gate2_sim_main.dart
#   tool/gate2_shots/capture_sim.sh OUT_DIR "iPhone 16e" "iPhone 16" "iPhone 16 Pro Max"
#
# Run it through ~/bin/neupod-buildlock on the shared machine. Shuts each
# simulator down when done.
set -euo pipefail
trap 'rm -f /tmp/neupod-gate2-scene.env' EXIT

out=$1
shift
app=build/ios/iphonesimulator/Runner.app
bundle=net.stuartfraser.neupod
mkdir -p "$out"

# name status wordmark
variants=(
  "base 1 1"
  "no-statusbar 0 1"
  "no-wordmark 1 0"
)

for model in "$@"; do
  udid=$(xcrun simctl list devices available | grep -F "    $model (" | head -1 | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')
  slug=$(echo "$model" | tr 'A-Z ' 'a-z-')
  echo "== $model $udid"
  xcrun simctl boot "$udid" 2>/dev/null || true
  xcrun simctl bootstatus "$udid" -b >/dev/null
  xcrun simctl status_bar "$udid" override --time 9:41 --batteryState charged \
    --batteryLevel 100 --cellularMode active --cellularBars 4 --wifiBars 3
  xcrun simctl install "$udid" "$app"
  for style in classic_light modern_dark; do
    for scene in home nowplaying; do
      for v in "${variants[@]}"; do
        read -r name status wordmark <<<"$v"
        xcrun simctl terminate "$udid" "$bundle" 2>/dev/null || true
        printf 'GATE2_STYLE=%s\nGATE2_STATUS=%s\nGATE2_WORDMARK=%s\nGATE2_SCENE=%s\n' \
          "$style" "$status" "$wordmark" "$scene" >/tmp/neupod-gate2-scene.env
        xcrun simctl launch "$udid" "$bundle" >/dev/null
        sleep 5
        xcrun simctl io "$udid" screenshot "$out/sim-$slug-$scene-$style-$name.png" >/dev/null 2>&1
      done
    done
  done
  xcrun simctl terminate "$udid" "$bundle" 2>/dev/null || true
  xcrun simctl shutdown "$udid"
done

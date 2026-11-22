#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
#
# Verifies neupod_storage on an Android emulator: picks Music/neupod_it with
# the real system picker (driven by drive_saf_picker.py), walks and reads it,
# then relaunches the app and checks the read-only grant was kept.
#
# Usage: scripts/app/verify-folder-access-android.sh [AVD]   (default neupod_api36)
# On the shared build Mac run it under the build lock:
#   ~/bin/neupod-buildlock scripts/app/verify-folder-access-android.sh
set -euo pipefail

AVD="${1:-neupod_api36}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
EMU_LOG="$(mktemp -t neupod-emu)"

# Our own console port, so the serial is known and never collides with another emulator.
PORT="${NEUPOD_EMU_PORT:-5580}"
SERIAL="emulator-$PORT"
"$SDK/emulator/emulator" -avd "$AVD" -port "$PORT" -no-window -no-audio -no-snapshot-save -no-boot-anim >"$EMU_LOG" 2>&1 &
EMU_PID=$!
cleanup() {
  adb -s "$SERIAL" emu kill >/dev/null 2>&1 || kill "$EMU_PID" 2>/dev/null || true
  wait "$EMU_PID" 2>/dev/null || true
}
trap cleanup EXIT

adb -s "$SERIAL" wait-for-device
until [ "$(adb -s "$SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do sleep 2; done
echo "emulator: $(adb -s "$SERIAL" shell getprop ro.build.version.release | tr -d '\r') (API $(adb -s "$SERIAL" shell getprop ro.build.version.sdk | tr -d '\r'))"
adb -s "$SERIAL" shell input keyevent KEYCODE_WAKEUP
adb -s "$SERIAL" shell wm dismiss-keyguard || true

# The test tree: freely licensed fixtures from tests/assets/audio.
# Shared storage (FUSE) can refuse writes for a while after boot: retry.
for _ in $(seq 1 30); do
  adb -s "$SERIAL" shell 'rm -rf /sdcard/Music/neupod_it && mkdir -p /sdcard/Music/neupod_it/sub' 2>/dev/null && break
  sleep 3
done
# Pushed to /data/local/tmp and copied by the shell user: a direct push into
# shared storage can fail on a fresh emulator while MediaProvider starts.
adb -s "$SERIAL" push "$REPO/tests/assets/audio/flac16_44k_st.flac" /data/local/tmp/a.flac >/dev/null
adb -s "$SERIAL" push "$REPO/tests/assets/audio/mp3_44k_st.mp3" /data/local/tmp/b.mp3 >/dev/null
adb -s "$SERIAL" shell cp /data/local/tmp/a.flac /sdcard/Music/neupod_it/a.flac
adb -s "$SERIAL" shell cp /data/local/tmp/b.mp3 /sdcard/Music/neupod_it/sub/b.mp3
adb -s "$SERIAL" shell ls -lR /sdcard/Music/neupod_it

cd "$REPO/mobile"
flutter build apk --debug -t integration_test/folder_access_test.dart --dart-define=NEUPOD_FOLDER_IT=true
APK=build/app/outputs/flutter-apk/app-debug.apk
PKG=net.stuartfraser.neupod
adb -s "$SERIAL" uninstall "$PKG" >/dev/null 2>&1 || true
adb -s "$SERIAL" install -r "$APK" >/dev/null

# Launches the test app and waits for its result in logcat; prints the
# NEUPOD_STEP line it reached.
launch() {
  adb -s "$SERIAL" logcat -c
  adb -s "$SERIAL" shell am start -W -n "$PKG/.MainActivity" >/dev/null
  local deadline=$((SECONDS + 600)) log
  while [ $SECONDS -lt $deadline ]; do
    log="$(adb -s "$SERIAL" logcat -d -s flutter:I 2>/dev/null)"
    if grep -q 'Some tests failed\|\[E\]' <<<"$log"; then echo "$log" | tail -60; return 1; fi
    if grep -q 'All tests passed' <<<"$log"; then
      grep -o 'NEUPOD_\(SCAN\|PLAYED\) .*' <<<"$log" | tr -d '\r' >&2 || true
      grep -o 'NEUPOD_STEP .*' <<<"$log" | tr -d '\r'
      return 0
    fi
    sleep 2
  done
  echo "timed out" >&2
  adb -s "$SERIAL" logcat -d -s flutter:I | tail -60
  return 1
}

python3 "$HERE/drive_saf_picker.py" -s "$SERIAL" --timeout 600 &
DRIVER=$!
STEP1="$(launch)"
wait "$DRIVER"
echo "launch 1: $STEP1"
[ "$STEP1" = "NEUPOD_STEP picked" ]

# A real restart: the process is killed, then launched again.
adb -s "$SERIAL" shell am force-stop "$PKG"
STEP2="$(launch)"
echo "launch 2: $STEP2"
[ "$STEP2" = "NEUPOD_STEP persisted-and-removed" ]
adb -s "$SERIAL" uninstall "$PKG" >/dev/null
echo "verify-folder-access-android: OK"

#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
#
# Verifies neupod_storage on an iOS simulator: the Documents root, a folder
# bookmark added through the debug hook (the system picker UI cannot be driven
# from a Flutter integration test), walking and reading, and the bookmark being
# resolved again after the app is relaunched.
#
# Usage: scripts/app/verify-folder-access-ios.sh [RUNTIME] [DEVICE]
#   default: iOS 18.6, iPhone 16 (the minimum OS the app supports)
# On the shared build Mac run it under the build lock:
#   ~/bin/neupod-buildlock scripts/app/verify-folder-access-ios.sh
set -euo pipefail

RUNTIME="${1:-iOS 18.6}"
DEVICE="${2:-iPhone 16}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

UDID="$(xcrun simctl list devices available | awk -v rt="-- $RUNTIME --" -v dev="$DEVICE" '
  $0 == rt { inrt = 1; next } /^-- / { inrt = 0 }
  inrt && index($0, "    " dev " (") == 1 { match($0, /\([0-9A-F-]+\)/); print substr($0, RSTART + 1, RLENGTH - 2); exit }')"
[ -n "$UDID" ] || { echo "no simulator '$DEVICE' on '$RUNTIME'" >&2; exit 1; }
echo "simulator: $DEVICE, $RUNTIME ($UDID)"

xcrun simctl boot "$UDID" 2>/dev/null || true
trap 'xcrun simctl shutdown "$UDID" >/dev/null 2>&1 || true' EXIT
xcrun simctl bootstatus "$UDID" -b >/dev/null

cd "$REPO/mobile"
# Run with `flutter run`, not `flutter test` (which uninstalls the app after
# each run and so would drop the bookmark). A simulator only runs debug
# builds, and those must be started by the Flutter tool.
BID=net.stuartfraser.neupod
xcrun simctl uninstall "$UDID" "$BID" >/dev/null 2>&1 || true
LOG="$(mktemp -t neupod-ios)"
# Runs the test app until it reports, then stops it; prints the NEUPOD_STEP
# line it reached.
launch() {
  : >"$LOG"
  flutter run -d "$UDID" -t integration_test/folder_access_test.dart \
    --dart-define=NEUPOD_FOLDER_IT=true >"$LOG" 2>&1 </dev/null &
  local pid=$! deadline=$((SECONDS + 600)) rc=1
  while [ $SECONDS -lt $deadline ] && kill -0 "$pid" 2>/dev/null; do
    if grep -q 'Some tests failed\|\[E\]' "$LOG"; then tail -60 "$LOG" >&2; break; fi
    if grep -q 'All tests passed' "$LOG"; then
      grep -o 'NEUPOD_\(SCAN\|PLAYED\) .*' "$LOG" | tr -d '\r' >&2 || true
      grep -o 'NEUPOD_STEP .*' "$LOG" | tr -d '\r'
      rc=0
      break
    fi
    sleep 2
  done
  [ $rc = 0 ] || tail -40 "$LOG" >&2
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  xcrun simctl terminate "$UDID" "$BID" >/dev/null 2>&1 || true
  return $rc
}

STEP1="$(launch)"
echo "launch 1: $STEP1"
[ "$STEP1" = "NEUPOD_STEP picked" ]
STEP2="$(launch)"
echo "launch 2: $STEP2"
[ "$STEP2" = "NEUPOD_STEP persisted-and-removed" ]

# The Info.plist keys that make Documents visible in Files and Finder.
APP="$(xcrun simctl get_app_container "$UDID" "$BID" app)"
for key in UIFileSharingEnabled LSSupportsOpeningDocumentsInPlace; do
  printf '%s = %s\n' "$key" "$(/usr/libexec/PlistBuddy -c "Print :$key" "$APP/Info.plist")"
done
xcrun simctl uninstall "$UDID" "$BID"
echo "verify-folder-access-ios: OK"

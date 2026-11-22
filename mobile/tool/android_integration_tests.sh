#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Runs each mobile/integration_test/*_test.dart on the one connected Android device or emulator as an instrumentation
# test (android/app/src/androidTest, the integration_test package's "Android Device Testing" route): the app is built
# with that file as its Dart entry point and the Dart tests report through instrumentation, not through the Dart VM
# service that `flutter test` finds by reading logcat. Needs android/local.properties (any `flutter build apk` writes
# it). Stops at the first failing or hanging file and exits non-zero.
#
#   bash tool/android_integration_tests.sh    (from mobile/; PER_FILE_TIMEOUT=10m by default)
set -euo pipefail
cd "$(dirname "$0")/.."

shopt -s nullglob
files=(integration_test/*_test.dart)
if [ ${#files[@]} -eq 0 ]; then
  echo "::error::no integration tests in mobile/integration_test"
  exit 1
fi

per_file_timeout="${PER_FILE_TIMEOUT:-10m}"
for file in "${files[@]}"; do
  echo "::group::$file"
  adb logcat -c || true
  rm -rf build/app/outputs/androidTest-results
  status=0
  (cd android && timeout --kill-after=30s "$per_file_timeout" \
    ./gradlew app:connectedDebugAndroidTest -Ptarget="$PWD/../$file") || status=$?
  echo "::endgroup::"
  if [ "$status" -ne 0 ]; then
    if [ "$status" -eq 124 ]; then
      echo "::error::$file did not finish within $per_file_timeout"
    else
      echo "::error::$file failed (exit $status)"
    fi
    echo "::group::instrumentation results"
    find build/app/outputs/androidTest-results -name '*.xml' -exec cat {} + 2>/dev/null || true
    echo "::endgroup::"
    echo "::group::logcat (flutter, test runner, crashes)"
    adb logcat -d -s flutter:V TestRunner:V FlutterTestRunner:V AndroidRuntime:E | tail -300 || true
    echo "::endgroup::"
    exit 1
  fi
  # Each Dart test is one JUnit test case. A run that reported none (the Dart side never sent its results) is a
  # failure, not a pass.
  names=$(find build/app/outputs/androidTest-results -name 'TEST-*.xml' -exec cat {} + 2>/dev/null \
    | grep -o '<testcase name="[^"]*"' | sed 's/^<testcase name="//; s/"$//' || true)
  if [ -z "$names" ]; then
    echo "::error::$file reported no test results"
    exit 1
  fi
  echo "$file: $(printf '%s\n' "$names" | wc -l | tr -d ' ') passed"
  printf '%s\n' "$names" | sed 's/^/  /'
done
echo "OK: ${#files[@]} integration test files passed"

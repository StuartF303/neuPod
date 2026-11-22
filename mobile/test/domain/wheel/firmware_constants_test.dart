// SPDX-License-Identifier: MPL-2.0

// The Dart constants must match the firmware headers they were copied from.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/firmware_constants.dart';

int _define(String path, String name) {
  final text = File('../$path').readAsStringSync();
  final m = RegExp(
    '^#define\\s+$name\\s+\\(?(-?\\d+)u?\\)?',
    multiLine: true,
  ).firstMatch(text);
  if (m == null) fail('$name not found in $path');
  return int.parse(m.group(1)!);
}

void main() {
  test('constants match the firmware headers', () {
    const rc = 'app/include/runtime_config.h';
    const logic = 'app/ui/ui_logic.h';
    const wi = 'app/include/wheel_input.h';
    expect(
      halWheelPositions,
      _define('hal/include/hal_input.h', 'HAL_WHEEL_POSITIONS'),
    );
    expect(uiCountsPerStep, _define(rc, 'UI_COUNTS_PER_STEP'));
    expect(uiAccelWindowMs, _define(rc, 'UI_ACCEL_WINDOW_MS'));
    expect(uiAccelT1, _define(rc, 'UI_ACCEL_T1'));
    expect(uiAccelT2, _define(rc, 'UI_ACCEL_T2'));
    expect(uiAccelT3, _define(rc, 'UI_ACCEL_T3'));
    expect(uiSeekIdleMs, _define(logic, 'UI_SEEK_IDLE_MS'));
    expect(uiSeekCommitMs, _define(logic, 'UI_SEEK_COMMIT_MS'));
    expect(uiSelHoldMs, _define(logic, 'UI_SELHOLD_MS'));
    expect(uiHoldMs, _define(logic, 'UI_HOLD_MS'));
    expect(uiFfSpeed, _define(logic, 'UI_FF_SPEED'));
    expect(uiHoldMaxMs, _define(logic, 'UI_HOLD_MAX_MS'));
    expect(wiLjFastSteps, _define(wi, 'WI_LJ_FAST_STEPS'));
    expect(wiLjWindowMs, _define(wi, 'WI_LJ_WINDOW_MS'));
    expect(wiLjHideMs, _define(wi, 'WI_LJ_HIDE_MS'));
    expect(uiRailFadeMs, _define('app/ui/ui_rail.c', 'FADE_MS'));
  });

  test('u32 and i32 wrap like C', () {
    expect(u32(-1), 0xFFFFFFFF);
    expect(u32(0x100000005), 5);
    expect(i32(0x80000000), -0x80000000);
    expect(i32(-5), -5);
  });
}

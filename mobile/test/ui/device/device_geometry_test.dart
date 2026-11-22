// SPDX-License-Identifier: MPL-2.0

// The device geometry against the design's reference implementation:
// fixtures/render_py_geometry.json is render.py's geom() for the three phones
// (fit A; fit B was dropped at gate 2) (docs/design/2026-11-21-app-device-view/render.py, run with
// `python3 -I`; regenerate with the snippet in the PR that added it).

import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/ui/device/device_geometry.dart';

import 'device_test_harness.dart';

void main() {
  final ref = jsonDecode(
    File('test/ui/device/fixtures/render_py_geometry.json').readAsStringSync(),
  ) as Map<String, dynamic>;

  for (final phone in designPhones) {
    {
      final key = '${phone.w.toInt()}A';
      test('$key agrees with render.py', () {
        final r = ref[key] as Map<String, dynamic>;
        double v(String k) => (r[k] as num).toDouble();
        final g = DeviceGeometry.compute(
          width: phone.w,
          height: phone.h,
          safeTop: phone.top,
          safeBottom: phone.bottom,
        );
        const e = 1e-9;
        expect(g.screen.width, closeTo(v('sw'), e));
        expect(g.screen.height, closeTo(v('sh'), e));
        expect(g.screen.left, closeTo(v('scr_x'), e));
        expect(g.screen.top, closeTo(v('scr_top'), e));
        expect(g.screen.bottom, closeTo(v('scr_bot'), e));
        expect(g.frameWidth, closeTo(v('bz'), e));
        expect(g.wheelDiameter, closeTo(v('D'), e));
        expect(g.wheelCentre.dy, closeTo(v('cy'), e));
        expect(g.wheelCentre.dx, closeTo(phone.w / 2, e));
        expect(g.body.top, closeTo(v('body_top'), e));
        expect(g.body.bottom, closeTo(v('body_bot'), e));
        expect(g.scale, closeTo(v('scale'), e));
        expect(g.showWordmark, r['mark_shown']);
        // render.py puts the text box's top 8 pt above the gap's middle.
        expect(g.wordmarkCentreY, closeTo(v('mark_y') + 8, e));
      });
    }
  }

  test('the scale table of section 1', () {
    for (final (w, s, d) in [
      (375.0, 0.550, 240.0),
      (393.0, 0.576, 251.52),
      (440.0, 0.645, 281.6),
    ]) {
      final g = DeviceGeometry.compute(
        width: w,
        height: 900,
        safeTop: 50,
        safeBottom: 34,
      );
      expect(g.scale, closeTo(s, 0.0006));
      expect(g.wheelDiameter, closeTo(d, 1e-9));
      expect(g.wheelDiameter * 0.36, closeTo(d * 0.36, 1e-9));
    }
  });

  test('narrow phones keep a 320 pt screen and shrink the margin to 12', () {
    final g = DeviceGeometry.compute(
      width: 350,
      height: 700,
      safeTop: 20,
      safeBottom: 0,
    );
    expect(g.screen.width, 320);
    expect(g.screen.left, 15);
    final n = DeviceGeometry.compute(
      width: 344,
      height: 700,
      safeTop: 20,
      safeBottom: 0,
    );
    expect(n.screen.width, 320);
    expect(n.screen.left, 12);
    expect(n.tooNarrow, isFalse);
    final x = DeviceGeometry.compute(
      width: 330,
      height: 700,
      safeTop: 20,
      safeBottom: 0,
    );
    expect(x.screen.left, 12); // below the supported width: margin kept
    expect(x.tooNarrow, isTrue);
  });

  test('fit A shrinks the wheel on a short phone, to no less than 0.56 W', () {
    final g = DeviceGeometry.compute(
      width: 375,
      height: 560,
      safeTop: 20,
      safeBottom: 0,
    );
    final space = (560 - 0.03 * 375) - g.frame.bottom;
    expect(g.wheelDiameter, closeTo(space - 24, 1e-9));
    expect(g.wheelDiameter, lessThan(0.64 * 375));
    final h = DeviceGeometry.compute(
      width: 375,
      height: 450,
      safeTop: 20,
      safeBottom: 0,
    );
    expect(h.wheelDiameter, closeTo(0.56 * 375, 1e-9));
  });

  test('the wordmark is omitted when its gap is under 44 pt', () {
    final g = DeviceGeometry.compute(
      width: 375,
      height: 600,
      safeTop: 20,
      safeBottom: 0,
    );
    expect(g.wheelCentre.dy - g.wheelRadius - g.screen.bottom, lessThan(44));
    expect(g.showWordmark, isFalse);
  });

  test('wheel touch area clears the system edge gestures by 0.18 W', () {
    for (final p in designPhones) {
      final g = DeviceGeometry.compute(
        width: p.w,
        height: p.h,
        safeTop: p.top,
        safeBottom: p.bottom,
      );
      expect(g.wheelCentre.dx - g.wheelRadius, closeTo(0.18 * p.w, 1e-9));
    }
  });
}

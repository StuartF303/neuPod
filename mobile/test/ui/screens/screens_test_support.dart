// SPDX-License-Identifier: MPL-2.0

// Shared pieces of the gate screens' tests: pumping a screen on its canvas at
// 600x450 in a style and tier, and a recording volume port.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/system_volume_port.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/text_tier.dart';

/// Pumps [child] inside the 600x450 canvas for [style] and [tier], inside a
/// repaint boundary keyed `screen`.
Future<void> pumpScreenCanvas(
  WidgetTester tester,
  NeupodStyle style,
  TextTier tier,
  Widget child, {
  BackdropLayout backdrop = BackdropLayout.lists,
}) async {
  debugDisableShadows = false;
  tester.view.physicalSize = neupodScreenSize;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  await tester.pumpWidget(
    Directionality(
      textDirection: TextDirection.ltr,
      child: RepaintBoundary(
        key: const ValueKey('screen'),
        child: NeupodScreen(
          style: style,
          tier: tier,
          backdrop: backdrop,
          child: child,
        ),
      ),
    ),
  );
}

/// A system volume that records steps and can be made to fail.
class RecordingVolume implements SystemVolumePort {
  /// Starts at [current] of [steps].
  RecordingVolume({this.current = 8, this.steps = 16, this.failWith});

  /// The step now.
  int current;

  /// The platform's steps.
  final int steps;

  /// When set, every step fails with this status and the volume stays.
  SystemVolumeStatus? failWith;

  /// The directions asked for.
  final List<int> asked = [];

  @override
  Future<SystemVolume> read() async => SystemVolume(
    step: current,
    steps: steps,
    status: failWith ?? SystemVolumeStatus.ok,
  );

  @override
  Future<SystemVolume> step(int direction) async {
    asked.add(direction);
    if (failWith == null) current = (current + direction.sign).clamp(0, steps);
    return read();
  }
}

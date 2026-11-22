// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/device/device_geometry.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/device/hit_gate.dart';

/// Hosts a screen inside the device: the child is laid out at 600x450
/// design pixels and scaled uniformly by s = screen width / 600 (design
/// section 1), never re-laid out per phone.
///
/// The hit-routing contract for screens (direct touch, design section 6):
///
/// - Candidate 1 ([DirectTouch.wheelOnly]): the screen takes no pointer
///   touches at all; everything goes through the wheel. Semantics are kept,
///   so the screen reader can still activate a row.
/// - Candidate 2 ([DirectTouch.wheelAndScreen]): pointer events reach the
///   screen in its own 600x450 coordinates (the scale is undone by the
///   framework's hit test). The screen decides what takes touches: rows (tap
///   = move the selection there, then Select; pressed tint from touch-down),
///   list drags, Cover Flow drags, Now Playing's art, a panel's OK. The
///   status bar, the right-hand pane and panel backgrounds take none.
/// - Screens read the candidate with [DeviceScreenScope.of] when they need
///   to (for example to show the pressed tint only in candidate 2).
class DeviceScreenHost extends StatelessWidget {
  /// Hosts [child] at [scale] with [directTouch].
  const DeviceScreenHost({
    required this.scale,
    required this.directTouch,
    required this.child,
    this.dragProgressToSeek = false,
    super.key,
  });

  /// s, the design-to-phone scale.
  final double scale;

  /// The direct-touch candidate.
  final DirectTouch directTouch;

  /// Now Playing's progress bar may be dragged to seek (a gate switch).
  final bool dragProgressToSeek;

  /// The screen, laid out at 600x450.
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final touch = directTouch == DirectTouch.wheelAndScreen;
    return SizedBox(
      width: DeviceGeometry.designWidth * scale,
      height: DeviceGeometry.designHeight * scale,
      child: HitGate(
        accepts: (_, _) => touch,
        child: FittedBox(
          fit: BoxFit.fill,
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: DeviceGeometry.designWidth,
            height: DeviceGeometry.designHeight,
            child: DeviceScreenScope(
              scale: scale,
              directTouch: directTouch,
              dragProgressToSeek: dragProgressToSeek,
              child: child,
            ),
          ),
        ),
      ),
    );
  }
}

/// What a screen inside the device may need to know about its host.
class DeviceScreenScope extends InheritedWidget {
  /// The scope.
  const DeviceScreenScope({
    required this.scale,
    required this.directTouch,
    required this.dragProgressToSeek,
    required super.child,
    super.key,
  });

  /// s, the design-to-phone scale (design px x s = points).
  final double scale;

  /// The direct-touch candidate.
  final DirectTouch directTouch;

  /// Now Playing's progress bar may be dragged to seek.
  final bool dragProgressToSeek;

  /// The scope above [context], or the defaults when there is none (a
  /// screen tested on its own).
  static DeviceScreenScope of(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<DeviceScreenScope>() ??
      const DeviceScreenScope(
        scale: 1,
        directTouch: DirectTouch.wheelAndScreen,
        dragProgressToSeek: false,
        child: SizedBox.shrink(),
      );

  @override
  bool updateShouldNotify(DeviceScreenScope old) =>
      old.scale != scale ||
      old.directTouch != directTouch ||
      old.dragProgressToSeek != dragProgressToSeek;
}

// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/device/device_geometry.dart';

/// Hosts a screen inside the device: the child is laid out at 600x450
/// design pixels and scaled uniformly by s = screen width / 600 (design
/// section 1), never re-laid out per phone.
///
/// The hit-routing contract for screens (direct touch, design section 6,
/// owner decision 18): pointer events reach the screen in its own 600x450
/// coordinates (the framework's hit test undoes the scale). The screen
/// decides what takes touches: rows (tap = move the selection there, then
/// Select; pressed tint from touch-down), list drags, Cover Flow drags, Now
/// Playing's art, a panel's OK. The status bar, the right-hand pane and
/// panel backgrounds take none.
class DeviceScreenHost extends StatelessWidget {
  /// Hosts [child] at [scale].
  const DeviceScreenHost({required this.scale, required this.child, super.key});

  /// s, the design-to-phone scale.
  final double scale;

  /// The screen, laid out at 600x450.
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: DeviceGeometry.designWidth * scale,
      height: DeviceGeometry.designHeight * scale,
      child: FittedBox(
        fit: BoxFit.fill,
        alignment: Alignment.topLeft,
        child: SizedBox(
          width: DeviceGeometry.designWidth,
          height: DeviceGeometry.designHeight,
          child: DeviceScreenScope(scale: scale, child: child),
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
    required super.child,
    super.key,
  });

  /// s, the design-to-phone scale (design px x s = points).
  final double scale;

  /// The scope above [context], or the defaults when there is none (a
  /// screen tested on its own).
  static DeviceScreenScope of(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<DeviceScreenScope>() ??
      const DeviceScreenScope(scale: 1, child: SizedBox.shrink());

  @override
  bool updateShouldNotify(DeviceScreenScope old) => old.scale != scale;
}

// SPDX-License-Identifier: MPL-2.0

/// What a screen on the stack is given by the shell: whether it is on top,
/// and a place to take the wheel's events.
///
/// The shell listens to the wheel bus once and gives each event to the top
/// screen's [ScreenHandle.wheel]. A screen that returns false (or sets no
/// handler) lets the shell act: Menu goes back, Previous, Next and Play/Pause
/// drive the player (as on the device, where they work on every screen that
/// does not use them itself).
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';

/// Takes a wheel event; returns true when it used it.
typedef WheelHandler = bool Function(WheelEvent event);

/// The shell's handle on one screen.
class ScreenHandle {
  /// A handle for a screen that is [onTop] or not.
  ScreenHandle({bool onTop = true}) : isTop = ValueNotifier(onTop);

  /// True while this screen is the one showing and taking input. A screen
  /// that sets the wheel's semantics does so when this turns true.
  final ValueNotifier<bool> isTop;

  /// The screen's wheel handler; set it in `initState`, clear it in
  /// `dispose` (only if it is still yours).
  WheelHandler? wheel;
}

/// Gives a screen its [ScreenHandle].
class ScreenScope extends InheritedWidget {
  /// A scope for [handle].
  const ScreenScope({required this.handle, required super.child, super.key});

  /// The handle.
  final ScreenHandle handle;

  /// The scope above [context], or null when the screen is not on the stack
  /// (a screen built on its own in a test).
  static ScreenHandle? maybeOf(BuildContext context) =>
      context.getInheritedWidgetOfExactType<ScreenScope>()?.handle;

  @override
  bool updateShouldNotify(ScreenScope old) => old.handle != handle;
}

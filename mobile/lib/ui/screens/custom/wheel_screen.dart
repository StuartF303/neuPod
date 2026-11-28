// SPDX-License-Identifier: MPL-2.0

/// What Custom mode's own wheel screens (the part editor, the name entry)
/// share: taking the wheel from the shell while on top, and saying what the
/// wheel is to the screen reader.
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/shell/screen_scope.dart';

/// A screen that takes the wheel's events itself. Mix into the screen's
/// state; give [onWheel] and [describe].
mixin WheelScreenState<T extends ConsumerStatefulWidget> on ConsumerState<T> {
  late final WheelBus wheelBus;
  ScreenHandle? _handle;

  /// Whether this screen is the one showing.
  bool get isTop => _handle?.isTop.value ?? true;

  /// Takes a wheel event; true when used. What is not taken, the shell does
  /// (Menu goes back, Previous and Next skip tracks).
  bool onWheel(WheelEvent e);

  /// Sets `wheelBus.semantics` for this screen.
  void describe();

  /// The screen went to the top (or came back to it).
  void becameTop() {}

  /// The screen went from the top (a screen over it, or it was popped).
  void leftTop() {}

  @override
  void initState() {
    super.initState();
    wheelBus = ref.read(wheelBusProvider);
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final handle = ScreenScope.maybeOf(context);
    if (handle != _handle) {
      _handle?.isTop.removeListener(_topChanged);
      if (_handle?.wheel == _onWheel) _handle?.wheel = null;
      _handle = handle;
      handle?.wheel = _onWheel;
      handle?.isTop.addListener(_topChanged);
      WidgetsBinding.instance.addPostFrameCallback((_) => _topChanged());
    }
  }

  @override
  void dispose() {
    _handle?.isTop.removeListener(_topChanged);
    if (_handle?.wheel == _onWheel) _handle?.wheel = null;
    leftTop();
    super.dispose();
  }

  bool _onWheel(WheelEvent e) => isTop && onWheel(e);

  bool _wasTop = false;

  void _topChanged() {
    if (!mounted) return;
    if (isTop) {
      _wasTop = true;
      becameTop();
      describe();
    } else if (_wasTop) {
      _wasTop = false;
      leftTop();
    }
  }
}

/// Runs [action] once the wheel has rested for [delay] (the lists' preview
/// after 250 ms). A new [start] cancels the one waiting.
class RestTimer {
  Timer? _timer;

  /// Waits, then calls [action].
  void start(Duration delay, VoidCallback action) {
    _timer?.cancel();
    _timer = Timer(delay, action);
  }

  /// Stops waiting.
  void cancel() {
    _timer?.cancel();
    _timer = null;
  }
}

/// How long a list must rest on a row before the device shows it.
const Duration previewRest = Duration(milliseconds: 250);

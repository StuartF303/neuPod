// SPDX-License-Identifier: MPL-2.0

/// The screen stack inside the device view: what `DeviceView(screen: ...)`
/// shows.
///
/// - Pushes slide in from the right and pops slide out in 80 ms (none under
///   Reduce Motion), over the screen beneath, which keeps its state (its
///   selection and scroll come back on pop).
/// - The wheel bus is read here once. Each event goes to the top screen's
///   [ScreenHandle.wheel]; what it does not take, the shell does: Menu goes
///   back, Previous, Next and Play/Pause drive the player. While a panel is
///   up, Select and Menu dismiss it and nothing else gets through.
/// - The bus's `atRoot` follows the stack (Home alone, no panel), so the
///   system back leaves the app only on Home.
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/panel/panel_overlay.dart';
import 'package:neupod/ui/screens/shell/screen_scope.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';

/// The stack, rooted at [home].
class ShellRoot extends ConsumerStatefulWidget {
  /// A shell whose first screen is built by [home].
  const ShellRoot({required this.home, super.key});

  /// Builds Home.
  final WidgetBuilder home;

  @override
  ConsumerState<ShellRoot> createState() => _ShellRootState();
}

class _ShellRootState extends ConsumerState<ShellRoot>
    with SingleTickerProviderStateMixin {
  static const _slide = Duration(milliseconds: 80);

  late final WheelBus _bus = ref.read(wheelBusProvider);
  late final AnimationController _anim = AnimationController(
    vsync: this,
    duration: _slide,
  );
  StreamSubscription<WheelEvent>? _sub;
  final Map<Key, ScreenHandle> _handles = {};

  /// The page sliding in (push) or out (pop), while [_anim] runs.
  ShellPage? _moving;
  bool _movingIn = true;

  /// A popped page kept on screen until it has slid out.
  ShellPage? _leaving;

  @override
  void initState() {
    super.initState();
    _sub = _bus.events.listen(_onEvent);
    _anim.addStatusListener((status) {
      if (status == AnimationStatus.completed && mounted) {
        setState(() {
          _moving = null;
          if (_leaving != null) _handles.remove(_leaving!.key);
          _leaving = null;
        });
      }
    });
    // Home cannot be set while the tree is building.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      ref
          .read(shellProvider.notifier)
          .setRoot(ShellPage(name: 'Home', builder: widget.home));
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    _anim.dispose();
    super.dispose();
  }

  ScreenHandle _handleFor(ShellPage page, {required bool top}) =>
      _handles.putIfAbsent(page.key, () => ScreenHandle(onTop: top));

  void _onStack(ShellState? prev, ShellState next) {
    final reduce = MediaQuery.disableAnimationsOf(context);
    final before = prev?.pages ?? const <ShellPage>[];
    final after = next.pages;

    if (after.length > before.length && before.isNotEmpty) {
      _moving = after.last;
      _movingIn = true;
      if (reduce) {
        _moving = null;
      } else {
        _anim.forward(from: 0);
      }
    } else if (after.length < before.length) {
      // Drop the handles of pages that are gone, but keep the leaving page
      // alive until it has slid out.
      final gone = before.where((p) => !after.contains(p)).toList();
      if (reduce) {
        for (final p in gone) {
          _handles.remove(p.key);
        }
      } else if (gone.isNotEmpty) {
        _leaving = gone.last;
        _moving = _leaving;
        _movingIn = false;
        for (final p in gone.take(gone.length - 1)) {
          _handles.remove(p.key);
        }
        _anim.forward(from: 0);
      }
    }

    for (final p in before) {
      if (!after.contains(p)) _handles[p.key]?.isTop.value = false;
    }
    for (final p in after) {
      _handleFor(p, top: false).isTop.value = identical(p, after.last);
    }
    _bus.atRoot.value = next.atRoot && next.panel == null;
  }

  void _onEvent(WheelEvent e) {
    final st = ref.read(shellProvider);
    final top = st.top;
    if (top == null) return;
    if (st.panel != null) {
      if (e is WheelPress &&
          (e.button == WheelButton.centre || e.button == WheelButton.menu)) {
        ref
            .read(shellProvider.notifier)
            .dismissPanel(confirmed: e.button == WheelButton.centre);
      }
      return;
    }
    if (_handles[top.key]?.wheel?.call(e) ?? false) return;
    if (e is! WheelPress) return;
    switch (e.button) {
      case WheelButton.menu:
        ref.read(shellProvider.notifier).pop();
      case WheelButton.previous:
        if (ref.read(playerProvider).state.current != null) {
          unawaited(ref.read(playerProvider).skipToPrevious());
        }
      case WheelButton.next:
        if (ref.read(playerProvider).state.current != null) {
          unawaited(ref.read(playerProvider).skipToNext());
        }
      case WheelButton.playPause:
        final player = ref.read(playerProvider);
        if (player.state.current == null) return;
        unawaited(player.state.playWhenReady ? player.pause() : player.play());
      case WheelButton.centre:
        break;
    }
  }

  @override
  Widget build(BuildContext context) {
    ref.listen(shellProvider, _onStack);
    final st = ref.watch(shellProvider);
    if (st.pages.isEmpty) return const SizedBox.shrink();

    final pages = st.pages;
    final children = <Widget>[];
    for (var i = 0; i < pages.length; i++) {
      final page = pages[i];
      final isTop = i == pages.length - 1;
      // Only the top two are ever on screen; the rest keep their state.
      final visible = i >= pages.length - 2;
      children.add(_slot(page, visible: visible, top: isTop));
    }
    if (_leaving != null) {
      children.add(_slot(_leaving!, visible: true, top: false));
    }

    return Directionality(
      textDirection: TextDirection.ltr,
      child: Stack(
        fit: StackFit.expand,
        children: [
          ExcludeSemantics(
            excluding: st.panel != null,
            child: AbsorbPointer(
              absorbing: _anim.isAnimating || st.panel != null,
              child: Stack(fit: StackFit.expand, children: children),
            ),
          ),
          if (st.panel != null)
            Positioned.fill(
              child: PanelOverlay(
                request: st.panel!,
                onOk: () => ref
                    .read(shellProvider.notifier)
                    .dismissPanel(confirmed: true),
              ),
            ),
        ],
      ),
    );
  }

  Widget _slot(ShellPage page, {required bool visible, required bool top}) {
    final handle = _handleFor(page, top: top);
    Widget child = Offstage(
      offstage: !visible,
      child: ScreenScope(
        handle: handle,
        child: Builder(builder: page.builder),
      ),
    );
    if (identical(page, _moving)) {
      child = AnimatedBuilder(
        animation: _anim,
        builder: (context, c) {
          final t = Curves.easeOut.transform(_anim.value);
          final dx = (_movingIn ? 1 - t : t) * 600;
          return Transform.translate(offset: Offset(dx, 0), child: c);
        },
        child: child,
      );
    }
    return KeyedSubtree(key: page.key, child: child);
  }
}

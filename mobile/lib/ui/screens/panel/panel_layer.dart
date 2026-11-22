// SPDX-License-Identifier: MPL-2.0

/// The centred panel as a layer over whatever screen is showing (UI styles
/// design, "Feedback: three places"): for something lost or about to be, never
/// for a routine confirmation. Select or Menu dismisses it; with direct touch
/// its `OK` can be tapped.
///
/// Any code raises one with `ref.read(panelProvider.notifier).show(...)`. The
/// shell puts a [PanelLayer] above its screens, inside the screen frame so the
/// style and the 600x450 canvas apply.
///
/// While a panel is up, screens must not act on the wheel: they check
/// [panelProvider] (a screen under the panel sees the very press that
/// dismisses it, because the layer clears the request one microtask later).
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/panel.dart';
import 'package:neupod/ui/device/wheel_bus.dart';

/// What a panel says.
@immutable
class PanelRequest {
  /// A panel with a [title] and a [message].
  const PanelRequest({
    required this.title,
    required this.message,
    this.okLabel = 'OK',
    this.onDismissed,
  });

  /// Something lost: the library update was stopped before it finished
  /// (UI styles design, the messages that move to the panel).
  const PanelRequest.libraryUpdateStopped({this.onDismissed})
    : title = 'Library update stopped',
      message =
          'Some songs may be missing. Update the library again to '
          'finish.',
      okLabel = 'OK';

  /// The title, in Body.
  final String title;

  /// The text under it.
  final String message;

  /// The button's label.
  final String okLabel;

  /// Called once, after the panel went away.
  final VoidCallback? onDismissed;
}

/// The panel being shown, or null.
class PanelNotifier extends Notifier<PanelRequest?> {
  @override
  PanelRequest? build() => null;

  /// Shows [request], replacing any panel up.
  void show(PanelRequest request) => state = request;

  /// Takes the panel away and tells its [PanelRequest.onDismissed].
  void dismiss() {
    final request = state;
    if (request == null) return;
    state = null;
    request.onDismissed?.call();
  }
}

/// The panel now up, or null.
final panelProvider = NotifierProvider<PanelNotifier, PanelRequest?>(
  PanelNotifier.new,
);

/// Shows [child] with the panel, when there is one, over a scrim.
class PanelLayer extends ConsumerStatefulWidget {
  /// A layer over [child].
  const PanelLayer({required this.child, super.key});

  /// The screen below.
  final Widget child;

  @override
  ConsumerState<PanelLayer> createState() => _PanelLayerState();
}

class _PanelLayerState extends ConsumerState<PanelLayer> {
  late final WheelBus _bus = ref.read(wheelBusProvider);
  StreamSubscription<WheelEvent>? _sub;
  WheelSemantics? _before;
  bool _dismissing = false;

  @override
  void initState() {
    super.initState();
    _sub = _bus.events.listen(_onEvent);
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  void _onEvent(WheelEvent e) {
    if (ref.read(panelProvider) == null) return;
    if (e is WheelPress &&
        (e.button == WheelButton.centre || e.button == WheelButton.menu) &&
        !_dismissing) {
      _dismiss();
    }
  }

  // One microtask later, so the screen below, which hears the same press, still
  // sees the panel and ignores it.
  void _dismiss() {
    _dismissing = true;
    scheduleMicrotask(() {
      _dismissing = false;
      if (mounted) ref.read(panelProvider.notifier).dismiss();
    });
  }

  void _describe(PanelRequest? request) {
    if (request != null) {
      _before ??= _bus.semantics.value;
      _bus.semantics.value = WheelSemantics(
        label: request.title,
        value: request.message,
        playing: _before!.playing,
      );
    } else if (_before != null) {
      _bus.semantics.value = _before!;
      _before = null;
    }
  }

  @override
  Widget build(BuildContext context) {
    final request = ref.watch(panelProvider);
    ref.listen(panelProvider, (_, next) => _describe(next));
    return Stack(
      fit: StackFit.expand,
      children: [
        ExcludeSemantics(excluding: request != null, child: widget.child),
        if (request != null)
          // The scrim and the panel's background take no touches (a tap on
          // them does nothing); only OK does.
          GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: () {},
            child: NeupodPanelScrim(
              panel: NeupodPanel(
                title: request.title,
                message: request.message,
                okLabel: request.okLabel,
                onOk: () => ref.read(panelProvider.notifier).dismiss(),
              ),
            ),
          ),
      ],
    );
  }
}

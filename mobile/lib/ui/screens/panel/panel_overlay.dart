// SPDX-License-Identifier: MPL-2.0

/// The centred panel over the screen (UI styles design, "Feedback: three
/// places"): for something lost or about to be, never for a routine
/// confirmation. The shell shows it for `shellProvider`'s `panel` and
/// dismisses it on Select or Menu (nothing else gets through while it is up);
/// with direct touch its `OK` can be tapped. A tap on the scrim or on the
/// panel's background does nothing.
///
/// While it is up the wheel's screen-reader text is the panel's (its title as
/// the label, its message as the value) and the screen's text comes back when
/// it goes. The screens below are not given the wheel, and the shell hides
/// them from the screen reader.
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/ui/components/panel.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/text_tier.dart';

/// The scrim and panel for [request], in the chosen style.
class PanelOverlay extends ConsumerStatefulWidget {
  /// Shows [request]; [onOk] dismisses it (a tap on OK).
  const PanelOverlay({required this.request, required this.onOk, super.key});

  /// What the panel says.
  final PanelRequest request;

  /// A tap on OK.
  final VoidCallback onOk;

  @override
  ConsumerState<PanelOverlay> createState() => _PanelOverlayState();
}

class _PanelOverlayState extends ConsumerState<PanelOverlay> {
  late final WheelBus _bus = ref.read(wheelBusProvider);
  WheelSemantics? _before;
  WheelSemantics? _mine;

  @override
  void initState() {
    super.initState();
    _before = _bus.semantics.value;
    // After this build: the wheel widget listens to the semantics.
    scheduleMicrotask(() {
      if (mounted) _say();
    });
  }

  @override
  void didUpdateWidget(PanelOverlay old) {
    super.didUpdateWidget(old);
    if (old.request != widget.request) {
      scheduleMicrotask(() {
        if (mounted) _say();
      });
    }
  }

  void _say() {
    _mine = WheelSemantics(
      label: widget.request.title,
      value: widget.request.message,
      playing: _before?.playing ?? false,
    );
    _bus.semantics.value = _mine!;
  }

  @override
  void dispose() {
    final before = _before;
    final bus = _bus;
    final mine = _mine;
    if (before != null) {
      // Not in the middle of tearing down the tree, where a listener could
      // not rebuild; and only if nobody has described the wheel since.
      scheduleMicrotask(() {
        try {
          if (mine == null || bus.semantics.value == mine) {
            bus.semantics.value = before;
          }
        } on FlutterError {
          // The bus is gone with the app.
        }
      });
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final choice = ref.watch(styleChoiceProvider);
    final mode = effectiveMode(
      choice,
      MediaQuery.platformBrightnessOf(context),
      look: ref.watch(activeLookProvider),
    );
    final style = NeupodStyles.resolve(
      choice.family,
      mode,
      art: ref.watch(ambientPaletteProvider),
    );
    final tier = TextTier.of(context);
    final request = widget.request;
    return NeupodTheme(
      style: style,
      tier: tier,
      child: DefaultTextStyle(
        style: NeupodType(tier).body(style.tokens.ink),
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: () {},
          child: NeupodPanelScrim(
            panel: NeupodPanel(
              title: request.title,
              message: request.message,
              okLabel: request.okLabel,
              onOk: widget.onOk,
            ),
          ),
        ),
      ),
    );
  }
}

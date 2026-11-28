// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/ui/device/device_paint.dart';

/// A single pulse of one part's edge on the device (design section 2: which
/// part is being edited is said by a pulse of its edge, 400 ms, from its
/// usual strength to full ink and back). [t] runs 0 to 1.
@immutable
class EdgePulse {
  const EdgePulse(this.part, this.t);

  final LookPart part;
  final double t;

  /// 0 at the ends, 1 in the middle.
  double get strength => 1 - (2 * t - 1).abs();

  @override
  bool operator ==(Object other) =>
      other is EdgePulse && other.part == part && other.t == t;

  @override
  int get hashCode => Object.hash(part, t);
}

/// How long the pulse lasts.
const Duration edgePulseDuration = Duration(milliseconds: 400);

/// The pulse in progress, or null. The part editor drives it (never under
/// Reduce Motion); the device view applies it.
class EdgePulseNotifier extends Notifier<EdgePulse?> {
  @override
  EdgePulse? build() => null;

  /// Sets the pulse.
  void set(EdgePulse? pulse) => state = pulse;
}

/// The pulse in progress.
final edgePulseProvider = NotifierProvider<EdgePulseNotifier, EdgePulse?>(
  EdgePulseNotifier.new,
);

Color _toFull(Color c, double strength) =>
    c.withValues(alpha: (c.a + (1 - c.a) * strength).clamp(0.0, 1.0));

/// [paint] with the edge of the pulsing part drawn up towards full ink.
DevicePaint applyEdgePulse(DevicePaint paint, EdgePulse? pulse) {
  if (pulse == null) return paint;
  final s = pulse.strength;
  switch (pulse.part) {
    case LookPart.body:
      return paint.withEdges(
        frameShadow: [
          for (final b in paint.frameShadow)
            BoxShadow(
              color: _toFull(b.color, s),
              offset: b.offset,
              blurRadius: b.blurRadius,
              spreadRadius: b.spreadRadius,
            ),
        ],
      );
    case LookPart.wheel:
      return paint.withEdges(ringEdge: _toFull(paint.ringEdge, s));
    case LookPart.centre:
      return paint.withEdges(centreEdge: _toFull(paint.centreEdge, s));
  }
}

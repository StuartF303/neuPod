// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

/// Takes pointer hits only where [accepts] says so, without touching the
/// semantics of its child (unlike IgnorePointer, which also blocks the
/// screen reader's actions): the screen's rows stay activatable by the
/// screen reader.
class HitGate extends SingleChildRenderObjectWidget {
  /// A gate that takes hits at local positions where [accepts] is true.
  const HitGate({required this.accepts, super.key, super.child});

  /// Whether a local position is taken.
  final bool Function(Offset local, Size size) accepts;

  @override
  RenderHitGate createRenderObject(BuildContext context) =>
      RenderHitGate(accepts);

  @override
  void updateRenderObject(BuildContext context, RenderHitGate renderObject) {
    renderObject.accepts = accepts;
  }
}

/// The render object of [HitGate].
class RenderHitGate extends RenderProxyBox {
  /// A gate with [accepts].
  RenderHitGate(this.accepts);

  /// Whether a local position is taken.
  bool Function(Offset local, Size size) accepts;

  @override
  bool hitTest(BoxHitTestResult result, {required Offset position}) {
    if (!accepts(position, size)) return false;
    return super.hitTest(result, position: position);
  }
}

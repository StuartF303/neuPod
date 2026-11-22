// SPDX-License-Identifier: MPL-2.0

/// A column taller than its space, scrolled just far enough to keep one item
/// in view (the wheel moves a selection; the list follows).
library;

import 'package:flutter/widgets.dart';

/// Shows [children], whose heights are [heights], scrolled so that item
/// [selected] is fully visible. Item 0 at the top shows from the very top (a
/// heading above the first row stays visible).
class ScrollWindow extends StatelessWidget {
  /// A window on [children].
  const ScrollWindow({
    required this.children,
    required this.heights,
    required this.selected,
    super.key,
  }) : assert(children.length == heights.length, 'a height for each child');

  /// The items, top to bottom.
  final List<Widget> children;

  /// Each item's height in design px.
  final List<double> heights;

  /// The item to keep in view; null for none (the top shows).
  final int? selected;

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, c) {
      var offset = 0.0;
      final s = selected;
      if (s != null && s > 0 && s < children.length) {
        var top = 0.0;
        for (var i = 0; i < s; i++) {
          top += heights[i];
        }
        final bottom = top + heights[s];
        if (bottom > c.maxHeight) offset = bottom - c.maxHeight;
        if (top < offset) offset = top;
      }
      return ClipRect(
        child: OverflowBox(
          alignment: Alignment.topLeft,
          minHeight: 0,
          maxHeight: double.infinity,
          child: Transform.translate(
            offset: Offset(0, -offset),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: children,
            ),
          ),
        ),
      );
    },
  );
}

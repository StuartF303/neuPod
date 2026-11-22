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

/// Like [ScrollWindow] for a long list: only the items in view (and one
/// either side) are built, so a result list of thousands costs the same as one
/// of ten. Heights come from [heightOf], which must be cheap.
class LazyScrollWindow extends StatelessWidget {
  /// A window on [itemCount] items.
  const LazyScrollWindow({
    required this.itemCount,
    required this.heightOf,
    required this.itemBuilder,
    required this.selected,
    super.key,
  });

  /// How many items there are.
  final int itemCount;

  /// The height of item `i` in design px.
  final double Function(int i) heightOf;

  /// Builds item `i`, only when it is in view.
  final Widget Function(int i) itemBuilder;

  /// The item to keep in view; null for none (the top shows).
  final int? selected;

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, c) {
      final tops = List<double>.filled(itemCount + 1, 0);
      for (var i = 0; i < itemCount; i++) {
        tops[i + 1] = tops[i] + heightOf(i);
      }
      var offset = 0.0;
      final s = selected;
      if (s != null && s > 0 && s < itemCount) {
        if (tops[s + 1] > c.maxHeight) offset = tops[s + 1] - c.maxHeight;
        if (tops[s] < offset) offset = tops[s];
      }
      // The first item that ends below the top edge, the last that starts
      // above the bottom edge, one more each side.
      var first = 0;
      while (first < itemCount && tops[first + 1] <= offset) {
        first++;
      }
      var last = first;
      while (last < itemCount - 1 && tops[last + 1] < offset + c.maxHeight) {
        last++;
      }
      first = first > 0 ? first - 1 : 0;
      last = last < itemCount - 1 ? last + 1 : last;
      return ClipRect(
        child: OverflowBox(
          alignment: Alignment.topLeft,
          minHeight: 0,
          maxHeight: double.infinity,
          child: Transform.translate(
            offset: Offset(0, tops[first] - offset),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                if (itemCount > 0)
                  for (var i = first; i <= last; i++) itemBuilder(i),
              ],
            ),
          ),
        ),
      );
    },
  );
}

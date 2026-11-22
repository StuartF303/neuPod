// SPDX-License-Identifier: MPL-2.0

/// The list every browsing screen is made of: rows of any height, one sliding
/// selection bar, the wheel, direct touch, the scroll bar and the fast-scroll
/// letter tile.
///
/// - **Wheel.** `WheelStep` moves the selection that many selectable rows
///   (the steps are already accelerated); Select activates; the detent haptic
///   plays for a step that moved, the end haptic once when pushed past an end.
///   Menu, Previous, Next and Play/Pause are left to the shell.
/// - **Bar.** Slides to an adjacent row in 80 ms; a move of more than three
///   rows jumps; nothing animates under Reduce Motion (device view section
///   5 and 9).
/// - **Window.** Whole rows only, as on the device: the list shows as many
///   whole rows as fit (7 one-line or 5 two-line at the standard tier), so a
///   partly visible row is never drawn.
/// - **Touch (candidate 2).** A tap selects the row and activates it, with
///   the pressed tint from touch-down; a drag scrolls and keeps the selection
///   on the nearest visible row. Under candidate 1 the host takes no pointer
///   hits; the rows' semantics still open them.
/// - **Fast scroll.** `LetterJump`, the firmware's rule, shows the tile and
///   rail on long lists and hides them after 250 ms at rest with a 150 ms
///   fade.
/// - **Screen reader.** Each row is one element (`NeupodRow` does it); the
///   wheel's value is the selected row, `Albums, 4 of 7`.
library;

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/wheel/letter_jump.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/small_parts.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/shell/screen_scope.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// What a row's builder is told.
class ItemState {
  /// The state of one item.
  const ItemState({
    required this.selection,
    required this.pressed,
    required this.onTap,
    required this.position,
  });

  /// [RowSelection.selected] on the selected row.
  final RowSelection selection;

  /// Under a finger.
  final bool pressed;

  /// Selects the row and activates it; give it to the row's `onTap`.
  final VoidCallback? onTap;

  /// `4 of 7` among the selectable rows, for the screen reader; null on a
  /// row that cannot be selected.
  final String? position;
}

/// One item of a [NeupodList]: a row, or a heading or header that the wheel
/// skips.
class NeupodListItem {
  /// An item of [height] design px drawn by [builder].
  const NeupodListItem({
    required this.height,
    required this.builder,
    this.selectable = true,
    this.twoLine = false,
    this.initial,
    this.label = '',
    this.onActivate,
  });

  /// A row of the list's text tier, from the common parts.
  factory NeupodListItem.row({
    required double height,
    required String title,
    String? subtitle,
    String? number,
    String? value,
    bool chevron = false,
    Widget Function(bool selected)? thumbnail,
    String? disabledReason,
    String? initial,
    VoidCallback? onActivate,
  }) {
    final greyed = disabledReason != null;
    return NeupodListItem(
      height: height,
      selectable: !greyed,
      twoLine: subtitle != null,
      initial: initial,
      label: [?number, title, ?subtitle, ?value].join(', '),
      onActivate: onActivate,
      builder: (context, s) => NeupodRow(
        title: title,
        subtitle: subtitle,
        number: number,
        value: value,
        chevron: chevron,
        thumbnail: thumbnail?.call(s.selection != RowSelection.none),
        disabledReason: disabledReason,
        selection: s.selection,
        pressed: s.pressed,
        position: s.position,
        paintsBar: false,
        onTap: s.onTap,
      ),
    );
  }

  final double height;
  final bool selectable;
  final bool twoLine;

  /// The first letter of the row's sort key, for the fast-scroll tile.
  final String? initial;

  /// What the screen reader's wheel says for this row.
  final String label;
  final VoidCallback? onActivate;
  final Widget Function(BuildContext context, ItemState state) builder;
}

/// A band as an item: [child] is drawn at the item's height.
NeupodListItem bandItem({required double height, required Widget child}) =>
    NeupodListItem(height: height, selectable: false, builder: (_, _) => child);

/// The list.
class NeupodList extends ConsumerStatefulWidget {
  /// A list of [items].
  const NeupodList({
    required this.items,
    this.initialSelection,
    this.onSelectionChanged,
    this.indexed = true,
    this.rightInset = 0,
    super.key,
  });

  final List<NeupodListItem> items;

  /// The item selected first; the first selectable one if null.
  final int? initialSelection;

  /// Called when the selection moves, with the item's index.
  final ValueChanged<int>? onSelectionChanged;

  /// Whether this list shows the fast-scroll tile and rail.
  final bool indexed;

  /// Space kept free at the right (a pane beside the list needs none).
  final double rightInset;

  @override
  ConsumerState<NeupodList> createState() => _NeupodListState();
}

class _NeupodListState extends ConsumerState<NeupodList>
    with SingleTickerProviderStateMixin {
  static const _slide = Duration(milliseconds: 80);

  late final WheelBus _bus = ref.read(wheelBusProvider);
  ScreenHandle? _handle;

  int _sel = -1;
  double _scroll = 0;
  bool _animate = false;
  int _pressed = -1;
  bool _atEnd = false;
  bool _placed = false;

  final LetterJump _jump = LetterJump();
  Ticker? _ticker;
  bool _tileOn = false;

  // Layout, rebuilt with the items.
  List<double> _tops = const [];
  double _total = 0;
  double _viewport = 0;

  List<NeupodListItem> get _items => widget.items;

  @override
  void initState() {
    super.initState();
    _sel = _firstSelectable(widget.initialSelection ?? 0);
    _layout();
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final handle = ScreenScope.maybeOf(context);
    if (handle != _handle) {
      _handle?.isTop.removeListener(_describe);
      if (_handle?.wheel == _onWheel) _handle?.wheel = null;
      _handle = handle;
      handle?.wheel = _onWheel;
      handle?.isTop.addListener(_describe);
    }
    _scheduleDescribe();
  }

  @override
  void didUpdateWidget(NeupodList old) {
    super.didUpdateWidget(old);
    _layout();
    if (_sel < 0 || _sel >= _items.length || !_items[_sel].selectable) {
      _sel = _firstSelectable(_sel < 0 ? 0 : _sel);
    }
    _scheduleDescribe();
  }

  @override
  void dispose() {
    _ticker?.dispose();
    _handle?.isTop.removeListener(_describe);
    if (_handle?.wheel == _onWheel) _handle?.wheel = null;
    super.dispose();
  }

  // -- layout ---------------------------------------------------------

  void _layout() {
    final tops = List<double>.filled(_items.length, 0);
    var y = 0.0;
    for (var i = 0; i < _items.length; i++) {
      tops[i] = y;
      y += _items[i].height;
    }
    _tops = tops;
    _total = y;
  }

  int _firstSelectable(int from) {
    for (var i = from.clamp(0, _items.length); i < _items.length; i++) {
      if (_items[i].selectable) return i;
    }
    for (var i = (from - 1).clamp(0, _items.length - 1); i >= 0; i--) {
      if (_items[i].selectable) return i;
    }
    return -1;
  }

  int get _selectableCount => _items.where((i) => i.selectable).length;

  int _ordinal(int index) {
    var n = 0;
    for (var i = 0; i <= index; i++) {
      if (_items[i].selectable) n++;
    }
    return n;
  }

  /// The index of the item at design-px [y] (clamped).
  int _indexAt(double y) {
    if (_items.isEmpty) return 0;
    var lo = 0;
    var hi = _items.length - 1;
    while (lo < hi) {
      final mid = (lo + hi + 1) ~/ 2;
      if (_tops[mid] <= y) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }
    return lo;
  }

  /// The height of the whole rows that fit in the viewport from [scroll].
  double _windowAt(double scroll) {
    if (_items.isEmpty || _viewport <= 0) return _viewport;
    final first = _indexAt(scroll + 0.5);
    if ((scroll - _tops[first]).abs() > 0.5) return _viewport; // mid-drag
    var sum = 0.0;
    for (var i = first; i < _items.length; i++) {
      if (sum + _items[i].height > _viewport + 0.01) break;
      sum += _items[i].height;
    }
    return sum == 0 ? _viewport : sum;
  }

  /// The scroll position that shows item [i], moving as little as the device
  /// does: up to the heading above it, or down to end the window on it.
  double _scrollFor(int i, double current) {
    var run = i;
    while (run > 0 && !_items[run - 1].selectable) {
      run--;
    }
    final runTop = _tops[run];
    if (runTop < current - 0.5) return runTop;
    final bottom = _tops[i] + _items[i].height;
    if (bottom <= current + _windowAt(current) + 0.5) return current;
    var j = i;
    var sum = _items[i].height;
    while (j > 0 && sum + _items[j - 1].height <= _viewport + 0.01) {
      j--;
      sum += _items[j].height;
    }
    return _tops[j];
  }

  // -- selection ------------------------------------------------------

  /// Moves [steps] selectable rows, skipping the rest.
  void _move(int steps) {
    if (_sel < 0 || steps == 0) return;
    var to = _sel;
    var moved = 0;
    final dir = steps > 0 ? 1 : -1;
    var left = steps.abs();
    var i = _sel;
    while (left > 0) {
      i += dir;
      if (i < 0 || i >= _items.length) break;
      if (_items[i].selectable) {
        to = i;
        left--;
        moved++;
      }
    }
    final feedback = ref.read(deviceFeedbackProvider);
    if (to == _sel) {
      if (!_atEnd) feedback.endReached();
      _atEnd = true;
      return;
    }
    _atEnd = false;
    feedback.detent();
    _select(to, animate: moved <= 3);

    if (widget.indexed) {
      final now = ref.read(wheelClockProvider)();
      _jump.scroll(steps, now, _items[to].initial ?? '#');
      if (_jump.active) {
        _tileOn = true;
        _startTicker();
      }
    }
  }

  void _select(int to, {required bool animate}) {
    setState(() {
      _sel = to;
      _animate = animate && !_reduceMotion;
      _scroll = _scrollFor(to, _scroll);
    });
    widget.onSelectionChanged?.call(to);
    _scheduleDescribe();
  }

  bool get _reduceMotion => MediaQuery.disableAnimationsOf(context);

  void _activate(int i) {
    if (i < 0 || i >= _items.length) return;
    _items[i].onActivate?.call();
  }

  bool _onWheel(WheelEvent e) {
    switch (e) {
      case WheelStep(:final steps):
        _move(steps);
        return true;
      case WheelPress(button: WheelButton.centre):
        _activate(_sel);
        return true;
      case _:
        return false;
    }
  }

  void _tap(int i) {
    if (!_items[i].selectable) return;
    if (i != _sel) _select(i, animate: false);
    _activate(i);
  }

  // -- touch ----------------------------------------------------------

  void _drag(double dy) {
    final max = (_total - _viewport).clamp(0.0, double.infinity);
    final next = (_scroll - dy).clamp(0.0, max);
    if (next == _scroll) return;
    setState(() {
      _animate = false;
      _scroll = next;
      // Keep the selection on the nearest row that is in view.
      final win = _windowAt(_scroll);
      final lo = _scroll;
      final hi = _scroll + win;
      if (_sel >= 0) {
        final top = _tops[_sel];
        final bottom = top + _items[_sel].height;
        if (top < lo - 0.5 || bottom > hi + 0.5) {
          final target = top < lo ? lo : hi - 1;
          final from = _indexAt(target);
          final near = _nearestSelectable(from, towardsEnd: top >= lo);
          if (near >= 0) {
            _sel = near;
            widget.onSelectionChanged?.call(near);
          }
        }
      }
    });
    _scheduleDescribe();
  }

  int _nearestSelectable(int from, {required bool towardsEnd}) {
    final step = towardsEnd ? -1 : 1;
    // Prefer the row inside the window nearest the one that left.
    for (var i = from; i >= 0 && i < _items.length; i += step) {
      if (_items[i].selectable) return i;
    }
    for (var i = from; i >= 0 && i < _items.length; i -= step) {
      if (_items[i].selectable) return i;
    }
    return -1;
  }

  // -- the letter tile ------------------------------------------------

  void _startTicker() {
    _ticker ??= createTicker((_) {
      _jump.tick(ref.read(wheelClockProvider)());
      if (!_jump.active) {
        _ticker?.stop();
        if (mounted) setState(() => _tileOn = false);
      }
    });
    if (!_ticker!.isActive) _ticker!.start();
  }

  // -- the screen reader's wheel --------------------------------------

  bool _describeQueued = false;

  void _scheduleDescribe() {
    if (_describeQueued) return;
    _describeQueued = true;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _describeQueued = false;
      if (mounted) _describe();
    });
  }

  void _describe() {
    if (!mounted) return;
    if (_handle != null && !_handle!.isTop.value) return;
    final n = _selectableCount;
    String say(int i) => '${_items[i].label}, ${_ordinal(i)} of $n';
    int neighbour(int dir) {
      for (var i = _sel + dir; i >= 0 && i < _items.length; i += dir) {
        if (_items[i].selectable) return i;
      }
      return -1;
    }

    final next = _sel < 0 ? -1 : neighbour(1);
    final prev = _sel < 0 ? -1 : neighbour(-1);
    final playing = ref.read(playerStateProvider).value?.playWhenReady ?? false;
    _bus.semantics.value = WheelSemantics(
      value: _sel < 0 ? '' : say(_sel),
      increasedValue: next < 0 ? '' : say(next),
      decreasedValue: prev < 0 ? '' : say(prev),
      playing: playing,
    );
  }

  // -- build ----------------------------------------------------------

  @override
  Widget build(BuildContext context) {
    final style = NeupodTheme.of(context).style;
    final reduce = _reduceMotion;
    final dur = _animate && !reduce ? _slide : Duration.zero;
    final n = _selectableCount;

    return LayoutBuilder(
      builder: (context, c) {
        _viewport = c.maxHeight;
        if (!_placed && _sel >= 0) {
          _placed = true;
          _scroll = _scrollFor(_sel, 0);
        }
        final overflow = _total > _viewport + 0.5;
        final window = _windowAt(_scroll);
        final barWidth = style.scrollBar.width;
        final selectedItem = _sel >= 0 ? _items[_sel] : null;

        // The rows near the window (a few more each side, so a slide in
        // progress still has its rows).
        final first = (_indexAt(_scroll) - 4).clamp(0, _items.length);
        final last = (_indexAt(_scroll + window) + 4).clamp(0, _items.length);

        final rows = <Widget>[
          if (selectedItem != null)
            AnimatedPositioned(
              key: const ValueKey('bar'),
              duration: dur,
              curve: Curves.easeOut,
              left: 0,
              right: 0,
              top: _tops[_sel],
              height: selectedItem.height,
              child: SelectionBar(twoLine: selectedItem.twoLine),
            ),
          for (var i = first; i < last; i++)
            Positioned(
              key: ValueKey(i),
              left: 0,
              right: 0,
              top: _tops[i],
              height: _items[i].height,
              child: _buildItem(context, i, n),
            ),
        ];

        final content = ClipRect(
          child: SizedBox(
            height: window,
            child: GestureDetector(
              behavior: HitTestBehavior.translucent,
              onVerticalDragStart: (_) => setState(() => _pressed = -1),
              onVerticalDragUpdate: (d) => _drag(d.delta.dy),
              child: Stack(
                clipBehavior: Clip.hardEdge,
                children: [
                  AnimatedPositioned(
                    duration: dur,
                    curve: Curves.easeOut,
                    left: 0,
                    right: 0,
                    top: -_scroll,
                    height: _total,
                    child: Stack(clipBehavior: Clip.none, children: rows),
                  ),
                ],
              ),
            ),
          ),
        );

        final rail = widget.indexed ? _rail(selectedItem) : null;
        return Stack(
          children: [
            Positioned.fill(
              child: Padding(
                padding: EdgeInsets.only(
                  right: widget.rightInset + (overflow ? barWidth : 0),
                ),
                child: Align(alignment: Alignment.topCenter, child: content),
              ),
            ),
            if (overflow)
              Positioned(
                right: widget.rightInset,
                top: 0,
                bottom: 0,
                child: NeupodScrollBar(
                  visible: window / _total,
                  position: _total - window <= 0
                      ? 0
                      : (_scroll / (_total - window)).clamp(0.0, 1.0),
                ),
              ),
            if (rail != null)
              Positioned.fill(
                child: IgnorePointer(
                  child: AnimatedOpacity(
                    opacity: _tileOn ? 1 : 0,
                    // A move during the fade shows it at full strength at
                    // once; the fade itself is the rail's 150 ms ease-in.
                    duration: _tileOn ? Duration.zero : _fade,
                    curve: Curves.easeIn,
                    child: rail,
                  ),
                ),
              ),
          ],
        );
      },
    );
  }

  static const _fade = Duration(milliseconds: 150);

  Widget _buildItem(BuildContext context, int i, int selectableCount) {
    final item = _items[i];
    final selected = i == _sel;
    return Listener(
      behavior: HitTestBehavior.translucent,
      onPointerDown: item.selectable
          ? (_) => setState(() => _pressed = i)
          : null,
      onPointerUp: (_) {
        if (_pressed == i) setState(() => _pressed = -1);
      },
      onPointerCancel: (_) {
        if (_pressed == i) setState(() => _pressed = -1);
      },
      child: item.builder(
        context,
        ItemState(
          selection: selected ? RowSelection.selected : RowSelection.none,
          pressed: _pressed == i,
          onTap: item.selectable ? () => _tap(i) : null,
          position: item.selectable
              ? '${_ordinal(i)} of $selectableCount'
              : null,
        ),
      ),
    );
  }

  Widget? _rail(NeupodListItem? selected) {
    final letter = LetterJump.normaliseInitial(_jump.letter);
    return FastScrollIndex(letter: letter, rail: railFor(letter));
  }

  /// The rail's letters: every second letter, with the current one in its
  /// place (as drawn, `A C E G H K M O Q S U W Y`).
  static String railFor(String letter) {
    const base = 'ACEGIKMOQSUWY';
    if (letter == '#') return '#$base';
    if (base.contains(letter)) return base;
    final code = letter.codeUnitAt(0);
    final out = StringBuffer();
    var placed = false;
    for (final c in base.codeUnits) {
      if (c == code + 1) continue; // the next letter makes room
      if (!placed && c > code) {
        out.writeCharCode(code);
        placed = true;
      }
      out.writeCharCode(c);
    }
    if (!placed) out.writeCharCode(code);
    return out.toString();
  }
}

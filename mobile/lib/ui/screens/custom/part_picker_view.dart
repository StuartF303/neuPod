// SPDX-License-Identifier: MPL-2.0

/// The part editor's drawing (design section 2), at the three text tiers
/// (section 8). Pure drawing of what it is given: the screen
/// (`part_picker_screen.dart`) holds the state and the wheel.
///
/// | Tier | Layout |
/// | --- | --- |
/// | Standard | part tabs, colour wheel 250, swatch and hex, four fields 60 high, the legend note, the hint |
/// | Large | the hint goes; fields 66 high; colour wheel 230 |
/// | Largest | the tabs go; the right column is the focused field alone (name, value, bar, `1 of 4`); colour wheel 230; the note stays |
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/search_entry.dart';
import 'package:neupod/ui/screens/custom/colour_wheel.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';

/// The names of the four fields, in focus order.
const List<String> fieldNames = [
  'Hue',
  'Saturation',
  'Brightness',
  'Transparency',
];

/// The part's name on its tab and in the status bar.
String partName(LookPart p) => switch (p) {
  LookPart.body => 'Body',
  LookPart.wheel => 'Wheel',
  LookPart.centre => 'Centre',
};

/// The hint under the note at the standard tier.
const String pickerHint =
    'Scroll: change · Prev/Next: field · Select: next part';

/// A field's value as written: `205°` or `62 %`.
String fieldValueText(int field, int v) => field == 0 ? '$v°' : '$v %';

/// A field's value as the screen reader says it.
String fieldValueWords(int field, int v) =>
    field == 0 ? '$v degrees' : '$v percent';

/// The editor.
class PartPickerView extends StatelessWidget {
  /// The editor for [part], painted [paint], with [field] focused.
  const PartPickerView({
    required this.part,
    required this.paint,
    required this.field,
    required this.note,
    required this.transparencyLocked,
    required this.onPart,
    required this.onField,
    required this.onValue,
    required this.onColour,
    super.key,
  });

  final LookPart part;
  final PartPaint paint;

  /// 0 to 3: Hue, Saturation, Brightness, Transparency.
  final int field;

  /// The legend note, or what the rule did.
  final String note;

  /// Reduce Transparency (or Increase Contrast) is on: the Transparency field
  /// is greyed.
  final bool transparencyLocked;

  final ValueChanged<LookPart> onPart;
  final ValueChanged<int> onField;

  /// A bar was touched: the field and its new value.
  final void Function(int field, int value) onValue;
  final ColourPicked onColour;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final k = theme.tier.k;
    final largest = k > 1.3;
    final large = k > 1.05;
    final type = NeupodType(theme.tier);
    final tokens = theme.style.tokens;
    final disc = large ? 230.0 : 250.0;
    final fieldHeight = large ? 66.0 : 60.0;

    final tabs = largest
        ? const SizedBox.shrink()
        : SizedBox(
            height: 52,
            child: Align(
              alignment: Alignment.centerLeft,
              child: Padding(
                padding: const EdgeInsets.only(left: 19),
                child: ModeChips(
                  labels: [for (final p in LookPart.values) partName(p)],
                  active: part.index,
                  onTap: (i) => onPart(LookPart.values[i]),
                ),
              ),
            ),
          );

    final wheel = ColourWheel(
      hue: paint.hue,
      saturation: paint.saturation,
      brightness: paint.brightness,
      size: disc,
      onPicked: onColour,
    );

    final Widget right = largest
        ? _FocusedField(
            field: field,
            paint: paint,
            part: part,
            locked: field == 3 && transparencyLocked,
            onValue: onValue,
          )
        : _Fields(
            field: field,
            paint: paint,
            part: part,
            height: fieldHeight,
            transparencyLocked: transparencyLocked,
            onField: onField,
            onValue: onValue,
          );

    final noteBand = SizedBox(
      height: largest ? 48.0 : 30.0,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 19),
        child: Align(
          alignment: Alignment.centerLeft,
          child: Text(
            note,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: type.small(theme.style.list.secondary),
          ),
        ),
      ),
    );

    return ListGround(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          tabs,
          Expanded(
            child: Padding(
              padding: EdgeInsets.fromLTRB(19, 0, largest ? 19 : 7, 0),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Align(
                    alignment: largest ? Alignment.center : Alignment.topCenter,
                    child: SizedBox(
                      width: disc,
                      child: Center(child: wheel),
                    ),
                  ),
                  SizedBox(width: largest ? 28 : 16),
                  Expanded(child: right),
                ],
              ),
            ),
          ),
          noteBand,
          if (!large)
            SizedBox(
              height: 36,
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 19),
                child: Align(
                  alignment: Alignment.centerLeft,
                  child: Text(
                    pickerHint,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: type.small(tokens.ink3),
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }
}

/// The swatch and hex band, then the four fields.
class _Fields extends StatelessWidget {
  const _Fields({
    required this.field,
    required this.paint,
    required this.part,
    required this.height,
    required this.transparencyLocked,
    required this.onField,
    required this.onValue,
  });

  final int field;
  final PartPaint paint;
  final LookPart part;
  final double height;
  final bool transparencyLocked;
  final ValueChanged<int> onField;
  final void Function(int field, int value) onValue;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final type = NeupodType(theme.tier);
    final rgb = hsvToRgb(paint.hue, paint.saturation, paint.brightness);
    return Column(
      children: [
        SizedBox(
          height: 40,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Row(
              children: [
                Container(
                  width: 30,
                  height: 30,
                  decoration: BoxDecoration(
                    color: Color(0xFF000000 | rgb),
                    borderRadius: BorderRadius.circular(4),
                    border: Border.all(color: const Color(0xFF9DA2A9)),
                  ),
                ),
                const SizedBox(width: 10),
                Text(hexOf(rgb), style: type.small(theme.style.list.secondary)),
              ],
            ),
          ),
        ),
        for (var i = 0; i < 4; i++)
          SizedBox(
            height: height,
            child: _FieldRow(
              index: i,
              paint: paint,
              part: part,
              selected: i == field,
              locked: i == 3 && transparencyLocked,
              onField: onField,
              onValue: onValue,
            ),
          ),
      ],
    );
  }
}

/// One field: name, value, bar and knob; the focused one sits on the
/// selection bar.
class _FieldRow extends StatelessWidget {
  const _FieldRow({
    required this.index,
    required this.paint,
    required this.part,
    required this.selected,
    required this.locked,
    required this.onField,
    required this.onValue,
  });

  final int index;
  final PartPaint paint;
  final LookPart part;
  final bool selected;
  final bool locked;
  final ValueChanged<int> onField;
  final void Function(int field, int value) onValue;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final style = theme.style;
    final type = NeupodType(theme.tier);
    final sel = style.selection;
    final value = fieldValue(paint, index);
    final ink = selected ? sel.text : style.tokens.ink;
    final shadows = selected ? sel.textShadows : const <Shadow>[];
    TextStyle text(Color c) {
      final s = type.small(c);
      return shadows.isEmpty ? s : s.copyWith(shadows: shadows);
    }

    final name = fieldNames[index];
    final label = locked
        ? '$name, ${fieldValueWords(index, value)}, unavailable'
        : '$name, ${fieldValueWords(index, value)}';
    Widget row = Stack(
      children: [
        if (selected) Positioned.fill(child: SelectionBar(twoLine: false)),
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 6, 12, 0),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Row(
                children: [
                  Expanded(child: Text(name, style: text(ink))),
                  Text(fieldValueText(index, value), style: text(ink)),
                ],
              ),
              const SizedBox(height: 5),
              FieldBar(
                field: index,
                paint: paint,
                part: part,
                edge: selected
                    ? const Color(0xFFFFFFFF)
                    : const Color(0xFF8C9199),
                enabled: !locked,
                onFocus: () => onField(index),
                onValue: (v) => onValue(index, v),
              ),
            ],
          ),
        ),
      ],
    );
    if (locked) row = Opacity(opacity: 0.4, child: row);
    return MergeSemantics(
      child: Semantics(
        label: label,
        selected: selected,
        button: true,
        enabled: !locked,
        onTap: () => onField(index),
        child: ExcludeSemantics(
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: () => onField(index),
            child: row,
          ),
        ),
      ),
    );
  }
}

/// The largest tier's right column: the focused field alone.
class _FocusedField extends StatelessWidget {
  const _FocusedField({
    required this.field,
    required this.paint,
    required this.part,
    required this.locked,
    required this.onValue,
  });

  final int field;
  final PartPaint paint;
  final LookPart part;
  final bool locked;
  final void Function(int field, int value) onValue;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final type = NeupodType(theme.tier);
    final style = theme.style;
    final value = fieldValue(paint, field);
    Widget col = Column(
      mainAxisAlignment: MainAxisAlignment.center,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(fieldNames[field], style: type.small(style.list.secondary)),
        Text(fieldValueText(field, value), style: type.large(style.tokens.ink)),
        const SizedBox(height: 8),
        FieldBar(
          field: field,
          paint: paint,
          part: part,
          edge: const Color(0xFF8C9199),
          enabled: !locked,
          onFocus: () {},
          onValue: (v) => onValue(field, v),
        ),
        const SizedBox(height: 14),
        Text('${field + 1} of 4', style: type.small(style.list.secondary)),
      ],
    );
    if (locked) col = Opacity(opacity: 0.4, child: col);
    return Semantics(
      label:
          '${fieldNames[field]}, ${fieldValueWords(field, value)}, '
          '${field + 1} of 4',
      child: ExcludeSemantics(child: col),
    );
  }
}

/// A field's 10 px bar with its 18 px knob: the rainbow for hue; for the rest
/// the range the field covers at the other values. Touch sets the field.
class FieldBar extends StatelessWidget {
  /// The bar of [field].
  const FieldBar({
    required this.field,
    required this.paint,
    required this.part,
    required this.edge,
    required this.enabled,
    required this.onFocus,
    required this.onValue,
    super.key,
  });

  final int field;
  final PartPaint paint;
  final LookPart part;
  final Color edge;
  final bool enabled;

  /// A touch began on the bar.
  final VoidCallback onFocus;
  final ValueChanged<int> onValue;

  static const double knob = 18;

  /// The value at [dx] along a bar [width] wide.
  static int valueAt(double dx, double width, int max) {
    final f = ((dx - knob / 2) / (width - knob)).clamp(0.0, 1.0);
    return (f * max).round();
  }

  @override
  Widget build(BuildContext context) {
    final max = fieldRule(field, part).max;
    final value = fieldValue(paint, field);
    return LayoutBuilder(
      builder: (context, c) {
        final w = c.maxWidth;
        final frac = value / max;
        void touch(Offset p) {
          if (!enabled) return;
          onValue(valueAt(p.dx, w, max));
        }

        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTapDown: (d) {
            onFocus();
            touch(d.localPosition);
          },
          onHorizontalDragStart: (_) => onFocus(),
          onHorizontalDragUpdate: (d) => touch(d.localPosition),
          child: SizedBox(
            height: 14,
            child: Stack(
              clipBehavior: Clip.none,
              children: [
                Positioned(
                  left: 0,
                  right: 0,
                  top: 2,
                  height: 10,
                  child: CustomPaint(painter: _BarPainter(field, paint, edge)),
                ),
                Positioned(
                  left: frac * (w - knob),
                  top: -2,
                  width: knob,
                  height: knob,
                  child: const DecoratedBox(
                    decoration: BoxDecoration(
                      color: Color(0xFFFFFFFF),
                      shape: BoxShape.circle,
                      border: Border.fromBorderSide(
                        BorderSide(color: Color(0xFF181818), width: 2),
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _BarPainter extends CustomPainter {
  _BarPainter(this.field, this.colour, this.edge);

  final int field;
  final PartPaint colour;
  final Color edge;

  Color _c(int h, int s, int v) => Color(0xFF000000 | hsvToRgb(h, s, v));

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    final rr = RRect.fromRectAndRadius(rect, Radius.circular(size.height / 2));
    canvas.save();
    canvas.clipRRect(rr);
    final p = colour;
    if (field == 3) {
      // A checkerboard under the colour fading to nearly nothing.
      final a = Paint()..color = const Color(0xFFBBBBBB);
      canvas.drawRect(rect, Paint()..color = const Color(0xFFFFFFFF));
      for (var x = 0.0; x < size.width; x += 5) {
        for (var y = 0.0; y < size.height; y += 5) {
          if (((x ~/ 5) + (y ~/ 5)) % 2 == 0) {
            canvas.drawRect(Rect.fromLTWH(x, y, 5, 5), a);
          }
        }
      }
    }
    final colours = switch (field) {
      0 => [
        for (final h in [0, 60, 120, 180, 240, 300, 359]) _c(h, 100, 100),
      ],
      1 => [_c(p.hue, 0, p.brightness), _c(p.hue, 100, p.brightness)],
      2 => [_c(p.hue, p.saturation, 0), _c(p.hue, p.saturation, 100)],
      _ => [
        _c(p.hue, p.saturation, p.brightness),
        _c(p.hue, p.saturation, p.brightness).withValues(alpha: 0.1),
      ],
    };
    canvas.drawRect(
      rect,
      Paint()..shader = LinearGradient(colors: colours).createShader(rect),
    );
    canvas.restore();
    canvas.drawRRect(
      rr.inflate(0.5),
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = edge,
    );
  }

  @override
  bool shouldRepaint(_BarPainter old) =>
      old.field != field || old.colour != colour || old.edge != edge;
}

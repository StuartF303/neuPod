// SPDX-License-Identifier: MPL-2.0

// Every Classic Light text colour against every background it is drawn on is
// at least 4.5:1 (gate 2: the owner adjusted the designer's exact values where
// they fell short). The pairs follow where the components use each colour:
//
// - list rows (`list_row.dart`): ink, secondary, number and reason on the list
//   ground (white); text on the selection bar and the flash on every stop of
//   their gradients; greyed rows use `list.disabled`, which is inactive text
//   and exempt;
// - section headers (`list_parts.dart`): `section.text` on its own ground;
// - preview pane, album header: ink and secondary on their gradients;
// - status bar and panel title: on the status gradient;
// - Now Playing: ink, ink2, ink3 (format line) on the ground;
// - Search: field text, the ring's ink, ink2, ink3 and the hint on the ground
//   or the white field, chips on their own fills;
// - fast-scroll rail and tile on the list ground.
//
// ink3 is NOT drawn on the section ground (#e9ebee) or the flash (#d6eafd),
// so it has no pair there. Icons and the caret are graphics, not text.

import 'dart:ui';

import 'package:flutter/painting.dart' show Gradient, LinearGradient;
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/skins.dart';

int _rgb(Color c) => c.toARGB32() & 0xffffff;

/// [fg] (with its alpha) over the opaque [bg].
int _over(Color fg, int bg) {
  final a = fg.a;
  final (br, bgc, bb) = channels(bg);
  return rgbOf(
    fg.r * 255 * a + br * (1 - a),
    fg.g * 255 * a + bgc * (1 - a),
    fg.b * 255 * a + bb * (1 - a),
  );
}

/// Every stop of the gradient, or the colour, of [b].
List<int> _stops(BoxSkin b) {
  final Gradient? g = b.gradient;
  if (g is LinearGradient) return [for (final c in g.colors) _rgb(c)];
  return [_rgb(b.color!)];
}

void main() {
  final s = NeupodStyles.maybeOf(StyleFamily.classic, StyleMode.light)!;
  final t = s.tokens;

  final white = _rgb(t.bg);
  final grey = _rgb(t.surface);
  final field = _stops(s.searchField.box);
  final status = _stops(s.statusBar.ground);
  final header = _stops(s.header.ground);
  final pane = _stops(s.pane);
  final titleStrip = _stops(s.panel.titleStrip!);
  const stage = [0x000000, 0x1a1c20];

  // name: (text colour, the backgrounds it is drawn on)
  final pairs = <String, (Color, List<int>)>{
    'list ink on the ground': (t.ink, [white]),
    'list secondary on the ground': (s.list.secondary, [white]),
    'row number on the ground': (s.list.number, [white]),
    'disabled reason on the ground': (s.list.reason, [white]),
    'selection text on every bar stop': (
      s.selection.text,
      _stops(s.selection.bar),
    ),
    'flash text on every flash stop': (
      s.selection.flashText,
      _stops(s.selection.flash),
    ),
    'section text on the section ground': (
      s.section.text,
      [_rgb(s.section.ground)],
    ),
    'header secondary on the header': (s.header.secondary, header),
    'ink on the header': (t.ink, header),
    'ink on the preview pane': (t.ink, pane),
    'secondary on the preview pane': (s.list.secondary, pane),
    'status-bar title on the status gradient': (s.statusBar.title, status),
    'status-bar context on the status gradient': (s.statusBar.context, status),
    'panel title on the title strip': (s.panel.title, titleStrip),
    'panel body on the panel': (s.panel.body, [_rgb(s.panel.box.color!)]),
    'OK text on every OK stop': (s.panel.okText, _stops(s.panel.ok)),
    'white on the accent': (t.onAccent, [_rgb(t.accent)]),
    'Now Playing ink on the ground': (t.ink, [white, grey]),
    'Now Playing ink2 (artist, album)': (t.ink2, [white, grey]),
    'Now Playing ink3 (format line)': (t.ink3, [white, grey]),
    'search field text': (s.searchField.text, field),
    'search field glyph': (s.searchField.glyph, field),
    'search ring ink3 and the hint': (t.ink3, [white]),
    'search ring ink2': (t.ink2, [white]),
    'chip on text on its fill': (
      s.searchField.chipOnText,
      _stops(s.searchField.chipOn),
    ),
    'chip off text on the ground': (s.searchField.chipOffText, [white]),
    'fast-scroll rail letters on the ground': (s.index.rail, [white]),
    'fast-scroll current letter on the ground': (s.index.railCurrent, [white]),
    'fast-scroll tile letter on its tile': (
      s.index.tileLetter,
      [_over(s.index.tile, white), _over(s.index.tile, 0x000000)],
    ),
    // Cover Flow's black stage (cover_flow_screen.dart: a white title and a
    // #a8a8a8 artist on a #000000 to #1a1c20 gradient).
    'Cover Flow title on its stage': (const Color(0xffffffff), stage),
    'Cover Flow artist on its stage': (const Color(0xffa8a8a8), stage),
  };

  for (final MapEntry(key: name, value: (fg, bgs)) in pairs.entries) {
    test('Classic Light contrast: $name', () {
      expect(bgs, isNotEmpty);
      for (final bg in bgs) {
        expect(
          contrastRatio(_rgb(fg), bg),
          greaterThanOrEqualTo(4.5),
          reason: '${hexOf(_rgb(fg))} on ${hexOf(bg)}',
        );
      }
    });
  }
}

// SPDX-License-Identifier: MPL-2.0

/// Classic, Light: the values of the UI styles design ("Classic, Light: the
/// values") and its mock-ups' HTML, at 600x450. `A | B` in the design is a hard
/// step at the midpoint, drawn here as stops at 0.50 and 0.51.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/skins.dart';

const _ink = Color(0xff181818);
const _ink2 = Color(0xff606060);
// Darker than the design's #767676, which is only 4.1:1 on the grey surface
// (gate 2: Classic Light is adjusted to 4.5:1; 4.6:1 on #f3f4f6).
const _ink3 = Color(0xff6e6e6e);
const _disabled = Color(0xffa0a0a0);
const _glyph = Color(0xff3a3d42);

/// The status-bar gradient, also the panel's title strip.
final _statusGradient = vertical(
  const [
    Color(0xfffdfdfd),
    Color(0xffdcdfe3),
    Color(0xffc6cad0),
    Color(0xffb6bbc2),
  ],
  const [0, 0.50, 0.51, 1],
);

/// The selection bar's gradient. The design's upper stops (#6db3f5, #2f80e0)
/// are only 2.2:1 and 4.0:1 under the white text; gate 2 adjusts them to
/// 4.65:1 and 4.96:1 (the lower stops were already 5.7:1 and 6.4:1).
final _selectionGradient = vertical(
  const [
    Color(0xff2273d8),
    Color(0xff1d6ed4),
    Color(0xff1b62cf),
    Color(0xff1858c8),
  ],
  const [0, 0.50, 0.51, 1],
);

/// Progress and volume fill: as the selection bar, starting `#8fc3f6`.
final _barFill = vertical(
  const [
    Color(0xff8fc3f6),
    Color(0xff2f80e0),
    Color(0xff1b62cf),
    Color(0xff1858c8),
  ],
  const [0, 0.50, 0.51, 1],
);

const _textShadow = [Shadow(color: Color(0x4d000000), offset: Offset(0, 1))];

NeupodStyle classicLight() {
  const tokens = NeupodTokens(
    bg: Color(0xffffffff),
    surface: Color(0xfff3f4f6),
    bar: Color(0xffdcdfe3),
    line: Color(0xffb9bdc4),
    ink: _ink,
    ink2: _ink2,
    ink3: _ink3,
    accent: Color(0xff1858c8),
    onAccent: Color(0xffffffff),
    accentSoft: Color(0xffd6eafd),
    amber: Color(0xffd08018),
    amberSoft: Color(0xfff8e8c8),
    green: Color(0xff3f9c1c),
    red: Color(0xffd03028),
  );

  return NeupodStyle(
    family: StyleFamily.classic,
    mode: StyleMode.light,
    tokens: tokens,
    drawsBackdrop: false,
    ground: tokens.bg,
    statusBar: StatusBarSkin(
      ground: BoxSkin(
        gradient: _statusGradient,
        border: const Border(bottom: BorderSide(color: Color(0xff8c9199))),
      ),
      glyph: _glyph,
      context: _glyph,
      title: _ink,
      dim: const Color(0xff9a9ea5),
      bluetoothActive: const Color(0xff1858c8),
      iconGap: 10,
      play: NeupodGlyph.play,
      pause: NeupodGlyph.pause,
      playPaint: GlyphPaint.fill,
      battery: BatterySkin(
        outline: const Color(0xff6d626a),
        radius: 4,
        inside: const Color(0xfff4f5f6),
        fill: vertical(
          const [
            Color(0xffc9f5a3),
            Color(0xff63c62f),
            Color(0xff3f9c1c),
            Color(0xff58b82a),
          ],
          const [0, 0.45, 0.55, 1],
        ),
        fillRadius: 1,
        terminal: const Size(4, 10),
        terminalGap: 0,
        low: tokens.red,
      ),
      volume: BarSkin(
        height: 10,
        radius: 5,
        track: BoxSkin(
          gradient: vertical(const [Color(0xffb9bdc3), Color(0xffe4e6e9)]),
          border: Border.all(color: const Color(0xff7f858d)),
          radius: 5,
        ),
        fill: BoxSkin(gradient: _barFill),
      ),
      volumeGlyphPaint: GlyphPaint.fillAndStroke,
    ),
    selection: SelectionSkin(
      insetX: 0,
      bar: BoxSkin(gradient: _selectionGradient),
      radiusOneLine: 0,
      radiusTwoLine: 0,
      text: const Color(0xffffffff),
      textShadows: _textShadow,
      flash: BoxSkin(
        gradient: vertical(
          const [
            Color(0xffd6eafd),
            Color(0xffa9d0f8),
            Color(0xff93c3f5),
            Color(0xff8abdf3),
          ],
          const [0, 0.50, 0.51, 1],
        ),
      ),
      flashText: const Color(0xff10233f),
      pressedTint: const Color(0x2e1858c8), // rgba(24,88,200,.18)
    ),
    list: const ListSkin(
      rowPadX: 19,
      secondary: _ink2,
      number: _ink3,
      chevron: _disabled,
      disabled: _disabled,
      reason: _ink3,
    ),
    scrollBar: ScrollBarSkin(
      width: 14,
      padding: const EdgeInsets.all(3),
      gutter: BoxSkin(
        gradient: horizontal(const [Color(0xffd5d8dc), Color(0xfff1f2f4)]),
        border: const Border(left: BorderSide(color: Color(0xffb9bdc4))),
      ),
      thumb: BoxSkin(
        gradient: horizontal(
          const [Color(0xff8fc3f6), Color(0xff2f80e0), Color(0xff1858c8)],
          const [0, 0.6, 1],
        ),
        radius: 4,
      ),
    ),
    progress: BarSkin(
      height: 12,
      radius: 6,
      track: BoxSkin(
        gradient: vertical(const [Color(0xffc9ccd1), Color(0xffeceef0)]),
        border: Border.all(color: const Color(0xff9da2a9)),
        radius: 6,
      ),
      fill: BoxSkin(gradient: _barFill),
    ),
    homeProgress: BarSkin(
      height: 8,
      radius: 4,
      track: BoxSkin(
        gradient: vertical(const [Color(0xffc9ccd1), Color(0xffeceef0)]),
        border: Border.all(color: const Color(0xff9da2a9)),
        radius: 4,
      ),
      fill: BoxSkin(
        gradient: vertical(
          const [Color(0xff7dbcf7), Color(0xff2f80e0), Color(0xff1858c8)],
          const [0, 0.5, 1],
        ),
      ),
    ),
    volume: VolumeSkin(
      bar: BarSkin(
        height: 12,
        radius: 6,
        track: BoxSkin(
          gradient: vertical(const [Color(0xffc9ccd1), Color(0xffeceef0)]),
          border: Border.all(color: const Color(0xff9da2a9)),
          radius: 6,
        ),
        fill: BoxSkin(gradient: _barFill),
      ),
      glyphSize: 26,
      gap: 12,
      glyph: _glyph,
    ),
    panel: PanelSkin(
      scrim: const Color(0x73282c34), // rgba(40,44,52,.45)
      box: BoxSkin(
        color: const Color(0xffffffff),
        border: Border.all(color: const Color(0xff6f757d)),
        radius: 10,
        shadows: const [
          BoxShadow(
            color: Color(0x66000000),
            offset: Offset(0, 8),
            blurRadius: 24,
          ),
        ],
      ),
      titleStrip: BoxSkin(
        gradient: _statusGradient,
        border: const Border(bottom: BorderSide(color: Color(0xff8c9199))),
      ),
      title: _ink,
      body: _glyph,
      padding: const EdgeInsets.fromLTRB(20, 16, 20, 18),
      gap: 0,
      okHeight: 52,
      ok: BoxSkin(gradient: _selectionGradient),
      okText: const Color(0xffffffff),
      okTextShadows: _textShadow,
    ),
    art: ArtFrameSkin(
      radius: (_) => 0,
      shadows: (use) => switch (use) {
        ArtUse.home || ArtUse.nowPlaying => const [
          BoxShadow(
            color: Color(0x66000000),
            offset: Offset(0, 2),
            blurRadius: 6,
          ),
        ],
        ArtUse.header => const [
          BoxShadow(
            color: Color(0x66000000),
            offset: Offset(0, 2),
            blurRadius: 5,
          ),
        ],
        ArtUse.thumbnail => const [
          BoxShadow(
            color: Color(0x59000000),
            offset: Offset(0, 1),
            blurRadius: 3,
          ),
        ],
        ArtUse.thumbnailSelected => const [
          BoxShadow(
            color: Color(0x73000000),
            offset: Offset(0, 1),
            blurRadius: 3,
          ),
        ],
      },
      reflection: true,
    ),
    header: HeaderSkin(
      ground: BoxSkin(
        gradient: vertical(const [Color(0xfff3f4f6), Color(0xffe3e6e9)]),
        border: const Border(bottom: BorderSide(color: Color(0xffb9bdc4))),
      ),
      secondary: _ink2,
    ),
    pane: BoxSkin(
      gradient: vertical(const [Color(0xfff3f4f6), Color(0xffdde0e4)]),
      border: const Border(left: BorderSide(color: Color(0xffb9bdc4))),
    ),
    index: const IndexSkin(
      tile: Color(0xdb181818), // rgba(24,24,24,.86)
      tileLetter: Color(0xffffffff),
      rail: _ink3, // the design's #a0a0a0 is 2.6:1; now 5.1:1 on white
      railCurrent: _ink,
    ),
    section: const SectionSkin(ground: Color(0xffe9ebee), text: _ink2),
    seek: const SeekSkin(amber: Color(0xffd08018), track: Color(0xfff8e8c8)),
    searchField: SearchFieldSkin(
      box: BoxSkin(
        color: const Color(0xffffffff),
        border: Border.all(color: const Color(0xff9da2a9)),
        radius: 8,
      ),
      text: _ink,
      glyph: _ink2,
      caret: const Color(0xff1858c8),
      ringRule: const Color(0xffd9dce1),
      chipOn: BoxSkin(
        color: const Color(0xffd8e4f8),
        border: Border.all(color: const Color(0xffd8e4f8)),
        radius: 18,
      ),
      chipOnText: const Color(0xff1858c8),
      chipOff: BoxSkin(
        border: Border.all(color: const Color(0xffc9ccd1)),
        radius: 18,
      ),
      chipOffText: _ink2,
    ),
    rating: const RatingSkin(filled: _ink2, empty: _ink3),
  );
}

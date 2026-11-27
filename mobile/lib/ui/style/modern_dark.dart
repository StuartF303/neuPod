// SPDX-License-Identifier: MPL-2.0

/// Modern, Dark: "art-led glass" (UI styles design, "Modern, Dark: the values",
/// and its mock-ups' HTML). True black, a backdrop and accent taken from the
/// album art, translucent tinted chrome.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/style/accent.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/skins.dart';

const _ink = Color(0xfff0f0f0);
const _ink2 = Color(0xffb0b0b0);
const _ink3 = Color(0xffa8a8a8);
const _disabled = Color(0xff7c7c7c);

/// The accent with no art. The design does not give one ("No art: the ground
/// alone" covers only the backdrop); this is the firmware's dark-theme accent
/// (`k_ui_theme_dark`, `#2868d8`) put through the same clamp, so white text
/// reaches 4.5:1 under the gloss. To confirm with the owner.
final Color modernAccentWithoutArt = Color(
  0xff000000 | clampAccentForWhiteText(0x2868d8),
);

/// The progress fill with no art: a light tint of [modernAccentWithoutArt].
const Color modernProgressWithoutArt = Color(0xff9dbcf0);

/// The gloss on every accent surface: white at 16 % fading out by 55 %.
final _gloss = vertical(
  const [Color(0x29ffffff), Color(0x00ffffff)],
  const [0, 0.55],
);

NeupodStyle modernDark(ArtPalette? art) {
  final accent = art == null
      ? modernAccentWithoutArt
      : Color(0xff000000 | art.accent);
  final progressFill = art == null
      ? modernProgressWithoutArt
      : Color(0xff000000 | art.progressFill);

  final tokens = NeupodTokens(
    bg: const Color(0xff050608),
    surface: const Color(0xeb1c1c20), // rgba(28,28,32,.92)
    bar: const Color(0x800a0a0c), // rgba(10,10,12,.5)
    line: const Color(0x1affffff), // rgba(255,255,255,.10)
    ink: _ink,
    ink2: _ink2,
    ink3: _ink3,
    accent: accent,
    onAccent: const Color(0xffffffff),
    accentSoft: accent.withValues(alpha: 0.28),
    amber: const Color(0xffe89828),
    amberSoft: const Color(0x47e89828), // rgba(232,152,40,.28)
    green: const Color(0xff30b870),
    red: const Color(0xffe84840),
  );

  const barShadow = BoxShadow(
    color: Color(0x73000000), // rgba(0,0,0,.45)
    offset: Offset(0, 6),
    blurRadius: 16,
  );
  final selectionBar = BoxSkin(
    color: accent,
    overlay: _gloss,
    radius: 14,
    shadows: const [barShadow],
    insetTop: const Color(0x47ffffff), // rgba(255,255,255,.28)
  );

  return NeupodStyle(
    family: StyleFamily.modern,
    mode: StyleMode.dark,
    tokens: tokens,
    drawsBackdrop: true,
    palette: art,
    ground: tokens.bg,
    statusBar: StatusBarSkin(
      ground: const BoxSkin(
        color: Color(0x800a0a0c),
        border: Border(bottom: BorderSide(color: Color(0x1affffff))),
      ),
      glyph: _ink,
      context: const Color(0xffb8b8b8),
      title: _ink,
      dim: const Color(0xff9a9a9a),
      bluetoothActive: const Color(0xff5aa2ff),
      recording: const Color(0xffff7b70),
      iconGap: 10,
      play: NeupodGlyph.playRounded,
      pause: NeupodGlyph.pauseRounded,
      playPaint: GlyphPaint.fillAndStroke,
      battery: BatterySkin(
        outline: const Color(0x99ffffff), // rgba(255,255,255,.6)
        radius: 8,
        fill: vertical(const [Color(0xff8cecb4), Color(0xff30b870)]),
        fillRadius: 4,
        terminal: const Size(3, 9),
        terminalGap: 1,
        low: tokens.red,
      ),
      volume: const BarSkin(
        height: 8,
        radius: 4,
        track: BoxSkin(color: Color(0x38ffffff), radius: 4), // .22
        fill: BoxSkin(color: _ink, radius: 4),
        fillRadius: 4,
      ),
      volumeGlyphPaint: GlyphPaint.fillAndStroke,
    ),
    selection: SelectionSkin(
      insetX: 6,
      bar: selectionBar,
      radiusOneLine: 14,
      radiusTwoLine: 16,
      text: const Color(0xffffffff),
      flash: selectionBar.copyWith(
        shadows: const [
          BoxShadow(color: Color(0x99ffffff), spreadRadius: 3), // the ring
          barShadow,
        ],
      ),
      flashText: const Color(0xffffffff),
      pressedTint: const Color(0x1fffffff), // rgba(255,255,255,.12)
    ),
    list: const ListSkin(
      rowPadX: 13,
      padX: 6,
      ground: BoxSkin(color: Color(0x6b000000)), // rgba(0,0,0,.42)
      paneGround: BoxSkin(
        color: Color(0x61000000), // rgba(0,0,0,.38)
        border: Border(right: BorderSide(color: Color(0x14ffffff))),
      ),
      divider: Color(0x14ffffff), // rgba(255,255,255,.08)
      secondary: _ink2,
      number: _ink2,
      chevron: Color(0xff8a8a8a),
      disabled: _disabled,
      reason: _ink2,
    ),
    scrollBar: const ScrollBarSkin(
      width: 10,
      padding: EdgeInsets.symmetric(vertical: 6, horizontal: 3),
      thumb: BoxSkin(color: Color(0x73ffffff), radius: 2), // .45
    ),
    progress: BarSkin(
      height: 6,
      radius: 3,
      track: const BoxSkin(color: Color(0x33ffffff), radius: 3), // .2
      fill: BoxSkin(color: progressFill, radius: 3),
      fillRadius: 3,
    ),
    homeProgress: BarSkin(
      height: 5,
      radius: 3,
      track: const BoxSkin(color: Color(0x33ffffff), radius: 3),
      fill: BoxSkin(color: progressFill, radius: 3),
      fillRadius: 3,
    ),
    volume: const VolumeSkin(
      bar: BarSkin(
        height: 8,
        radius: 4,
        track: BoxSkin(color: Color(0x33ffffff), radius: 4),
        fill: BoxSkin(color: _ink, radius: 4),
        fillRadius: 4,
      ),
      glyphSize: 24,
      gap: 14,
      glyph: _ink,
      capsule: BoxSkin(
        color: Color(0x66000000), // rgba(0,0,0,.4)
        border: Border.fromBorderSide(
          BorderSide(color: Color(0x24ffffff)),
        ), // .14
        radius: 22,
        insetTop: Color(0x1fffffff), // .12
      ),
      capsuleHeight: 44,
      capsulePadding: 14,
    ),
    panel: PanelSkin(
      scrim: const Color(0x99000000), // rgba(0,0,0,.6)
      box: const BoxSkin(
        color: Color(0xeb1c1c20),
        border: Border.fromBorderSide(
          BorderSide(color: Color(0x29ffffff)),
        ), // .16
        radius: 22,
        shadows: [
          BoxShadow(
            color: Color(0x99000000),
            offset: Offset(0, 18),
            blurRadius: 40,
          ),
        ],
        insetTop: Color(0x24ffffff), // .14
      ),
      title: _ink,
      body: const Color(0xffc8c8c8),
      padding: const EdgeInsets.fromLTRB(20, 20, 20, 16),
      gap: 12,
      okHeight: 50,
      ok: BoxSkin(
        color: accent,
        overlay: _gloss,
        radius: 14,
        insetTop: const Color(0x47ffffff),
      ),
      okText: const Color(0xffffffff),
      okMarginTop: 4,
    ),
    art: ArtFrameSkin(
      radius: (use) => switch (use) {
        ArtUse.home || ArtUse.nowPlaying => 16,
        ArtUse.header => 14,
        ArtUse.thumbnail || ArtUse.thumbnailSelected => 10,
      },
      shadows: (use) => switch (use) {
        ArtUse.home => const [
          BoxShadow(
            color: Color(0x99000000),
            offset: Offset(0, 12),
            blurRadius: 30,
          ),
        ],
        ArtUse.nowPlaying => const [
          BoxShadow(
            color: Color(0xa6000000),
            offset: Offset(0, 14),
            blurRadius: 34,
          ),
        ],
        ArtUse.header => const [
          BoxShadow(
            color: Color(0x99000000),
            offset: Offset(0, 10),
            blurRadius: 24,
          ),
        ],
        ArtUse.thumbnail => const [],
        ArtUse.thumbnailSelected => const [
          BoxShadow(
            color: Color(0x66000000),
            offset: Offset(0, 2),
            blurRadius: 6,
          ),
        ],
      },
      reflection: false,
      insetTop: const Color(0x33ffffff), // rgba(255,255,255,.2)
      placeholder: const Color(0xff2a2c31),
    ),
    header: const HeaderSkin(
      ground: BoxSkin(), // the backdrop shows through
      secondary: Color(0xffc8c8c8),
    ),
    pane: const BoxSkin(),
    index: const IndexSkin(
      tile: Color(0xebf0f0f0), // rgba(240,240,240,.92)
      tileLetter: Color(0xff101010),
      rail: Color(0xff8a8a8a),
      railCurrent: Color(0xffffffff),
    ),
    section: const SectionSkin(
      ground: Color(0x12ffffff), // rgba(255,255,255,.07)
      text: Color(0xffc4c4c4),
      extraPadX: 6,
    ),
    seek: const SeekSkin(amber: Color(0xffe89828), track: Color(0x47e89828)),
    searchField: SearchFieldSkin(
      box: const BoxSkin(
        color: Color(0x1affffff), // .1
        border: Border.fromBorderSide(
          BorderSide(color: Color(0x33ffffff)),
        ), // .2
        radius: 14,
      ),
      text: _ink,
      glyph: _ink2,
      caret: progressFill,
      ringRule: const Color(0x1affffff),
      chipOn: BoxSkin(
        color: accent,
        border: Border.all(color: const Color(0x38ffffff)), // .22
        radius: 18,
      ),
      chipOnText: const Color(0xffffffff),
      chipOff: BoxSkin(
        border: Border.all(color: const Color(0x38ffffff)),
        radius: 18,
      ),
      chipOffText: const Color(0xffc8c8c8),
    ),
    rating: RatingSkin(filled: progressFill, empty: const Color(0xff7c7c7c)),
  );
}

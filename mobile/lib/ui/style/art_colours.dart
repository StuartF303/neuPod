// SPDX-License-Identifier: MPL-2.0

/// Album art to [ArtPalette], once per track: the cover is decoded straight to
/// a 24 px wide copy (the codec downsamples, so the full image is never held),
/// its pixels go to the pure-Dart rule in `domain/style/art_palette.dart`, and
/// the result is kept in a small cache keyed by the caller's key (a track or
/// album id), so turning back to a recent track costs nothing.
library;

import 'dart:collection';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:neupod/domain/style/art_palette.dart';

/// The width of the copy the palette is read from.
const int paletteSampleWidth = 24;

/// Decodes [encoded] (JPEG, PNG ...) to a small copy and reads its palette.
/// Returns null if the image has no opaque pixel or cannot be decoded.
Future<ArtPalette?> paletteFromEncodedArt(Uint8List encoded) async {
  try {
    final codec = await ui.instantiateImageCodec(
      encoded,
      targetWidth: paletteSampleWidth,
    );
    final frame = await codec.getNextFrame();
    codec.dispose();
    final palette = await paletteFromImage(frame.image);
    frame.image.dispose();
    return palette;
  } on Exception {
    return null;
  }
}

/// Reads the palette of an already decoded (small) [image].
Future<ArtPalette?> paletteFromImage(ui.Image image) async {
  final data = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
  if (data == null) return null;
  return ArtPalette.fromRgba(data.buffer.asUint8List());
}

/// A least-recently-used cache of palettes.
final class ArtPaletteCache {
  ArtPaletteCache({this.capacity = 64});

  final int capacity;
  final LinkedHashMap<Object, Future<ArtPalette?>> _entries = LinkedHashMap();

  /// The palette for [key], computing it with [load] the first time.
  Future<ArtPalette?> get(Object key, Future<ArtPalette?> Function() load) {
    final hit = _entries.remove(key);
    final value = hit ?? load();
    _entries[key] = value;
    while (_entries.length > capacity) {
      _entries.remove(_entries.keys.first);
    }
    return value;
  }

  int get length => _entries.length;

  void clear() => _entries.clear();
}

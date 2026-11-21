// SPDX-License-Identifier: MPL-2.0

/// How many tracks the library holds.
///
/// Scaffold placeholder that proves the domain layer is wired through to the
/// UI. Domain code is pure Dart: no Flutter, no Riverpod, no other layer.
final class LibrarySummary {
  const LibrarySummary({required this.trackCount})
    : assert(trackCount >= 0, 'trackCount must not be negative');

  final int trackCount;

  bool get isEmpty => trackCount == 0;

  /// "No songs", "1 song" or "N songs".
  String get songCountLabel => switch (trackCount) {
    0 => 'No songs',
    1 => '1 song',
    _ => '$trackCount songs',
  };

  @override
  bool operator ==(Object other) =>
      other is LibrarySummary && other.trackCount == trackCount;

  @override
  int get hashCode => trackCount.hashCode;

  @override
  String toString() => 'LibrarySummary(trackCount: $trackCount)';
}

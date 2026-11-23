// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library/track.dart';

void main() {
  Track t(
    String rel, {
    String title = '',
    String artist = '',
    String album = '',
  }) => Track(
    id: 1,
    source: const TrackSource(SourceKind.localFolder, 'content://tree/x'),
    location: TrackLocation(relPath: rel, uri: 'content://doc/$rel'),
    codec: 'flac',
    title: title,
    artist: artist,
    album: album,
  );

  test('a track has a source and a location, nothing platform-specific', () {
    final track = t('Artist/2001 Album/01 Song.flac');
    expect(
      track.source,
      const TrackSource(SourceKind.localFolder, 'content://tree/x'),
    );
    expect(track.location.fileName, '01 Song.flac');
  });

  // Owner decision 15: as the device shows it, extension and all.
  test('display title falls back to the whole file name', () {
    expect(t('a/b/01 Song.flac').displayTitle, '01 Song.flac');
    expect(t('x.flac', title: 'Real').displayTitle, 'Real');
    expect(t('noext').displayTitle, 'noext');
    expect(t('.flac').displayTitle, '.flac');
  });

  test('artist and album line skips what is missing', () {
    expect(t('a', artist: 'A', album: 'B').artistAndAlbum, 'A · B');
    expect(t('a', album: 'B').artistAndAlbum, 'B');
    expect(t('a').artistAndAlbum, '');
  });

  test('reason codes are stable (they are stored in the database)', () {
    expect(
      {for (final r in UnreadableReason.values) r.name: r.code},
      {
        'notDownloaded': 1,
        'cannotOpen': 2,
        'cannotRead': 3,
        'wrongFormat': 4,
        'damaged': 5,
      },
    );
    expect(UnreadableReason.fromCode(3), UnreadableReason.cannotRead);
    expect(UnreadableReason.fromCode(null), isNull);
    expect(UnreadableReason.fromCode(99), isNull);
    expect(UnreadableReason.values.where((r) => r.transient), [
      UnreadableReason.cannotOpen,
      UnreadableReason.cannotRead,
    ]);
  });
}

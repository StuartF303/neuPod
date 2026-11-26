// SPDX-License-Identifier: MPL-2.0

// Search and the iCloud Download action (owner decision 7): the index is
// built again when a finished scan rebuilds the library, so a song that has
// been downloaded stops reading `Not downloaded`; while iOS fetches it, its
// row reads `Downloading`, as in the lists.

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/search/search_screen.dart';
import 'package:neupod/ui/screens/search/search_session.dart';

/// A library of songs only.
class _Songs implements LibraryView {
  _Songs(this._songs);

  final List<Song> _songs;

  @override
  Future<List<Song>> songs() async => _songs;

  @override
  Future<List<Artist>> artists() async => const [];

  @override
  Future<List<Album>> albums() async => const [];

  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

Song _song({required bool away}) => Song(
  id: '1',
  title: 'Away',
  artist: 'A',
  album: 'B',
  albumId: 'b',
  uri: 'file:///m/away.flac',
  unplayableReason: away ? 'Not downloaded' : null,
  downloadable: away,
);

void main() {
  test('a finished scan rebuilds the index: the downloaded song is no longer '
      'greyed', () async {
    var library = _Songs([_song(away: true)]);
    final container = ProviderContainer.test(
      overrides: [libraryViewProvider.overrideWith((ref) async => library)],
    );
    SearchHit hit() =>
        container.read(searchFunctionProvider).value!('away').songs.single;

    await container.read(searchFunctionProvider.future);
    expect(hit().reason, 'Not downloaded');
    expect(hit().downloadable, isTrue);
    expect(hit().uri, 'file:///m/away.flac');

    library = _Songs([_song(away: false)]); // the scan found it on the phone
    container.invalidate(libraryViewProvider);
    await container.read(searchFunctionProvider.future);
    expect(hit().reason, isNull);
    expect(hit().downloadable, isFalse);
  });

  test('a song asked for reads Downloading; others keep their reason', () {
    const away = SearchHit(
      kind: SearchKind.song,
      id: '1',
      title: 'Away',
      detail: 'A · B',
      reason: 'Not downloaded',
      downloadable: true,
      uri: 'file:///m/away.flac',
    );
    expect(away.reasonWith(const {}), 'Not downloaded');
    expect(away.reasonWith(const {'file:///m/away.flac'}), 'Downloading');
    expect(away.labelWith(const {'file:///m/away.flac'}), 'Downloading');
    const damaged = SearchHit(
      kind: SearchKind.song,
      id: '2',
      title: 'Bad',
      detail: 'A · B',
      reason: 'Damaged file',
      uri: 'file:///m/away.flac',
    );
    expect(damaged.reasonWith(const {'file:///m/away.flac'}), 'Damaged file');
    const album = SearchHit(kind: SearchKind.album, id: 'b', title: 'B');
    expect(album.labelWith(const {'file:///m/away.flac'}), 'Album');
  });
}

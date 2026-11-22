// SPDX-License-Identifier: MPL-2.0

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/audio/test_media.dart';

/// Serves the real files from the repository, as the app bundle would.
class _FileBundle extends CachingAssetBundle {
  @override
  Future<ByteData> load(String key) async =>
      ByteData.sublistView(File(key).readAsBytesSync());
}

void main() {
  test(
    'bundled tones are copied out and described for the lock screen',
    () async {
      final dir = Directory.systemTemp.createTempSync('neupod_test_media');
      addTearDown(() => dir.deleteSync(recursive: true));
      final media = TestMedia(bundle: _FileBundle(), directory: dir);

      final flac = await media.flacItem();
      final mp3 = await media.mp3Item();

      expect(flac.uri, startsWith('file:///'));
      expect(flac.uri, endsWith('/flac16_44k_st.flac'));
      expect(mp3.uri, endsWith('/mp3_44k_st.mp3'));
      expect(flac.artUri, endsWith('/art_64.png'));
      expect(flac.title, 'Test tone (FLAC)');
      final copied = File.fromUri(Uri.parse(flac.uri));
      expect(
        copied.readAsBytesSync(),
        File('integration_test/assets/test_media/flac16_44k_st.flac')
            .readAsBytesSync(),
      );
    },
  );
}

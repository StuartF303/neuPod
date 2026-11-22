// SPDX-License-Identifier: MPL-2.0

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:neupod/domain/playback/queue_item.dart';

/// The generated test tones bundled with the app for the player debug screen
/// and the integration tests: copies of the firmware fixtures in
/// `tests/assets/audio/` (no one's music). They are copied out of the asset
/// bundle into the temporary folder, because the engine plays URIs, not
/// assets.
class TestMedia {
  const TestMedia({this.bundle, this.directory});

  /// Defaults to the app's [rootBundle].
  final AssetBundle? bundle;

  /// Defaults to the system temporary folder.
  final Directory? directory;

  static const _folder = 'integration_test/assets/test_media';

  /// FLAC, 16-bit, 44.1 kHz, stereo, 0.3 s.
  static const flac = 'flac16_44k_st.flac';

  /// MP3, 44.1 kHz, stereo, about 1 s.
  static const mp3 = 'mp3_44k_st.mp3';

  /// The bundled art for the lock screen, a generated 64x64 gradient PNG.
  static const art = 'art_64.png';

  Future<QueueItem> flacItem() => item(flac, title: 'Test tone (FLAC)');

  Future<QueueItem> mp3Item() => item(mp3, title: 'Test tone (MP3)');

  /// A queue item for bundled file [name], with lock-screen metadata.
  Future<QueueItem> item(String name, {String? title}) async {
    final artFile = await _copy(art);
    final file = await _copy(name);
    return QueueItem(
      id: 'test:$name',
      uri: file.uri.toString(),
      title: title ?? name,
      artist: 'neuPod test tone',
      album: 'Fixtures',
      artUri: artFile.uri.toString(),
    );
  }

  Future<File> _copy(String name) async {
    final folder = directory ?? Directory.systemTemp;
    final file = File('${folder.path}/neupod_test_media/$name');
    if (file.existsSync()) return file;
    final data = await (bundle ?? rootBundle).load('$_folder/$name');
    await file.parent.create(recursive: true);
    await file.writeAsBytes(
      data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
      flush: true,
    );
    return file;
  }
}

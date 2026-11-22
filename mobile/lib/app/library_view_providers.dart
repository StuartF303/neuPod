// SPDX-License-Identifier: MPL-2.0

/// Wires the browsing screens' library and their playback to the app.
///
/// The gate's library is the generated test library's manifest
/// (`mobile/tool/gen_test_library`). Where it comes from:
///
/// - `--dart-define=NEUPOD_GATE_LIBRARY=<dir>`, the folder the generator
///   wrote (it holds `manifest.json` and `library/`), when the app can read it
///   (the iOS simulator and the Android emulator on the build machine can, a
///   phone cannot): real names and the folder art, and playing a song plays
///   the generated file.
/// - Otherwise the copy of the manifest in `assets/gate_library/`: the same
///   names, placeholder covers, and playing a song plays a bundled test tone
///   with the song's title, artist and album on the lock screen.
///
/// When the drift-backed adapter (PR #117) merges, `libraryViewProvider`
/// returns it instead and nothing else changes.
library;

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/audio/test_media.dart';
import 'package:neupod/data/gate_library/manifest_library_view.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/playback/queue_item.dart';

const _gateLibraryDir = String.fromEnvironment('NEUPOD_GATE_LIBRARY');

/// The asset copy of the test library's manifest.
const gateManifestAsset = 'assets/gate_library/manifest.json';

/// The library the browsing screens show.
final libraryViewProvider = FutureProvider<LibraryView>((ref) async {
  if (_gateLibraryDir.isNotEmpty) {
    final file = File('$_gateLibraryDir/manifest.json');
    if (file.existsSync()) {
      return ManifestLibraryView.parse(
        await file.readAsString(),
        libraryDir: _gateLibraryDir,
      );
    }
  }
  return ManifestLibraryView.parse(
    await rootBundle.loadString(gateManifestAsset),
  );
});

/// Turns songs into the player's queue items.
typedef QueueMapper = Future<List<QueueItem>> Function(List<Song> songs);

/// Maps songs to queue items. A song with a real file opens it; a gate-sample
/// song opens a bundled test tone (FLAC for `.flac`, MP3 otherwise) under its
/// own title, artist and album.
final queueMapperProvider = Provider<QueueMapper>((ref) {
  final media = ref.watch(testMediaProvider);
  final tones = <String, QueueItem>{};
  Future<QueueItem> tone(String name) async =>
      tones[name] ??= await media.item(name);
  return (songs) async {
    final out = <QueueItem>[];
    for (final s in songs) {
      if (!s.playable) continue;
      if (s.uri.startsWith('gate-sample:')) {
        final base = await tone(
          s.uri.toLowerCase().endsWith('.flac')
              ? TestMedia.flac
              : TestMedia.mp3,
        );
        out.add(
          QueueItem(
            id: s.id,
            uri: base.uri,
            title: s.title,
            artist: s.artist,
            album: s.album,
            artUri: base.artUri,
            duration: s.duration,
          ),
        );
      } else {
        out.add(
          QueueItem(
            id: s.id,
            uri: Uri.file(s.uri).toString(),
            title: s.title,
            artist: s.artist,
            album: s.album,
            duration: s.duration,
          ),
        );
      }
    }
    return out;
  };
});

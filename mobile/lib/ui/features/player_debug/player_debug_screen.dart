// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod/ui/features/player_debug/player_debug_providers.dart';

/// A plain screen for checking the audio engine by hand: play the bundled
/// test tones or any URI, drive the transport, and read back the player and
/// media-session state. Not part of the designed app; reached from Home in
/// debug builds.
class PlayerDebugScreen extends ConsumerStatefulWidget {
  const PlayerDebugScreen({super.key});

  static const routeName = '/debug/player';

  @override
  ConsumerState<PlayerDebugScreen> createState() => _PlayerDebugScreenState();
}

class _PlayerDebugScreenState extends ConsumerState<PlayerDebugScreen> {
  final _uris = TextEditingController();
  String? _message;

  @override
  void dispose() {
    _uris.dispose();
    super.dispose();
  }

  Future<void> _run(Future<void> Function() action) async {
    try {
      await action();
      if (mounted) setState(() => _message = null);
    } on Object catch (e) {
      if (mounted) setState(() => _message = '$e');
    }
  }

  Future<void> _playQueue(Future<List<QueueItem>> Function() items) =>
      _run(() async {
        final player = ref.read(playerProvider);
        await player.setQueue(await items());
        await player.play();
      });

  Future<void> _playTones({required bool flac, required bool mp3}) {
    final media = ref.read(testMediaProvider);
    return _playQueue(
      () async => [
        if (flac) await media.flacItem(),
        if (mp3) await media.mp3Item(),
      ],
    );
  }

  /// Plays the URIs in the text field, one per line or comma-separated.
  Future<void> _playUris() => _playQueue(() async {
    final uris = _uris.text
        .split(RegExp(r'[\s,]+'))
        .where((u) => u.isNotEmpty)
        .toList();
    return [
      for (final (i, uri) in uris.indexed)
        QueueItem(
          id: 'uri:$i',
          uri: uri,
          title: Uri.parse(uri).pathSegments.lastOrNull ?? uri,
        ),
    ];
  });

  @override
  Widget build(BuildContext context) {
    final state = ref.watch(playerStateProvider).value ?? PlayerState.empty;
    final position = ref.watch(debugPositionProvider).value ?? state.position;
    final session = ref.watch(debugSessionProvider);
    final player = ref.read(playerProvider);
    final duration = state.duration ?? Duration.zero;
    final max = duration.inMilliseconds.toDouble();

    return Scaffold(
      appBar: AppBar(title: const Text('Player (debug)')),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                ElevatedButton(
                  onPressed: () => _playTones(flac: true, mp3: false),
                  child: const Text('Test FLAC'),
                ),
                ElevatedButton(
                  onPressed: () => _playTones(flac: false, mp3: true),
                  child: const Text('Test MP3'),
                ),
                ElevatedButton(
                  onPressed: () => _playTones(flac: true, mp3: true),
                  child: const Text('FLAC then MP3'),
                ),
              ],
            ),
            const SizedBox(height: 12),
            TextField(
              key: const Key('uris'),
              controller: _uris,
              decoration: const InputDecoration(
                labelText: 'URIs (content:// or file://), one per line',
              ),
              minLines: 1,
              maxLines: 4,
            ),
            Align(
              alignment: Alignment.centerLeft,
              child: TextButton(
                onPressed: _playUris,
                child: const Text('Play URIs'),
              ),
            ),
            const Divider(),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                TextButton(
                  onPressed: () => _run(player.skipToPrevious),
                  child: const Text('Previous'),
                ),
                TextButton(
                  onPressed: () =>
                      _run(state.playWhenReady ? player.pause : player.play),
                  child: Text(state.playWhenReady ? 'Pause' : 'Play'),
                ),
                TextButton(
                  onPressed: () => _run(player.skipToNext),
                  child: const Text('Next'),
                ),
              ],
            ),
            Slider(
              value: position.inMilliseconds.clamp(0, max).toDouble(),
              max: max > 0 ? max : 1,
              onChanged: max > 0
                  ? (v) => _run(
                      () => player.seek(Duration(milliseconds: v.round())),
                    )
                  : null,
            ),
            Text('${_time(position)} / ${_time(duration)}'),
            const Divider(),
            _Field('Status', state.status.name),
            _Field('Play when ready', '${state.playWhenReady}'),
            _Field(
              'Item',
              state.index == null
                  ? '-'
                  : '${state.index! + 1} of ${state.queue.length}: '
                        '${state.current?.title ?? state.current?.uri}',
            ),
            _Field('Format', state.format?.toString() ?? '-'),
            _Field('Error', state.error ?? _message ?? '-'),
            const Divider(),
            Row(
              children: [
                const Expanded(child: Text('Media session')),
                TextButton(
                  onPressed: () => ref.invalidate(debugSessionProvider),
                  child: const Text('Refresh'),
                ),
              ],
            ),
            Text(
              session.when(
                data: (s) => '$s',
                error: (e, _) => '$e',
                loading: () => '...',
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _Field extends StatelessWidget {
  const _Field(this.label, this.value);

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 2),
    child: Text('$label: $value'),
  );
}

String _time(Duration d) {
  final seconds = d.inMilliseconds / 1000;
  final m = seconds ~/ 60;
  final s = (seconds % 60).toStringAsFixed(1).padLeft(4, '0');
  return '$m:$s';
}

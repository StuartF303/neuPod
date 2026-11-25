// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/playback/player_state.dart';

/// The extrapolated position, ticking while the debug screen is open.
final debugPositionProvider = StreamProvider.autoDispose<Duration>(
  (ref) => ref.watch(playerProvider).positions(),
);

/// What the media session shows, read on demand by the debug screen.
final debugSessionProvider = FutureProvider.autoDispose<MediaSessionSnapshot>(
  (ref) => ref.watch(playerProvider).sessionSnapshot(),
);

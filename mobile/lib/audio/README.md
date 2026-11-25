# audio

Implements the domain's `Player` port (`domain/playback/player.dart`) on the in-repo engine plugin
`packages/neupod_audio` (decisions [0001](../../../docs/app/decisions/0001-audio-engine.md) and
[0002](../../../docs/app/decisions/0002-media-session.md)). May import `domain/` and Flutter plugins; never `ui/`, `app/`
or Riverpod.

- `neupod_player.dart`: `NeupodPlayer`, the `Player`. It maps engine events to `PlayerState`, extrapolates the position
  between events, ignores events about an earlier queue, and skips an unplayable entry so the queue never stops.
- `test_media.dart`: the bundled test tones (copies of `tests/assets/audio` fixtures) for the player debug screen and
  the integration tests.

The engine owns playback state on the native side (on Android it lives in a `MediaSessionService` and outlives the
Flutter engine), so this layer holds no queue of its own beyond the mirror in `PlayerState`. Shuffle, repeat and resume
are domain rules (phase 4) applied to the queue before it is set.

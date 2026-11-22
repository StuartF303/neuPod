// SPDX-License-Identifier: MPL-2.0

/// At most [perSecond] events a second: an event is allowed when at least
/// 1/[perSecond] of a second has passed since the last allowed one, and the
/// rest are dropped (design section 5: detent haptics and click sounds are
/// limited to 30 a second).
class RateLimiter {
  /// A limiter for [perSecond] events a second.
  RateLimiter({this.perSecond = 30});

  /// The most events allowed in a second.
  final int perSecond;

  int? _lastUs;

  /// Whether an event at [nowUs] (microseconds, monotonic) may fire; records
  /// it when it may.
  bool allow(int nowUs) {
    final last = _lastUs;
    if (last != null && nowUs >= last && (nowUs - last) * perSecond < 1000000) {
      return false;
    }
    _lastUs = nowUs;
    return true;
  }

  /// Forgets the last event.
  void reset() => _lastUs = null;
}

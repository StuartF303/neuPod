// SPDX-License-Identifier: MPL-2.0

/// At most [perSecond] events a second: an event is allowed when at least
/// 1/[perSecond] of a second has passed since the last allowed one, and the
/// rest are dropped (design section 5: detent haptics and click sounds are
/// limited to 30 a second). Times are milliseconds from a monotonic clock,
/// like the wheel's.
class RateLimiter {
  /// A limiter for [perSecond] events a second.
  RateLimiter({this.perSecond = 30});

  /// The most events allowed in a second.
  final int perSecond;

  int? _lastMs;

  /// Whether an event at [nowMs] may fire; records it when it may.
  bool allow(int nowMs) {
    final last = _lastMs;
    if (last != null && nowMs >= last && (nowMs - last) * perSecond < 1000) {
      return false;
    }
    _lastMs = nowMs;
    return true;
  }

  /// Forgets the last event.
  void reset() => _lastMs = null;
}

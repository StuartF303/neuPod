// SPDX-License-Identifier: MPL-2.0

/// The sort rule for names (owner decision 11): one leading article (`The `,
/// `A `, `An `) is skipped when something follows it, letters compare without
/// case and with accents folded, and the order is symbols, then digits, then
/// letters. The fast-scroll letter is the first letter of the key, `#` for a
/// digit or symbol.
///
/// INTERIM. The firmware's `lib_collate_key()` is ported byte for byte in
/// `domain/library/collation.dart` by PR #117; when that is on main, the gate
/// adapter should call it instead and this file goes. Until then the gate
/// library is sorted by this lighter version of the same rule, which is
/// enough for the manifest's cases (articles, accents, symbols, digits).
library;

const _accents = {
  'à': 'a', 'á': 'a', 'â': 'a', 'ã': 'a', 'ä': 'a', 'å': 'a', 'ç': 'c', //
  'è': 'e', 'é': 'e', 'ê': 'e', 'ë': 'e', 'ì': 'i', 'í': 'i', 'î': 'i',
  'ï': 'i', 'ð': 'd', 'ñ': 'n', 'ò': 'o', 'ó': 'o', 'ô': 'o', 'õ': 'o',
  'ö': 'o', 'ø': 'o', 'ù': 'u', 'ú': 'u', 'û': 'u', 'ü': 'u', 'ý': 'y',
  'ÿ': 'y', 'æ': 'ae', 'þ': 'th', 'ß': 'ss', 'œ': 'oe', 'ō': 'o', 'ē': 'e',
};

String _skipArticle(String s) {
  final lower = s.toLowerCase();
  for (final article in const ['the ', 'an ', 'a ']) {
    if (lower.startsWith(article) && s.length > article.length) {
      return s.substring(article.length);
    }
  }
  return s;
}

/// A key whose string comparison is the display order of [name], or of
/// [sort] when it is not empty.
String sortKey(String name, {String? sort}) {
  final src = _skipArticle(sort != null && sort.isNotEmpty ? sort : name);
  final out = StringBuffer();
  for (final rune in src.toLowerCase().runes) {
    final ch = String.fromCharCode(rune);
    final folded = _accents[ch] ?? ch;
    for (final c in folded.codeUnits) {
      if (c >= 0x61 && c <= 0x7a) {
        out.writeCharCode(0xC0 + (c - 0x61)); // letters last
      } else if (c >= 0x30 && c <= 0x39) {
        out.writeCharCode(0x80 + (c - 0x30)); // then digits
      } else if (c >= 0x20 && c < 0x7f) {
        out.writeCharCode(c); // symbols and space first, as themselves
      } else {
        out.writeCharCode(0x7f); // anything else, a symbol
      }
    }
  }
  return out.toString();
}

/// Compares two names by [sortKey].
int compareNames(String a, String b) => sortKey(a).compareTo(sortKey(b));

/// The fast-scroll letter of [name]: `A` to `Z`, or `#`.
String sortLetterOf(String name, {String? sort}) {
  final key = sortKey(name, sort: sort);
  if (key.isEmpty) return '#';
  final c = key.codeUnitAt(0);
  return c >= 0xC0 && c <= 0xC0 + 25
      ? String.fromCharCode(0x41 + c - 0xC0)
      : '#';
}

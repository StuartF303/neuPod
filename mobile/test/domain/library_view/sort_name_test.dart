// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library_view/sort_name.dart';

void main() {
  List<String> sorted(List<String> names) => [...names]..sort(compareNames);

  test('symbols, then digits, then letters', () {
    expect(sorted(['Zed', '99 Problems', '!Aktion', 'abba', '+44']), [
      '!Aktion',
      '+44',
      '99 Problems',
      'abba',
      'Zed',
    ]);
  });

  test('one leading article is skipped when something follows it', () {
    expect(sorted(['The Beatles', 'A Perfect Circle', 'An Orchard', 'Cave']), [
      'The Beatles',
      'Cave',
      'An Orchard',
      'A Perfect Circle',
    ]);
    // The article alone is a name.
    expect(sortLetterOf('The'), 'T');
    expect(sortLetterOf('The Beatles'), 'B');
    expect(sortLetterOf('A Perfect Circle'), 'P');
  });

  test('case and accents are folded', () {
    expect(compareNames('émilie', 'Emily') < 0, isTrue);
    expect(sortLetterOf('Émilie Côté'), 'E');
    expect(sortLetterOf('Þögn'), 'T');
  });

  test('the letter of a digit or symbol is #', () {
    expect(sortLetterOf('7 Seconds'), '#');
    expect(sortLetterOf('!Aktion'), '#');
    expect(sortLetterOf(''), '#');
  });

  test('a sort tag wins over the name', () {
    expect(sortLetterOf('Beatles, The', sort: 'Beatles'), 'B');
    expect(sortKey('x', sort: 'abc'), sortKey('abc'));
  });
}

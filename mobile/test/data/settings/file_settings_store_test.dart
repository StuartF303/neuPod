// SPDX-License-Identifier: MPL-2.0

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/settings/file_settings_store.dart';

void main() {
  late Directory root;
  late String dir;
  late FileSettingsStore store;

  setUp(() {
    root = Directory.systemTemp.createTempSync('neupod_settings_test_');
    dir = '${root.path}/settings'; // not created: the store makes it
    store = FileSettingsStore(() async => dir);
  });
  tearDown(() => root.deleteSync(recursive: true));

  test('a missing file reads as empty', () async {
    expect(await store.read(), isEmpty);
  });

  test(
    'put writes the file, creating the folder, and read returns it',
    () async {
      await store.put('mode', 'dark');
      await store.put('custom', {
        'looks': [1, 2],
      });
      expect(File('$dir/settings.json').existsSync(), isTrue);
      expect(await store.read(), {
        'mode': 'dark',
        'custom': {
          'looks': [1, 2],
        },
      });
      // A second store on the same folder sees the same.
      expect(
        await FileSettingsStore(() async => dir).read(),
        await store.read(),
      );
    },
  );

  test('put keeps the other keys and replaces its own', () async {
    await store.put('a', 1);
    await store.put('b', 2);
    await store.put('a', 3);
    expect(await store.read(), {'a': 3, 'b': 2});
  });

  test(
    'writes are atomic: no temp file is left, the file is whole JSON',
    () async {
      for (var i = 0; i < 20; i++) {
        unawaited(store.put('n', i));
      }
      await store.put('n', 99);
      expect(Directory(dir).listSync().map((e) => e.uri.pathSegments.last), [
        'settings.json',
      ]);
      expect(jsonDecode(File('$dir/settings.json').readAsStringSync()), {
        'n': 99,
      });
    },
  );

  test(
    'a corrupt file reads as empty and the next write replaces it',
    () async {
      Directory(dir).createSync(recursive: true);
      File('$dir/settings.json').writeAsStringSync('{"mode": "da');
      expect(await store.read(), isEmpty);
      await store.put('mode', 'light');
      expect(await store.read(), {'mode': 'light'});
    },
  );

  test('JSON that is not an object reads as empty', () async {
    Directory(dir).createSync(recursive: true);
    File('$dir/settings.json').writeAsStringSync('[1, 2, 3]');
    expect(await store.read(), isEmpty);
  });

  test('a folder that cannot be made never throws', () async {
    final blocked = File('${root.path}/file')..writeAsStringSync('x');
    final bad = FileSettingsStore(() async => '${blocked.path}/settings');
    expect(await bad.read(), isEmpty);
    await bad.put('a', 1); // dropped, no exception
    expect(await bad.read(), isEmpty);
  });

  test('a directory that cannot be found never throws', () async {
    final bad = FileSettingsStore(() async => throw StateError('no plugin'));
    expect(await bad.read(), isEmpty);
    await bad.put('a', 1);
  });
}

// SPDX-License-Identifier: MPL-2.0

// Android backups (owner decision 20): filesDir/stats (the ratings and play
// counts database) and filesDir/settings (the settings file) are the only paths
// backed up, by both rule sets (Auto Backup on
// Android 11 and earlier, data extraction rules on 12 and later). The
// library database is in noBackupFilesDir, never backed up. The on-device
// half is integration_test/backup_paths_test.dart.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

const _res = 'android/app/src/main/res/xml';

/// Every `<include>` / `<exclude>` rule in [xml], as `kind domain path`.
List<String> _rules(String xml) => [
  for (final m in RegExp(
    r'<(include|exclude)\s+domain="([^"]+)"\s+path="([^"]+)"\s*/>',
  ).allMatches(xml.replaceAll(RegExp(r'<!--.*?-->', dotAll: true), '')))
    '${m[1]} ${m[2]} ${m[3]}',
];

void main() {
  test('the manifest names both rule files and allows backup', () {
    final manifest = File('android/app/src/main/AndroidManifest.xml')
        .readAsStringSync();
    expect(manifest, contains('android:allowBackup="true"'));
    expect(manifest, contains('android:fullBackupContent="@xml/backup_rules"'));
    expect(
      manifest,
      contains('android:dataExtractionRules="@xml/data_extraction_rules"'),
    );
  });

  test('Android 11 and earlier: filesDir/stats and filesDir/settings', () {
    final xml = File('$_res/backup_rules.xml').readAsStringSync();
    expect(xml, contains('<full-backup-content>'));
    expect(_rules(xml), ['include file stats/', 'include file settings/']);
  });

  test(
    'Android 12 and later: filesDir/stats and settings, for cloud backup and '
    'device transfer',
    () {
      final xml = File('$_res/data_extraction_rules.xml').readAsStringSync();
      String section(String name) =>
          RegExp('<$name>(.*?)</$name>', dotAll: true).firstMatch(xml)![1]!;
      expect(_rules(section('cloud-backup')), [
        'include file stats/',
        'include file settings/',
      ]);
      expect(_rules(section('device-transfer')), [
        'include file stats/',
        'include file settings/',
      ]);
    },
  );

  test('the plugin puts the stats database where the rules include it, and '
      'the library where Android never backs up', () {
    final kotlin = File(
      'packages/neupod_storage/android/src/main/kotlin/net/stuartfraser/'
      'neupod_storage/NeupodStoragePlugin.kt',
    ).readAsStringSync();
    expect(kotlin, contains('java.io.File(files, "stats")'));
    expect(kotlin, contains('context?.filesDir'));
    expect(kotlin, contains('context?.noBackupFilesDir?.absolutePath'));
  });
}

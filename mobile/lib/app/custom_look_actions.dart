// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';

/// The settings-file key that holds the highest look id number ever made, so
/// an id is never reused after a look is deleted.
const String lookSequenceKey = 'customLookSeq';

/// The highest look id number ever made (`look-7` is 7). The starters are 1
/// to 3.
class LookSequenceNotifier extends Notifier<int> {
  /// Completes when the stored number has been read.
  Future<void> loaded = Future<void>.value();

  @override
  int build() {
    loaded = _load();
    return 0;
  }

  Future<void> _load() async {
    try {
      final stored = await ref.read(settingsStoreProvider).read();
      final v = stored[lookSequenceKey];
      // Never lower: a number taken before this finished stays taken.
      if (v is int && ref.mounted && v > state) state = v;
    } on Object catch (e) {
      debugPrint('neupod: look sequence not loaded: $e');
    }
  }

  /// Takes the next number, above every one made and every id in [looks].
  /// Waits for the stored number first, so it is right however early it is
  /// asked.
  Future<int> take(Iterable<CustomLook> looks) async {
    await loaded;
    final n = nextLookIdNumber(state, looks);
    state = n;
    unawaited(ref.read(settingsStoreProvider).put(lookSequenceKey, n));
    return n;
  }
}

/// The highest look id number ever made.
final lookSequenceProvider = NotifierProvider<LookSequenceNotifier, int>(
  LookSequenceNotifier.new,
);

/// A look the device shows for a moment while a list rests on it (the looks
/// list and the board picker preview after 250 ms), instead of the look in
/// use. Null: none.
class LookPreviewNotifier extends Notifier<CustomLook?> {
  @override
  CustomLook? build() => null;

  /// Shows [look], or none.
  void show(CustomLook? look) {
    if (state != look) state = look;
  }
}

/// The preview, if any.
final lookPreviewProvider = NotifierProvider<LookPreviewNotifier, CustomLook?>(
  LookPreviewNotifier.new,
);

/// What the person does to the looks. Each change is saved as it is made.
class CustomLookActions {
  const CustomLookActions(this.ref);

  final Ref ref;

  CustomLooksNotifier get _notifier => ref.read(customLooksProvider.notifier);
  CustomLooks get looks => ref.read(customLooksProvider);

  /// Makes the look with [id] the look in use and Mode Custom (design section
  /// 1: opening a look from the list is the one way in).
  void open(String id) {
    _notifier.update((l) => l.using(id));
    final choice = ref.read(styleChoiceProvider);
    if (choice.mode != ModeSetting.custom) {
      ref
          .read(styleChoiceProvider.notifier)
          .set(choice.copyWith(mode: ModeSetting.custom));
    }
  }

  /// A new look: a copy of the look in use (the default finish when there is
  /// none), named `Look n`, in use, Mode Custom. Null when six are saved.
  Future<CustomLook?> newLook({required bool phoneIsLight}) async {
    final now = looks;
    if (now.full) return null;
    final from = now.inUse;
    final base =
        from ??
        () {
          final d = defaultPaints(LookScreen.auto, phoneIsLight: phoneIsLight);
          return CustomLook(
            id: '',
            name: '',
            body: d.body,
            wheel: d.wheel,
            centre: d.centre,
          );
        }();
    final n = await ref.read(lookSequenceProvider.notifier).take(now.looks);
    if (looks.full) return null; // another New look got in while waiting
    final made = base.copyWith(id: 'look-$n', name: nextLookName(looks.looks));
    _notifier.update((l) => l.added(made));
    open(made.id);
    return made;
  }

  /// Changes the look with [id] and saves it.
  void edit(String id, CustomLook Function(CustomLook) change) {
    final look = looks.byId(id);
    if (look == null) return;
    _notifier.update((l) => l.replaced(change(look)));
  }

  /// Renames the look with [id]; an empty or over-long name is refused.
  bool renameLook(String id, String name) {
    final n = name.trim();
    if (n.isEmpty || n.length > maxLookNameLength) return false;
    edit(id, (l) => l.copyWith(name: n));
    return true;
  }

  /// The look's three parts back to the default finish for its screen.
  void reset(String id, {required bool phoneIsLight}) =>
      edit(id, (l) => resetLook(l, phoneIsLight: phoneIsLight));

  /// Deletes the look. When none is left Mode falls back to Auto.
  void removeLook(String id) {
    _notifier.update((l) => l.deleted(id));
    if (looks.looks.isEmpty) {
      final choice = ref.read(styleChoiceProvider);
      if (choice.mode == ModeSetting.custom) {
        ref
            .read(styleChoiceProvider.notifier)
            .set(choice.copyWith(mode: ModeSetting.auto));
      }
    }
  }
}

/// The actions, for a widget's `ref.read`.
final customLookActionsProvider = Provider<CustomLookActions>(
  CustomLookActions.new,
);

# 0008: Library database migrations

Status: Accepted (2026-11-22) · Owner: Library engineer

## Context

- Phase 1 shipped schema version 1 (`sources`, `tracks`) with an `onUpgrade` that dropped and recreated every table.
  That was fine for a cache a rescan rebuilds, but phase 3 and 6 add data the user cannot rebuild: ratings, play
  counts, last played, skip counts, playlists (decision 0005).
- Version 1 was never released: no store or TestFlight build, only debug builds on the owner's and the CI's devices.
- drift 2.35.4 / drift_dev 2.35.3 (pinned in `mobile/pubspec.lock`). The current drift docs
  (drift.simonbinder.eu/migrations, checked 2026-11-22) recommend `dart run drift_dev make-migrations`, configured in
  `build.yaml`: it writes a schema snapshot per version, a `<database>.steps.dart` file with a typed `stepByStep`
  helper, and a migration test on the generated `SchemaVerifier`.

## Options considered

1. **Reset the baseline**: delete the v1 snapshot and start the migration history at the first schema with
   user-owned tables. Saves nothing (the v1 snapshot already exists and is exactly the schema in the code) and leaves
   every debug install of v1 with no path forward except a drop.
2. **Keep v1 as the baseline** and migrate stepwise from it. A database at v1 upgrades like any other.
3. **Keep drop-and-recreate until the first user-owned table.** Rejected: the step that adds that table is exactly
   when a drop would lose data, and the drop code would have to be removed in the same PR that first needs it gone.

## Decision

**Option 2.** Version 1 stays the baseline, unchanged, and every later version is a drift step.

- `mobile/build.yaml` names the database (`library: lib/data/db/library_database.dart`), the snapshot folder
  (`drift_schemas/`) and the test folder (`test/drift/`).
- `onUpgrade` calls `stepByStep()` from the generated `lib/data/db/library_database.steps.dart` (through a getter,
  as drift advises), then `PRAGMA foreign_key_check`, which throws if a key is broken. With one version there are no
  steps yet, so any upgrade path throws; `make-migrations` regenerates the file with a `from1To2` parameter when
  version 2 exists.
- **One transaction; a failure rolls back; `user_version` is unchanged.** drift 2.35.4 does not wrap `onUpgrade` in a
  transaction, and writes `user_version` only after `beforeOpen` succeeds. So `PRAGMA foreign_keys = OFF` runs first
  (the pragma does nothing inside a transaction), then all the steps and the foreign key check run in one
  `transaction`, as in drift's documented pattern (`VersionedSchema.runMigrationSteps`). If any step or the check
  throws, the whole upgrade rolls back, the database keeps its old schema, rows and `user_version`, and the next
  launch starts again from there. `beforeOpen` turns foreign keys back on.
  `make-migrations` itself writes nothing but the snapshot while there is only one version, so the steps file and the
  test helpers for v1 were generated with `drift_dev schema steps` and `drift_dev schema generate --data-classes
  --companions`, which `make-migrations` overwrites from then on.
- **Downgrades are refused.** A database from a newer build (a newer `user_version`) makes drift's step runner throw a
  `StateError`, and nothing is changed or dropped. Stores do not downgrade apps; a developer who installs an older
  debug build over a newer one clears the app's data.
- **Tests** (`mobile/test/drift/library/migration_test.dart`, written once by hand; `make-migrations` never
  overwrites an existing test file):
  - the current `schemaVersion` has a snapshot and it is the newest, and a fresh database matches it (a table changed
    without a new version fails);
  - every earlier version migrates to every later one and matches its snapshot (drift's generated pattern);
  - every earlier version migrates to the current one **with every row kept**: one row is written in each table of the
    old version (parents first, by foreign key, with valid references), and after the migration each table still has
    it, with the same value in each column still present. A step that drops a column on purpose lists it in
    `_droppedOnPurpose`;
  - a database from a newer version is refused and keeps its rows and its `user_version`;
  - a failed upgrade (a step that throws part-way, or one that breaks a foreign key), injected through
    `LibraryDatabase.withStepsForTesting`, leaves the schema, the rows and `user_version` exactly as they were.
- **Guard** (`mobile/test/data/db/library_tables_guard_test.dart`): fails if any table other than `sources` and
  `tracks` exists. It is updated in the same PR that adds the first user-owned table, and that PR must bring its
  migration step.

## Adding version 2

1. Change the tables in `library_database.dart` and bump `schemaVersion` to 2.
2. `dart run drift_dev make-migrations` (from `mobile/`): writes `drift_schemas/library/drift_schema_v2.json`,
   regenerates `library_database.steps.dart` and `test/drift/library/generated/`.
3. Add `from1To2: (m, schema) async { ... }` to `stepByStep(...)` in `_upgrade`, using `schema` (the v2 snapshot), not
   the live tables.
4. `dart run build_runner build`; run `flutter test test/drift test/data/db`. If the step changes existing columns
   (not just adds tables or columns), add a data test with the generated `v1`/`v2` classes as well.

## Consequences

- Every schema change costs a snapshot, a step and a passing migration test; the data test runs for each old version
  with no extra code.
- The database stays excluded from backups for now (decision 0005); that is revisited with the first user-owned table.

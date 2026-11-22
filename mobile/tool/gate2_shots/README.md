# Gate 2 shots

Tools that produce the screenshots for the owner's design gate 2. Nothing here
is part of the app or its test suite.

## Exact renders (flutter_test)

Every gate screen in the real device view at 393x852, in Classic Light and
Modern Dark, at twice the point size. From `mobile/`:

    flutter test tool/gate2_shots/gate2_renders_test.dart \
      --dart-define=GATE2_OUT=<output dir>

PNGs go to `<output dir>` (default `build/gate2_renders`); nothing is compared.

## Simulator shots (iOS)

Real simulator screenshots of Home and Now Playing for each variant (fits,
body finishes, status bar, wordmark). From `mobile/`:

    flutter build ios --simulator --debug --no-codesign \
      -t tool/gate2_shots/gate2_sim_main.dart
    tool/gate2_shots/capture_sim.sh <output dir> "iPhone 16e" "iPhone 16"

`capture_sim.sh` writes the scene to `/tmp/neupod-gate2-scene.env` (removed on
exit) and shuts each simulator down when done. On a shared machine run both
commands through `~/bin/neupod-buildlock`.

## Index page

    python3 tool/gate2_shots/make_index.py <output dir>

Builds a standalone `index.html` over the `sim/` and `renders/` PNGs there.

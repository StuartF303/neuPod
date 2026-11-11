#!/usr/bin/env bash
# Configure and build one platform into build/<platform>/.
# Usage: scripts/build.sh [rp2350|esp32s3|host|y2] [--clean] [-- extra cmake/idf.py args]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${1:-rp2350}"; shift || true
CLEAN=0
if [[ "${1:-}" == "--clean" ]]; then CLEAN=1; shift; fi
if [[ "${1:-}" == "--" ]]; then shift; fi
BUILD="$ROOT/build/$PLATFORM"

(( CLEAN )) && rm -rf "$BUILD"

case "$PLATFORM" in
    rp2350|host|y2)
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DNEUPOD_PLATFORM="$PLATFORM" "$@"
        ninja -C "$BUILD"
        ;;
    esp32s3)
        command -v idf.py >/dev/null || {
            echo "idf.py not found. Install with scripts/setup-esp-idf.sh, then: . ~/esp/esp-idf/export.sh" >&2
            exit 1
        }
        idf.py -C "$ROOT/platforms/esp32s3" -B "$BUILD" set-target esp32s3 >/dev/null
        idf.py -C "$ROOT/platforms/esp32s3" -B "$BUILD" build "$@"
        ;;
    *)
        echo "Unknown platform '$PLATFORM' (rp2350, esp32s3, host, y2)" >&2
        exit 2
        ;;
esac

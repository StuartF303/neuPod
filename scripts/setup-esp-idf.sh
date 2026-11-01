#!/usr/bin/env bash
# Install ESP-IDF (default v5.3.2) to ~/esp/esp-idf with ESP32-S3 tools.
# Usage: scripts/setup-esp-idf.sh [git-ref]
# Afterwards, in each shell:  . ~/esp/esp-idf/export.sh
set -euo pipefail
REF="${1:-v5.3.2}"
IDF_DIR="${IDF_DIR:-$HOME/esp/esp-idf}"

if [[ -d "$IDF_DIR/.git" ]]; then
    echo "ESP-IDF already present at $IDF_DIR; checking out $REF"
    git -C "$IDF_DIR" fetch --tags
    git -C "$IDF_DIR" checkout "$REF"
    git -C "$IDF_DIR" submodule update --init --recursive
else
    mkdir -p "$(dirname "$IDF_DIR")"
    git clone --branch "$REF" --recursive --shallow-submodules --depth 1 \
        https://github.com/espressif/esp-idf.git "$IDF_DIR"
fi

"$IDF_DIR/install.sh" esp32s3
echo
echo "Done. Activate with:  . $IDF_DIR/export.sh"

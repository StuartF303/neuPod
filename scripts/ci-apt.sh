#!/usr/bin/env bash
# CI only: apt-get update + install with short timeouts and retries. If the runner's own mirror
# (azure.archive.ubuntu.com via /etc/apt/apt-mirrors.txt) is unreachable, switch to archive.ubuntu.com and try again.
#   scripts/ci-apt.sh <package>...
set -uo pipefail
(($#)) || { echo "usage: $0 <package>..." >&2; exit 2; }
OPTS=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30)

try() {
    sudo apt-get "${OPTS[@]}" update && sudo apt-get "${OPTS[@]}" install -y --no-install-recommends "$@"
}

try "$@" && exit 0
echo "::warning::apt via the runner's mirror failed; retrying against archive.ubuntu.com"
for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.sources /etc/apt/sources.list.d/*.list; do
    [[ -f $f ]] || continue
    sudo sed -i -E \
        -e 's#mirror\+file:/etc/apt/apt-mirrors\.txt/?#http://archive.ubuntu.com/ubuntu/#g' \
        -e 's#https?://azure\.archive\.ubuntu\.com/ubuntu/?#http://archive.ubuntu.com/ubuntu/#g' "$f"
done
try "$@"

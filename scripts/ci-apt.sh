#!/usr/bin/env bash
# CI only: apt-get update + install that survives the runner's Ubuntu mirror being unreachable.
#   scripts/ci-apt.sh <package>...
# Seen 2026-11-11: runner DNS failed for *.ubuntu.com only (packages.microsoft.com fine); apt-get update then just
# logs "Ign:" after 20 s timeouts, exits 0, and install crawls for >15 min. So probe first: the first mirror whose
# noble InRelease answers within 10 s gets every Ubuntu source (security included), before apt runs at all.
set -uo pipefail
(($#)) || { echo "usage: $0 <package>..." >&2; exit 2; }
. /etc/os-release
CANDIDATES=(
    http://azure.archive.ubuntu.com/ubuntu
    http://archive.ubuntu.com/ubuntu
    http://mirrors.edge.kernel.org/ubuntu
)
pick=""
for m in "${CANDIDATES[@]}"; do
    if curl -fsS -o /dev/null -m 10 "$m/dists/$VERSION_CODENAME/InRelease"; then pick=$m; break; fi
    echo "::warning::apt mirror $m unreachable from this runner"
done
[[ -n $pick ]] || { echo "::error::no Ubuntu mirror reachable (tried ${CANDIDATES[*]})"; exit 1; }
echo "apt mirror: $pick"
for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.sources /etc/apt/sources.list.d/*.list; do
    [[ -f $f ]] || continue
    sudo sed -i -E \
        -e "s#mirror\\+file:/etc/apt/apt-mirrors\\.txt/?#$pick/#g" \
        -e "s#https?://(azure\\.archive|archive|security)\\.ubuntu\\.com/ubuntu/?#$pick/#g" "$f"
done
OPTS=(-o Acquire::Retries=3 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20)
sudo apt-get "${OPTS[@]}" update && sudo apt-get "${OPTS[@]}" install -y --no-install-recommends "$@"

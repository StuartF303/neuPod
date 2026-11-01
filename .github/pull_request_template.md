## Summary

<!-- What does this change and why? Link issues: Fixes #123 -->

## Platforms affected

- [ ] rp2350
- [ ] esp32s3
- [ ] host
- [ ] docs / tooling only

## Checklist

- [ ] Conventional-commit messages; branch named `feat/…`, `fix/…`, `docs/…`, `hw/…` or `chore/…`
- [ ] `scripts/build.sh rp2350` builds cleanly
- [ ] Host tests pass (`ctest --test-dir build/host`)
- [ ] `clang-format` applied to changed C files
- [ ] `CHANGELOG.md` (Unreleased) and docs updated where behaviour changes
- [ ] Hardware facts are sourced; unknowns marked `TODO`

## Hardware testing

<!-- Board and what was verified, or "not hardware-tested". -->

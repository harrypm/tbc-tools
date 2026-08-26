# Validated restore point — macOS build fix (cross-platform CI all green)

**Date:** 2026-07-18
**Status:** FIXED / FULLY WORKING (confirmed by hard data: CI green)
**Validated at commit:** `4ae4472` (HEAD -> main, origin/main)

## What was broken
Release run 29631537388 (sha 3ca8c3c): both macOS jobs failed while
Win/Linux/Windows all succeeded.

- arm64 (`macos-latest`): `nix build .#` failed — Qt `uic` SIGTRAP on
  macOS 26 (CMake AUTOUIC test failed -> "CMake Generate step failed").
  Cause: `macos-latest` label migrated to macos-26 on 2026-07-15.
- x86_64 (`macos-15-intel`): `nix build .#` succeeded, but the ffmpeg
  fallback `nix build nixpkgs#ffmpeg.bin` (live channel) threw
  "Nixpkgs 26.11 has dropped support for x86_64-darwin", hidden by
  `2>/dev/null` -> step printed "Missing ffmpeg from nixpkgs build" exit 1.

Neither failure was caused by the repo's recent commits (flake.lock unchanged
since 2026-07-01; flake.nix changes darwin-inert; no source/CMakeLists touched).
Both were external infrastructure changes in the 17-day gap since macOS last built.

## The fix (commit 4ae4472)
1. Pin arm64 runner: `macos-latest` -> `macos-15` (GitHub's documented
   mitigation; matches x86_64 macos-15-intel).
2. ffmpeg fallback: live `nixpkgs#ffmpeg.bin` -> flake's pinned `.#ffmpeg^bin`
   (flake.lock nixpkgs still supports x86_64-darwin; insulates both arches
   from channel rolls). nix stderr now captured + printed on failure.
3. CI contract enforcement: MACOS_REQUIRED_SNIPPETS += `runner: macos-15`,
   `nix build .#ffmpeg^bin`; MACOS_FORBIDDEN_SNIPPETS += `runner: macos-latest`,
   `nix build nixpkgs#ffmpeg.bin`; +3 unit tests.

## Verification (hard data)
- Build macOS tools run 29633046640 (sha 4ae4472):
  `gh run view 29633046640` ->
  ```
  run=completed conclusion=success
    Build tbc-tools (arm64): success
    Build tbc-tools (x86_64): success
    Build universal macOS app/dmg: success
  ```
- arm64: `macos-15` pin fixed the uic SIGTRAP -> `nix build .#` succeeded.
- x86_64: `.#ffmpeg^bin` flake fallback produced a usable ffmpeg (no
  "Missing ffmpeg") -> build reached Create DMG + Upload binary artifact.
- Universal lipo merge of the two arches -> universal app/dmg built.
- Local pre-push checks: contract checker passed; 34 unit tests OK;
  actionlint 1.7.12 on build_macos_tools.yml exit 0.

## Restore point
This .md + the companion zip (`macos-build-fix-validated_2026-07-18.zip`)
preserve the validated state at commit 4ae4472 as a go-back point.

## Files in the fix (commit 4ae4472)
- .github/workflows/build_macos_tools.yml (arm64 runner pin + flake ffmpeg)
- ci/check_ci_contracts.py (new MACOS required/forbidden snippets)
- ci/tests/test_check_ci_contracts.py (+3 tests)

## Open / notes
- macos-15 arm64 pin should be re-evaluated when flake.lock bumps nixpkgs
  to a Qt that supports macOS 26.
- x86_64-darwin is EOL on GitHub Actions (macos-15-intel available until
  Aug 2027) and dropped from Nixpkgs 26.11; longer-term x86_64 macOS
  support needs a decision (drop, or keep pinning macos-15-intel + flake nixpkgs).

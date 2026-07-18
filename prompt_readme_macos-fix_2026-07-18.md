# Prompt log — macOS builds broken (investigation + fix)

**Date:** 2026-07-18
**Repo:** /home/harry/tbc-tools
**Branch:** main
**Triggering report:** User: "Win/Linux looks okay but macos builds go broken https://github.com/harrypm/tbc-tools/actions/runs/29631537388"

## User intent
Diagnose why macOS builds broke in Release run 29631537388 (sha 3ca8c3c) and fix.
Win/Linux + Windows (x86_64+arm64) all succeeded in that run; only macOS failed.

## Investigation (hard data)

### Run 29631537388 (Release, sha 3ca8c3c) job results
- Resolve release tag: success
- Build Linux binaries (x86_64 + arm64): success
- Build Windows binaries (x86_64 + arm64): success  <- Windows CUDA DLL bundling + -Force license fix both verified green
- Build macOS binaries (x86_64): FAILURE
- Build macOS binaries (arm64): FAILURE
- universal dmg / upload: skipped

### macOS x86_64 failure (job 88045997257)
- Failed step: "Build with Nix" -> "Missing ffmpeg from nixpkgs build" (exit 1)
- `nix build .#` SUCCEEDED (built /nix/store/...-ld-decode-tools-3.2.5.drv)
- The ffmpeg resolution block (build_macos_tools.yml:97-105) then ran:
  1. `nix-store -q --references result | grep '/ffmpeg-[^/]+-bin$'` -> empty (ffmpeg-bin not in result closure; only lib is)
  2. fallback `nix build nixpkgs#ffmpeg.bin --no-link --print-out-paths 2>/dev/null` -> empty, returned in ~0s
  -> "Missing ffmpeg" exit 1. The 2>/dev/null hid the real nix error.
- Root cause (reproduced locally via `nix eval` for x86_64-darwin against live nixos-unstable):
  ```
  error: Nixpkgs 26.11 has dropped support for x86_64-darwin.
  ```
  The macOS workflow's ffmpeg fallback uses the LIVE channel (NIX_PATH=nixpkgs=channel:nixos-unstable, line 54),
  which dropped x86_64-darwin. The flake's pinned nixpkgs (flake.lock) still supports x86_64-darwin
  (proven: `nix build .#` succeeded).

### macOS arm64 failure (job 88045997269)
- Failed step: "Build with Nix" -> `nix build .#` itself FAILED:
  ```
  error: builder for '/nix/store/9j9hjfshhvqkvb2ai3vl1sw33izpg11s-ld-decode-tools-3.2.5.drv' failed with exit code 1
  CMake Error: AUTOUIC for target ld-analyse: Test run of "uic" executable ... failed. SIGTRAP
  CMake Generate step failed. Build files cannot be regenerated correctly.
  (also: Package 'ogg', required by 'flac', not found -- pkg-config warning)
  ```
- Root cause: arm64 matrix used `runner: macos-latest`. Per GitHub actions/runner-images issue #14167,
  the `macos-latest` label migrated to **macos-26 (Tahoe)** completing **2026-07-15**. The run was
  2026-07-18 (after migration). The flake.lock-pinned nixpkgs Qt `uic` crashes (SIGTRAP) on macOS 26.
  GitHub's documented mitigation: "Switch back to macOS-15 by specifying the `macos-15` label."

### Not caused by my recent commits (hard data)
- `git diff --name-only 5a7d1e6 3ca8c3c` (my 2 commits af79464 + 3ca8c3c): only
  build_windows_tools.yml, .gitignore, AGENTS.md, ci/check_ci_contracts.py,
  ci/tests/test_check_ci_contracts.py, scripts/cuda-closure-cache.sh, scripts/windows-cuda-runtime.sh.
  Zero macOS / flake / CMakeLists / source files.
- `git diff --stat eb9d5e7 HEAD -- flake.lock flake.nix`: flake.lock UNCHANGED since last good macOS
  build (2026-07-01). flake.nix +15/-1 but those changes are darwin-inert (packageVersion 3.2.3->3.2.5
  + CUDA pin-drift assertions gated on `enableCuda` which is false on darwin).
- Last standalone macOS build before this: 2026-07-01 (sha eb9d5e7, success). 17-day gap during which
  macos-latest->macos-26 migration + Nixpkgs x86_64-darwin drop both occurred.

## Fix (commit 4ae4472)

### .github/workflows/build_macos_tools.yml
1. Pin arm64 runner: `macos-latest` -> `macos-15` (matches x86_64 macos-15-intel; GitHub's recommended
   mitigation for the macos-26 migration; restores the macOS 15 env where pinned Qt uic works).
2. ffmpeg fallback: `nix build nixpkgs#ffmpeg.bin` (live channel) -> `nix build .#ffmpeg^bin`
   (flake's pinned nixpkgs; supports x86_64-darwin; insulates both arches from channel rolls).
   Capture nix stderr to a temp file and print on failure (was hidden by 2>/dev/null) so future
   ffmpeg errors are diagnosable.
- Kept the `nix-store -q --references result | grep ...-bin` primary path (harmless; finds nothing
  since ffmpeg-bin isn't in result's closure).

### ci/check_ci_contracts.py
- MACOS_REQUIRED_SNIPPETS += "runner: macos-15", "nix build .#ffmpeg^bin"
- MACOS_FORBIDDEN_SNIPPETS += "runner: macos-latest", "nix build nixpkgs#ffmpeg.bin"
  (precise code patterns so the explanatory comments can still mention the bare labels)

### ci/tests/test_check_ci_contracts.py
- +3 tests: test_macos_contract_pins_runners_and_uses_flake_ffmpeg,
  test_macos_contract_forbids_drifting_runner_and_live_channel_ffmpeg,
  test_macos_workflow_uses_pinned_runner_and_flake_ffmpeg (integration: reads workflow content)

## Local verification (all passed before push)
- `python3 ci/check_ci_contracts.py` -> CI contract checks passed.
- `python3 -m unittest ci.tests.test_check_ci_contracts` -> Ran 34 tests ... OK (was 31, +3)
- actionlint 1.7.12 on build_macos_tools.yml -> ACTIONLINT_EXIT=0
- `nix eval` x86_64-darwin nixos-unstable ffmpeg.bin -> "Nixpkgs 26.11 has dropped support for x86_64-darwin" (reproduced root cause)
- `nix build .#ffmpeg^bin --dry-run` (local linux) -> exit 0 (attribute resolves)

## Commands run (key ones)
- gh run view 29631537388 / gh run view --job=88045997257 --log-failed / --job=88045997269 --log-failed
- git --no-pager diff --name-only 5a7d1e6 3ca8c3c ; git --no-pager diff --stat eb9d5e7 HEAD -- flake.lock flake.nix
- nix eval --impure --expr '... x86_64-darwin ... ffmpeg.bin.outPath'  (reproduced drop error)
- nix eval --impure --expr '... aarch64-darwin ... ffmpeg.bin.outPath'  (succeeded: /nix/store/...-ffmpeg-8.1.2-bin)
- nix eval --impure --expr '... pkgs.ffmpeg.outputs'  -> [ "bin" "lib" "dev" "doc" "man" "data" "out" ] (bin output exists)
- nix build .#ffmpeg^bin --no-link --dry-run --print-out-paths  -> exit 0
- exa_web_search "GitHub Actions macos-latest runner label current version 2026" (confirmed macos-26 migration)
- git commit -F - (4ae4472) ; git push origin main (3ca8c3c..4ae4472)
- gh workflow run "Build macOS tools" --ref main -> run 29633046640

## CI verification (in progress)
- Triggered Build macOS tools on main (sha 4ae4472): https://github.com/harrypm/tbc-tools/actions/runs/29633046640
- Both jobs (arm64 macos-15, x86_64 macos-15-intel) in_progress.
- Pending: confirm arm64 `nix build .#` succeeds (uic OK on macos-15) + x86_64 `.#ffmpeg^bin` succeeds
  (no "Missing ffmpeg") + both jobs complete -> then re-trigger Release to confirm green.

## Notes / open
- The `.#ffmpeg^bin` fix is high-confidence but not 100% verified on darwin locally (I'm on linux);
  the CI run is the hard-data confirmation.
- macos-15 arm64 runner pin should be re-evaluated when flake.lock bumps nixpkgs to a Qt that supports macOS 26.
- The x86_64-darwin architecture is end-of-life on GitHub Actions (macos-15-intel available until Aug 2027
  per actions/runner-images#13045) and dropped from Nixpkgs 26.11. Longer-term, x86_64 macOS support will
  need a decision (drop it, or keep pinning macos-15-intel + flake-pinned nixpkgs).

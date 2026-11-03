# Prompt README — AV1 python test fix (use bundled/pulled ffmpeg) (2026-09-22)

## User prompt
> "No this is good, now I need you to fix this av1 test issue it should use the same ffmpeg that is bundled and or would be pulled so update my local ffmpeg if need be"

## Root cause (hard data)
- `av1`/`av1_web` pytest profiles failed instantly: the AV1 profiles encode with
  `libsvtav1` (`config/default.py`), which the system ffmpeg 4.4.8 lacks
  (only `libaom-av1`). The python tests ran in the venv against the system PATH.
- The project's bundled/pulled Linux ffmpeg is the **Nix dev shell ffmpeg 7.1.1**
  (from `flake.nix`), which has both `libsvtav1` and `libaom-av1`.
- Brew ffmpeg 8.1.1 (installed locally) also has `libsvtav1` but still failed the
  profiles instantly — an additional ffmpeg-8 incompatibility, so upgrading the
  system ffmpeg to brew's would NOT have fixed it. The Nix ffmpeg is the match.
- CI note: GitHub Actions never runs the pytest suite (only the CI contract
  unittest), so these failures were local-only and never blocked CI.

## Fix
- `ci/run_local_ci_parity.sh`: added `run_python_tests()` — runs the
  `src/tbc-video-export` pytest suite inside the root Nix dev shell with
  `.venv/bin` + `build/bin` prepended to PATH, so tests use the same bundled
  ffmpeg as the release builds and the freshly built tools. Skips with a notice
  if the venv has no pytest.
- New scoped mode `--python-tests-only`; also wired into `--all`.
- README "Local CI parity checks" section documents the new mode and why.

## Validation (hard data, 2026-09-22)
- Direct invocation of the new mode: `bash ci/run_local_ci_parity.sh --python-tests-only`
  → **317 passed in 44.82s** (both AV1 profiles now pass; 315 passed + 2 failed before).
- `python3 ci/check_ci_contracts.py`: passed. `bash -n` syntax check: clean.

## Status
Complete; committed and pushed. Local system ffmpeg was NOT changed (not needed —
the bundled Nix ffmpeg is used for the tests).

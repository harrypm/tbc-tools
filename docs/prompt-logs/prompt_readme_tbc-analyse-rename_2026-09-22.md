# Prompt README: BUILD.md move + ld-analyse -> tbc-analyse rename
Date: 2026-09-22
Working dir: /home/harry/tbc-tools (branch: main)

## User prompts (inputs)
1. "lets commit and finish the rename"
2. "but first make a quick edit moving the manual build stuff to BUILD.md for the tbc-tools repo"
3. "rename ld-analayse to tbc-analyse" (the rename request that started this session's research)

## Context from prior sessions (hard data)
- Phase 1 (2026-08-18, `prompt_readme_internal-rename_2026-08-18.md`): internal SPDX/comment rename only; app/exe name surfaces deliberately deferred.
- Phase 2 metadata rename (2026-08-23, `prompt_readme_ld-to-tbc-rename_2026-08-23.md`): "Deferred (next step)" list named exactly this step: `ld-analyse` -> `tbc-analyse` incl. workflows, CI contract constants, .desktop/MIME/icon install paths, READMEs, AGENTS.md hard rules.
- Pending from previous prompt: export boundary thickness stock default 4 px -> 2 px change (committed separately here; GUI confirmation still outstanding).

## Changes made
### 1. BUILD.md move (commit `391645e` "docs: move manual distro build instructions to BUILD.md")
- Moved README.md "Native distro packages (manual build)" section (submodules, Debian/Fedora/Arch/openSUSE deps, build/install commands) into the existing `BUILD.md` as a "# Manual build (native distro packages)" part (BUILD.md already existed with Nix content; title broadened to "# Build").
- README.md section replaced with a pointer to `BUILD.md`.

### 2. Boundary thickness default (commit `8540ebe` "ld-analyse: set stock export boundary thickness default to 2 px")
- configuration.{h,cpp} from the previous session: stock default 4 -> 2 px, untouched-config migration (4 + no user-set flag -> 2), `exportBoundaryThicknessUserSet` flag written on manual change.

### 3. Rename (commit `5f75645` "rename ld-analyse to tbc-analyse (app, exe, desktop, CI, docs)")
- git mv: `src/ld-analyse` -> `src/tbc-analyse`; `ld-analyse-resources.qrc` -> `tbc-analyse-resources.qrc`; `install/ld-analyse.desktop` -> `tbc-analyse.desktop`; `docs/Tools/ld-analyse.md` -> `tbc-analyse.md`; 25 doc asset PNGs `ld-analyse_*.png` -> `tbc-analyse_*.png` (docs/Tools/assets + docs/How-to-guides/assets).
- Bulk exact-token replace (perl, NUL-safe while-read loop): `ld-analyse` -> `tbc-analyse` and `LD_ANALYSE_` -> `TBC_ANALYSE_` across 67 tracked files (+307/-307). First attempt failed silently (broken NUL handling in a printf/xargs pipeline — zero files touched, verified via `git diff --stat` before redoing).
- Targeted extras:
  - `configuration.cpp`: settings file renamed to `tbc-analyse.ini` + one-time migration (copy legacy `ld-analyse.ini` -> `tbc-analyse.ini` if new file missing; old file kept as backup). `#include <QFile>` added.
  - `generate_release_notes.py`: bucket renamed to "tbc-analyse"; matcher still accepts historical `ld-analyse` commit subjects so old commits keep bucketing.
- Excluded (historical/vendor, unchanged): development-logs/, docs/restore-points/, docs/prompt-logs/, docs/fix_notes/, test-artifacts/, src/audio-align/vendor/, TELETEXT_CENTERING_FIX.md, WST_DECODER_INTEGRATION.md.
- Audio-align wire marker `ld-analyse-export-audio-tracks` renamed in lockstep — verified there is no separate reader (single occurrence, writer only), and writer+reader ship in the same binaries.

## Commands run (verification)
- `git mv` chains (all succeeded).
- Bulk replace + residual `git grep -l "ld-analyse"` (exclusion-filtered) -> 0 in-scope residuals; then exactly 2 intentional literal holders after targeted edits (configuration.cpp legacy INI migration, release-notes historical matcher).
- `rm -rf build && nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && nix develop -c ninja -C build` -> success, produced `build/bin/tbc-analyse` (4342264 bytes, 2026-09-22 18:57); `build/bin/ld-analyse` confirmed absent.
- `python3 ci/check_ci_contracts.py` -> "CI contract checks passed."
- Pre-existing warnings only: dirty git tree (expected mid-rename), CUDA <12 Nixpkgs 25.05 eval warning, ld-lds-converter linuxbrew libFLAC rpath warning, teletextviewerdialog trigraph warning.

## Commits (this session; hashes after push rebase)
- `b0f22e5` docs: move manual distro build instructions to BUILD.md
- `1afb908` ld-analyse: set stock export boundary thickness default to 2 px
- `38badfd` rename ld-analyse to tbc-analyse (app, exe, desktop, CI, docs) — 209 files, +358/-309

(Pre-rebase hashes were 391645e / 8540ebe / 5f75645.)

## Push (user: "Push")
- First push rejected (remote had `24269a1 chore(release): prepare v3.2.9`, tag v3.2.9, touching only flake.nix + vcpkg.json — orthogonal to these commits).
- `git pull --rebase origin main` (conflict-free) then `git push origin main` -> `24269a1..38badfd main -> main`. HEAD == origin/main == 38badfd. Push triggers GitHub Actions CI (tests call the release workflow, no publish).

## Not yet verified / pending
- GitHub Actions CI result on 38badfd (triggered by the push, outcome not yet checked).
- Real-world GUI confirmation still outstanding (user rules Gx7w791EYIjU04S4dWrV5y): (a) boundary renders at 2 px stock, (b) app now presents as tbc-analyse (window title/menu entry), (c) settings carried over from ld-analyse.ini on first run of the renamed build.
- No zip restore point yet (user has not stated the changes are fully working; per rule udQirjAOEYGyA029HzncJp that triggers on user confirmation).

# Prompt README - Internal rename (phase 1): ld-decode-tools -> tbc-tools, ld-analyse -> tbc-analyse

**Date:** 2026-08-18
**Repo:** /home/harry/tbc-tools (https://github.com/harrypm/tbc-tools.git)
**Branch:** main
**Plan:** "Internal rename: ld-decode-tools -> tbc-tools, ld-analyse -> tbc-analyse (phase 1, no app-name changes)"
**Status:** Executed + verified. NOT committed (awaiting user request).

---

## User input
- "Commit, then start project cleanup internal rename for ld-analyse & ld-decode-tools to tbc-analyse / tbc-tools don't change the app names yet and exclude ld-discmap, ld-disc-stacker"
- "Execute this plan." (approved the plan as written)

## Commit performed first (update checker, prior session)
- `631b6d4` feat(ld-analyse): add GitHub release update checker (manual + weekly) — 9 files, +481/-2.

## Verified hard data (gathered before editing)
Per rule (never assume without hard data), the exact replacement strings were verified against tracked files:
- Pattern inventory (git grep -lzF, tracked files):
  - P1 `This file is part of ld-decode-tools.` -> 334 tracked files (12 under src/ld-discmap|ld-disc-stacker; 1 in historical artifacts)
  - `ld-decode-tools` bare token total -> 324 in-scope files (after excluding excluded dirs / vendor / historical / AGENTS.md)
  - ld-analyse header doc-line ` * ld-analyse - TBC output analysis GUI` -> 42 (1 in a historical file)
  - ld-analyse header doc-line ` * ld-analyse - Dedicated EFM/AC3 handling workflow GUI` -> 2 (efmhandlerdialog.{cpp,h})
- `ld-decode-tools` variants found beyond the SPDX sentence (all covered by a single literal token replace since `ld-decode-tools` is not a substring of `ld-decode`, `ld-decode-testdata`, or `ld-analyse`):
  - GPL boilerplate `ld-decode-tools is free software...`
  - Version strings `setApplicationVersion("ld-decode-tools - Branch: %1 / Commit: %2")` (audio-align, tbc-export-metadata, ld-chroma-decoder main.cpp's)
  - Shell-comment SPDX `# This file is part of ld-decode-tools.` + `# ld-decode-tools is free software...` (scripts/test-teletext-export)
  - CUDA comment `Target: GTX-1000-series (Pascal) CUDA support for ld-decode-tools.` (scripts/cuda-closure-cache.sh)
  - `ld-decode-tools filter library` (src/library/filter/firfilter.h)
- Edge cases checked: flake.nix (description + pname), CMakeLists.txt (project() + in-source-build hints), AGENTS.md (no `ld-decode-tools` literal), .github/workflows (no `ld-decode-tools` literal), vendor CHANGELOG (third-party `ld-analyze ... ld-decode-tools` reference - excluded).
- Exclusion filter confirmed meaningful: excluded dirs hold 12 P1 matches (correctly left unchanged); historical holds 1.
- `ld-analyse` exe-name surfaces to preserve: `setApplicationName("ld-analyse")`, `setDesktopFileName("ld-analyse")`, `QIcon::fromTheme("ld-analyse")` (main.cpp:232/238/242), `install/ld-analyse.desktop` (Name/Exec/Icon), CLI description, window titles, scripts/release/generate_release_notes.py commit-bucket @register("ld-analyse").
- `LdDecodeMetaData` class / `lddecodemetadata.h` / `lddecode-library` CMake target: upstream ld-decode lineage, NOT the project name - explicitly out of scope.

## Commands run
1. `git add <update-checker files> && git commit` -> `631b6d4` (update checker; see prior session).
2. Inventory: `git grep -lz 'ld-decode-tools'` / `git grep -lz ' * ld-analyse - ...'` + per-file grep counts (read-only).
3. Mass exact-string rename (exclusion filter applied via `grep -vE`):
   ```
   EXCL='^(src/ld-discmap/|src/ld-disc-stacker/|src/audio-align/vendor/|prompt_readme.*\.md|prompt readme\.md|development-logs/|docs/restore-points/|docs/prompt-logs/|AGENTS\.md$)'
   # Edit 1 (324 files): perl -0pi -e 's/\Qld-decode-tools\E/tbc-tools/g'
   # Edit 2 (41 files):  perl -0pi -e 's/\Q * ld-analyse - TBC output analysis GUI\E/ * tbc-analyse - TBC output analysis GUI/g'
   # Edit 3 (2 files):   perl -0pi -e 's|...Dedicated EFM/AC3...|...tbc-analyse - Dedicated EFM/AC3...|g'  (used | delimiter - see failure note)
   ```
4. **Failure + recovery on Edit 3:** the first Edit-3 attempt used `/` as the `s///` delimiter; the `/` inside `EFM/AC3` collided, Perl died (`Can't locate object method "Dedicated" via package "EFM"`) before writing. Verified both target files (`src/ld-analyse/efmhandlerdialog.{cpp,h}`) were not corrupted (Edit-1's tbc-tools SPDX had applied; Edit-3 wrote nothing). Re-ran Edit 3 with `|` delimiter -> success. Headers now read ` * tbc-analyse - Dedicated EFM/AC3 handling workflow GUI`.
5. Scope verification (read-only): `git --no-pager diff --stat`, `git --no-pager diff --name-only | grep -E ...`, residual `git grep` checks.
6. Clean reconfigure + full build:
   - `rm -rf build && nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` -> `-- Configuring done (15.7s)` / `-- Build files have been written to: .../build`
   - `nix develop -c ninja -C build` -> `[294/294] Linking CXX executable bin/ld-analyse` (full build complete; only pre-existing ld-lds-converter linuxbrew/FLAC rpath warning, unrelated)
7. CI + surface checks: `python3 ci/check_ci_contracts.py` -> `CI contract checks passed.`; app-name surface grep + `cat` of desktop file.

## Files changed
326 tracked files, 467 insertions / 467 deletions (pure renames - line-for-line swaps). Categories:
- SPDX header `This file is part of ld-decode-tools.` -> `This file is part of tbc-tools.` (~322 source files across src/, prototypes/, scripts/)
- GPL boilerplate `ld-decode-tools is free software...` -> `tbc-tools is free software...` (library headers, scripts/test-teletext-export)
- Library/filter header doc-lines ` * ld-decode-tools TBC library` / `ld-decode-tools filter library` -> ` * tbc-tools TBC library` / `tbc-tools filter library`
- Version-string literal `ld-decode-tools - Branch: ...` -> `tbc-tools - Branch: ...` (audio-align, tbc-export-metadata, ld-chroma-decoder main.cpp's)
- ld-analyse header doc-lines ` * ld-analyse - TBC output analysis GUI` -> ` * tbc-analyse - TBC output analysis GUI` (41 files) and ` * ld-analyse - Dedicated EFM/AC3 handling workflow GUI` -> ` * tbc-analyse - Dedicated EFM/AC3 handling workflow GUI` (efmhandlerdialog.{cpp,h})
- `CMakeLists.txt`: `project(ld-decode-tools)` -> `project(tbc-tools)` (line 48); in-source-build error hints `../ld-decode-tools-build` -> `../tbc-tools-build`, `../ld-decode-tools` -> `../tbc-tools` (lines 43-45)
- `flake.nix`: `description = "ld-decode-tools (Nix flake)"` -> `"tbc-tools (Nix flake)"` (line 2); `pname = "ld-decode-tools"` -> `pname = "tbc-tools"` (line 145)
- `README.md`: project-name reference `ld-decode-tools` -> `tbc-tools` (releases-page line)
- `scripts/cuda-closure-cache.sh`: CUDA-support comment `ld-decode-tools` -> `tbc-tools` (line 302)

## Files NOT changed (by design)
- `src/ld-discmap/**` and `src/ld-disc-stacker/**` - excluded entirely (their `part of ld-decode-tools.` SPDX line retained for now; accepted temporary inconsistency).
- `src/audio-align/vendor/**` - third-party vendored tree (CHANGELOG mentions `ld-analyze ... ld-decode-tools`).
- App/exe name surface (preserved as `ld-analyse`): `setApplicationName`, `setDesktopFileName`, `QIcon::fromTheme`, `install/ld-analyse.desktop` (Name/Exec/Icon), CLI description, window titles, icon resource/theme name, `scripts/release/generate_release_notes.py` @register("ld-analyse").
- `ci/check_ci_contracts.py` (hardcoded `src/ld-analyse/exportdialog.cpp` path + `LD_ANALYSE_*` identifiers) and `src/ld-analyse/exportdialog.cpp` enforced snippets.
- Directory `src/ld-analyse/`, CMake target `ld-analyse`, exe output name, `tbc-efm-handler` target.
- `LdDecodeMetaData` class, `lddecodemetadata.h` filename, `lddecode-library` CMake target (upstream ld-decode lineage).
- Other `ld-*` tool names (ld-chroma-decoder, ld-process-vbi, ld-process-vits, ld-lds-converter, ld-dropout-correct) - only their SPDX `part of` line changed.
- `.github/workflows/*.yml`, `AGENTS.md` (live exe/CI contracts by current exe name).
- Historical session artifacts: `prompt_readme*.md`, `prompt readme.md`, `development-logs/`, `docs/restore-points/`, `docs/prompt-logs/`, root validation `*.md`/`*.zip`.

## Verification results
- **Scope:** `git diff --name-only` shows zero files under `src/ld-discmap/`, `src/ld-disc-stacker/`, `src/audio-align/vendor/`, or any historical-artifact path. 326 files / +467 / -467 (line-for-line).
- **Residual grep:** `git grep -ln 'ld-decode-tools'` minus excluded dirs/vendor/historical -> `(none outside excluded - correct)`. `git grep -lF 'This file is part of ld-decode-tools.' -- src/` minus excluded dirs -> `(none - correct)`.
- **ld-analyse headers:** residual ` * ld-analyse -` under `src/ld-analyse/` -> `(none - correct)`; new ` * tbc-analyse -` count under `src/ld-analyse/` -> 43.
- **Configure:** clean `rm -rf build && cmake` succeeded; `project(tbc-tools)` accepted; only the pre-existing ld-lds-converter linuxbrew/FLAC rpath warning (unrelated to this change).
- **Build:** `ninja -C build` -> `[294/294] Linking CXX executable bin/ld-analyse`; all 31 executables present in `build/bin/` (including the excluded ld-discmap / ld-disc-stacker, which still built from their unchanged sources).
- **CI contracts:** `python3 ci/check_ci_contracts.py` -> `CI contract checks passed.` (enforced paths/snippets untouched).
- **App-name surfaces intact:** `setApplicationName("ld-analyse")`, `setDesktopFileName("ld-analyse")`, `QIcon::fromTheme("ld-analyse")` (main.cpp:232/238/242); `.desktop` Name/Exec/Icon = `ld-analyse`.
- **Project name in build config:** `CMakeLists.txt:48 project(tbc-tools)`; `flake.nix:145 pname = "tbc-tools"`; `flake.nix:2 description = "tbc-tools (Nix flake)"`.

## Notes / deferred
- **Phase 1 only ("start project cleanup").** Phase 2 (later, explicit request): rename the actual app/exe names - directory `src/ld-analyse/`, CMake target `ld-analyse`, exe output name, `.desktop` Name/Exec/Icon, `setApplicationName`/`setDesktopFileName`, icon theme name, window titles, CLI description, `ci/check_ci_contracts.py` `LD_ANALYSE_*` identifiers + hardcoded path, `scripts/release/generate_release_notes.py` @register, `.github/workflows` build refs. Intentionally NOT started here.
- Exclusion of `ld-discmap`/`ld-disc-stacker` leaves their SPDX `part of ld-decode-tools.` line in place (temporary inconsistency); corrected in a later pass if desired.
- Build-system name changes (`project()`, flake `pname`, flake `description`) change the Nix output store-path derivation name (`ld-decode-tools-3.2.6` -> `tbc-tools-3.2.6`) and the CMake project name; these are internal build identities, not installed app names. No installed exe/desktop/icon name changed.
- The Edit-3 perl `s///` `/`-delimiter collision is logged above as a process note; recovery verified both files intact and the rename applied correctly.
- Changes are NOT committed. No zip/restore-point created (rename is a non-validated-by-runtime code/comment change; the full build + CI checker are the validation, and the user has not stated it "fixed/fully working").
